// SPDX-License-Identifier: MIT
#include "artbox/native_dlfcn.h"
#include "artbox/native_call.h"
#include "artbox/native_thread.h"
#include "artbox/native_vm.h"
#include <dlfcn.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "loader host check failed at %d\n", __LINE__); return 1; } } while (0)
struct state {
    artbox_guest_dl_thread *primary, *worker;
    uint64_t check, stage, extension_check, worker_stack, stack_size;
    pthread_mutex_t mutex;
    pthread_cond_t condition;
    unsigned ready, release, cases;
    int worker_error, result;
};
static int call(uint64_t address, unsigned argument) {
    return (int32_t)artbox_call7((void *)(uintptr_t)address, argument, 0, 0, 0, 0, 0, 0);
}
static void worker(void *opaque) {
    struct state *s = opaque;
    artbox_guest_dl_thread *previous = artbox_native_dlfcn_swap(s->worker);
    if (previous || call(s->stage, 2) || call(s->stage, 0)) s->worker_error = 1;
    else s->cases = 2;
    pthread_mutex_lock(&s->mutex);
    s->ready = 1;
    pthread_cond_broadcast(&s->condition);
    while (!s->release) pthread_cond_wait(&s->condition, &s->mutex);
    pthread_mutex_unlock(&s->mutex);
    if (call(s->stage, 1)) s->worker_error = 1;
    else ++s->cases;
    if (artbox_native_dlfcn_swap(previous) != s->worker) s->worker_error = 1;
}
static int run_primary(struct state *s) {
    int observed = call(s->check, 0);
    if (observed != 32) { fprintf(stderr, "guest loader case failed: %d\n", observed); return 1; }
    int extensions = call(s->extension_check, 0);
    int namespace_control = call(s->extension_check, 1);
    int flags_control = call(s->extension_check, 2);
    if (extensions != 51 || namespace_control != -1001 || flags_control != -1009) {
        fprintf(stderr, "guest loader extensions: %d; controls: %d %d\n",
            extensions, namespace_control, flags_control);
        return 1;
    }
    artbox_thread_ops threads = artbox_native_threads();
    void *handle = NULL;
    CHECK(!threads.start((void *)(uintptr_t)s->worker_stack, (size_t)s->stack_size, worker, s, &handle));
    pthread_mutex_lock(&s->mutex);
    while (!s->ready) pthread_cond_wait(&s->condition, &s->mutex);
    int main_ok = !call(s->stage, 0) && !call(s->stage, 1) && !call(s->stage, 2);
    s->release = 1;
    pthread_cond_broadcast(&s->condition);
    pthread_mutex_unlock(&s->mutex);
    CHECK(!threads.join(handle) && main_ok && !s->worker_error && s->cases == 3);
    return 0;
}
static void primary(void *opaque) {
    struct state *s = opaque;
    artbox_guest_dl_thread *previous = artbox_native_dlfcn_swap(s->primary);
    s->result = previous ? 1 : run_primary(s);
    if (artbox_native_dlfcn_swap(previous) != s->primary) s->result = 1;
}
static artbox_elf_result invoke(void *context, uint64_t function, const uint64_t args[3], uint64_t *result) {
    (void)context;
    *result = artbox_call7((void *)(uintptr_t)function, args[0], args[1], args[2], 0, 0, 0, 0);
    return ARTBOX_ELF_OK;
}
static artbox_elf_result reject_constructor(void *context, uint64_t address) {
    (void)context; (void)address;
    return ARTBOX_ELF_INVALID;
}
int main(int argc, char **argv) {
    if (argc != 9) return 2;
    const char *names[4] = {"libartbox_loader_client.so", "libdl.so", "libartbox_loader_provider.so", "libdl_android.so"};
    unsigned char *original[4] = {NULL, NULL, NULL, NULL};
    void *library[4] = {NULL, NULL, NULL, NULL};
    artbox_elf elf[4];
    artbox_dynamic dynamic[4];
    artbox_relocation_memory memory[4];
    artbox_link_module module[4];
    artbox_vm_ops vm_ops = artbox_native_vm();
    artbox_vm *vm = artbox_vm_create(&vm_ops, 64 * 1024 * 1024, 128);
    CHECK(vm);
    const uint64_t page = artbox_vm_page_size(vm), stack_size = 1024 * 1024;
    for (unsigned i = 0; i < 4; ++i) {
        FILE *input = fopen(argv[2 + i * 2], "rb");
        CHECK(input && !fseek(input, 0, SEEK_END));
        long length = ftell(input);
        CHECK(length > 0 && length < 1024 * 1024 && !fseek(input, 0, SEEK_SET));
        original[i] = malloc((size_t)length);
        CHECK(original[i] && fread(original[i], 1, (size_t)length, input) == (size_t)length && !fclose(input));
        CHECK(artbox_elf_open(original[i], (size_t)length, &elf[i]) == ARTBOX_ELF_OK && elf[i].type == 3 &&
              elf[i].segment_count == 2 && elf[i].segments[0].virtual_address == 0 &&
              elf[i].segments[0].flags == 5 && elf[i].segments[1].flags == 6 &&
              artbox_dynamic_open(&elf[i], &dynamic[i]) == ARTBOX_ELF_OK &&
              !dynamic[i].init && !dynamic[i].fini && !dynamic[i].init_array.size && !dynamic[i].fini_array.size &&
              !dynamic[i].preinit_array.size && dynamic[i].soname && !strcmp(dynamic[i].soname, names[i]));
        CHECK(dynamic[i].needed_count == (i ? 0 : 3));
        library[i] = dlopen(argv[1 + i * 2], RTLD_NOW | RTLD_LOCAL);
        if (!library[i]) { fprintf(stderr, "%s\n", dlerror()); return 1; }
        unsigned char *rx = dlsym(library[i], "artbox_dynamic_rx"), *rw = dlsym(library[i], "artbox_dynamic_rw");
        CHECK(rx && rw && (uintptr_t)rx % 16384 == 0 &&
              (uintptr_t)rw == (uintptr_t)rx + elf[i].segments[1].virtual_address &&
              !memcmp(rx, original[i], (size_t)elf[i].segments[0].file_size) &&
              !memcmp(rw, original[i] + elf[i].segments[1].file_offset, (size_t)elf[i].segments[1].file_size));
        for (uint64_t n = elf[i].segments[1].file_size; n < elf[i].segments[1].memory_size; ++n) CHECK(rw[n] == 0);
        memory[i] = (artbox_relocation_memory){1, rw, (size_t)elf[i].segments[1].memory_size};
        module[i] = (artbox_link_module){names[i], &dynamic[i], (uintptr_t)rx, &memory[i], 1};
        CHECK(!artbox_vm_register_readonly(vm, rx, (size_t)elf[i].segments[0].memory_size));
        size_t data_size = (size_t)((elf[i].segments[1].memory_size + page - 1) / page * page);
        CHECK(!artbox_vm_register_data(vm, rw, data_size, 3));
    }
    uint64_t baseline = artbox_vm_reserved_bytes(vm);
    artbox_load_group *group = NULL;
    CHECK(artbox_load_group_create(module, 4, names[0], artbox_native_dlfcn_resolve, NULL, &group) == ARTBOX_ELF_OK);
    CHECK(artbox_load_group_count(group) == 4 && artbox_load_group_relocate(group) == ARTBOX_ELF_OK);
    artbox_dlfcn *loader = NULL;
    artbox_guest_dlfcn *service = NULL;
    artbox_guest_dl_ops guest_ops = {NULL, invoke, NULL};
    CHECK(artbox_dlfcn_create_with_namespace(group, NULL, 0, "default", &loader) == ARTBOX_ELF_OK);
    CHECK(artbox_guest_dlfcn_create(loader, group, vm, &guest_ops, &service) == ARTBOX_ELF_OK);
    CHECK(artbox_load_group_initialize(group, reject_constructor, NULL) == ARTBOX_ELF_OK);
    struct state s = {0};
    CHECK(!pthread_mutex_init(&s.mutex, NULL) && !pthread_cond_init(&s.condition, NULL));
    CHECK(artbox_guest_dl_thread_create(service, &s.primary) == ARTBOX_ELF_OK);
    CHECK(artbox_guest_dl_thread_create(service, &s.worker) == ARTBOX_ELF_OK);
    CHECK(artbox_load_group_lookup(group, "artbox_loader_check", NULL, &s.check) == ARTBOX_ELF_OK);
    CHECK(artbox_load_group_lookup(group, "artbox_loader_error_stage", NULL, &s.stage) == ARTBOX_ELF_OK);
    CHECK(artbox_load_group_lookup(group, "artbox_loader_dlext_check", NULL, &s.extension_check) == ARTBOX_ELF_OK);
    int64_t stacks[2];
    for (unsigned i = 0; i < 2; ++i) {
        stacks[i] = artbox_vm_mmap(vm, 0, stack_size + page, 3, 0x22, -1, 0);
        CHECK(stacks[i] > 0 && !artbox_vm_mprotect(vm, (uint64_t)stacks[i], page, 0));
    }
    s.worker_stack = (uint64_t)stacks[1] + page; s.stack_size = stack_size;
    artbox_thread_ops threads = artbox_native_threads();
    void *handle = NULL;
    CHECK(!threads.start((void *)(uintptr_t)((uint64_t)stacks[0] + page), (size_t)stack_size, primary, &s, &handle));
    CHECK(!threads.join(handle) && !s.result);
    CHECK(!pthread_cond_destroy(&s.condition) && !pthread_mutex_destroy(&s.mutex));
    for (unsigned i = 0; i < 2; ++i) CHECK(!artbox_vm_munmap(vm, (uint64_t)stacks[i], stack_size + page));
    artbox_guest_dl_thread_destroy(s.worker); artbox_guest_dl_thread_destroy(s.primary);
    artbox_guest_dlfcn_destroy(service); artbox_dlfcn_destroy(loader); artbox_load_group_destroy(group);
    CHECK(artbox_vm_reserved_bytes(vm) == baseline && !artbox_vm_destroy(vm));
    for (unsigned i = 0; i < 4; ++i) { CHECK(!dlclose(library[i])); free(original[i]); }
    puts("{\"cases\":32,\"thread_error_checks\":6,\"cleanup\":true,"
         "\"extension_cases\":51,\"namespace_control\":-1001,\"flags_control\":-1009}");
    return 0;
}
