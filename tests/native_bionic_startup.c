// Controlled two-image startup fixture. This is not the general guest loader.
#include "artbox/dynamic.h"
#include "artbox/relocation.h"
#include "artbox/native_call.h"
#include "artbox/native_tls.h"
#include "artbox/native_syscall.h"
#include "artbox/native_vm.h"
#include "artbox/native_system.h"
#include "artbox/devices.h"
#include <dlfcn.h>
#include <inttypes.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

typedef struct module {
    unsigned char *original, *rx, *rw;
    void *handle;
    artbox_elf elf;
    artbox_dynamic dynamic;
    artbox_relocation_stats relocations;
} module;
static module images[2];
static artbox_vm *vm;
static artbox_devices *devices;
static artbox_kernel_thread thread;
static unsigned calls, absent_netd, constructors;
static unsigned unsupported[512];
static int64_t result;
static unsigned force_sampling;
static uint64_t gwp_enabled, guarded_samples;
static const char *loader_error;

static _Noreturn void fail(const char *message) {
    fprintf(stderr, "startup failure: %s\n", message);
    _Exit(1);
}
static uint64_t now(void) {
    struct timespec t;
    if (clock_gettime(CLOCK_MONOTONIC, &t)) fail("host clock");
    return (uint64_t)t.tv_sec * UINT64_C(1000000000) + (uint64_t)t.tv_nsec;
}
static void fault(int number) {
    (void)number;
    const char text[] = "startup: native fault (see last stage and syscall)\n";
    (void)write(2, text, sizeof(text) - 1);
    _Exit(1); // No core dump or platform crash reporter needed by this fixture.
}
static uint64_t unexpected(void) { fail("unexpected external loader/thread interface"); return 0; }
static int target_sdk(void) { return 35; }
static void *missing_netd(const char *name, int flags) {
    // The only absent optional library permitted by this startup test.
    // No Android name is ever passed to the host's dlopen/dlsym.
    if (!name || strcmp(name, "libnetd_client.so") || flags != 2) fail("unexpected optional library");
    ++absent_netd;
    loader_error = "libnetd_client.so is absent from this fixture";
    return NULL;
}
static const char *guest_dlerror(void) {
    const char *error = loader_error;
    loader_error = NULL;
    return error;
}

static artbox_elf_result resolve(void *context, const artbox_dynamic *dynamic, uint32_t index, uint64_t *address) {
    artbox_elf_symbol symbol;
    (void)context;
    if (artbox_dynamic_symbol(dynamic, index, &symbol) != ARTBOX_ELF_OK) return ARTBOX_ELF_INVALID;
    // A bounded, fixed load group: libc first, then its NDK client.
    for (unsigned i = 0; i < 2; ++i) {
        artbox_elf_symbol found;
        if (artbox_dynamic_lookup(&images[i].dynamic, symbol.name, &found) == ARTBOX_ELF_OK) {
            *address = (uintptr_t)images[i].rx + found.value;
            return ARTBOX_ELF_OK;
        }
    }
#define HOST(import_name, entry) if (!strcmp(symbol.name, import_name)) { \
    void (*p)(void) = (void (*)(void))(entry); \
    _Static_assert(sizeof(p) == sizeof(*address), "ARM64 code pointer"); \
    memcpy(address, &p, sizeof(p)); return ARTBOX_ELF_OK; }
    HOST("artbox_bionic_syscall", artbox_bionic_syscall)
    HOST("artbox_bionic_get_tls", artbox_bionic_get_tls)
    HOST("artbox_bionic_set_tls", artbox_bionic_set_tls)
    HOST("android_get_application_target_sdk_version", target_sdk)
    HOST("dlopen", missing_netd)
    HOST("dlerror", guest_dlerror)
    // These bindings deliberately fail the test if called. They do not claim
    // clone, namespace APIs or native thread exit are implemented.
    HOST("__bionic_clone", unexpected)
    HOST("_exit_with_stack_teardown", unexpected)
    HOST("vfork", unexpected)
    HOST("android_dlopen_ext", unexpected)
    HOST("android_get_exported_namespace", unexpected)
    HOST("dlsym", unexpected)
    HOST("dlclose", unexpected)
#undef HOST
    if (symbol.binding != 2) fprintf(stderr, "unresolved: %s\n", symbol.name);
    return ARTBOX_ELF_NOT_FOUND;
}
static const void *entry(module *m, const char *name) {
    artbox_elf_symbol s;
    if (artbox_dynamic_lookup(&m->dynamic, name, &s) != ARTBOX_ELF_OK || s.type != 2 ||
        s.value % 4 || s.value >= m->elf.segments[0].file_size ||
        s.size > m->elf.segments[0].file_size - s.value) fail(name);
    return m->rx + s.value;
}
static void load(module *m, const char *framework, const char *file) {
    FILE *f = fopen(file, "rb");
    long size;
    if (!f || fseek(f, 0, SEEK_END) || (size = ftell(f)) <= 0 || size > 64 * 1024 * 1024 ||
        fseek(f, 0, SEEK_SET)) fail("ELF file");
    m->original = malloc((size_t)size);
    if (!m->original || fread(m->original, 1, (size_t)size, f) != (size_t)size || fclose(f)) fail("ELF read");
    if (artbox_elf_open(m->original, (size_t)size, &m->elf) != ARTBOX_ELF_OK || m->elf.type != 3 ||
        m->elf.segment_count != 2 || m->elf.segments[0].virtual_address || m->elf.segments[0].flags != 5 ||
        m->elf.segments[1].flags != 6 || m->elf.segments[1].memory_size % 16384 || m->elf.has_tls ||
        artbox_dynamic_open(&m->elf, &m->dynamic) != ARTBOX_ELF_OK || m->dynamic.init || m->dynamic.preinit_array.size)
        fail("controlled ELF shape");
    m->handle = dlopen(framework, RTLD_NOW | RTLD_LOCAL);
    if (!m->handle) fail(dlerror());
    m->rx = dlsym(m->handle, "artbox_dynamic_rx");
    m->rw = dlsym(m->handle, "artbox_dynamic_rw");
    if (!m->rx || !m->rw || (uintptr_t)m->rx % 16384 ||
        (uintptr_t)m->rw != (uintptr_t)m->rx + m->elf.segments[1].virtual_address ||
        memcmp(m->rx, m->original, (size_t)m->elf.segments[0].file_size) ||
        memcmp(m->rw, m->original + m->elf.segments[1].file_offset, (size_t)m->elf.segments[1].file_size))
        fail("signed bytes or load bias");
    for (uint64_t i = m->elf.segments[1].file_size; i < m->elf.segments[1].memory_size; ++i)
        if (m->rw[i]) fail("BSS");
    if (artbox_vm_register_readonly(vm, m->rx, (size_t)m->elf.segments[0].file_size) ||
        artbox_vm_register_data(vm, m->rw, (size_t)m->elf.segments[1].memory_size, 3)) fail("borrow image");
}
static int64_t dispatch(void *context, uint64_t n, uint64_t a0, uint64_t a1, uint64_t a2,
                        uint64_t a3, uint64_t a4, uint64_t a5) {
    int64_t value = artbox_kernel_call(context, n, a0, a1, a2, a3, a4, a5);
    if (value == -38) value = artbox_devices_call(devices, context, n, a0, a1, a2, a3);
    if (n == 66 && a0 == 2 && a2 <= 16) {
        // Observe Bionic's fatal diagnostics without pretending writev is
        // implemented: the syscall still returns its original ENOSYS below.
        for (uint64_t i = 0; i < a2; ++i) {
            uint64_t vector[2];
            if (a1 > UINT64_MAX - i * 16 || artbox_vm_read(vm, a1 + i * 16, vector, sizeof(vector))) break;
            char text[1024];
            size_t length = vector[1] > sizeof(text) ? sizeof(text) : (size_t)vector[1];
            if (!artbox_vm_read(vm, vector[0], text, length)) (void)fwrite(text, 1, length, stderr);
        }
    }
    // A bounded diagnostic console, not the future virtual descriptor table.
    if (n == 64 && (a0 == 1 || a0 == 2)) {
        char buffer[1024];
        if (a2 > sizeof(buffer)) value = -22;
        else if (artbox_vm_read(vm, a1, buffer, (size_t)a2)) value = -14;
        else value = fwrite(buffer, 1, (size_t)a2, stderr) == (size_t)a2 ? (int64_t)a2 : -5;
    }
    if (value == -38 && n < 512) ++unsupported[n];
    if (++calls <= 128 || value < 0) fprintf(stderr, "syscall %" PRIu64 "(%" PRIx64 ",%" PRIx64 ",%" PRIx64 ",%" PRIx64 ") = %" PRId64 "\n", n,a0,a1,a2,a3,value);
    if (n == 93 || n == 94) fail("Bionic requested process exit");
    return value;
}
static void construct(module *m) {
    artbox_elf_table *table = &m->dynamic.init_array;
    uint64_t start = m->elf.segments[1].virtual_address, length = m->elf.segments[1].memory_size;
    if (table->size && (table->size % 8 || table->address < start || table->address - start > length ||
        table->size > length - (table->address - start))) fail("constructor table");
    for (uint64_t i = 0; i < table->size; i += 8) {
        uint64_t p;
        memcpy(&p, m->rx + table->address + i, 8);
        if (!p || p == UINT64_MAX) continue; // CRT sentinels.
        if (p % 4 || p < (uintptr_t)m->rx || p - (uintptr_t)m->rx >= m->elf.segments[0].file_size) fail("constructor address");
        fprintf(stderr, "constructor %u ELF+0x%" PRIx64 "\n", constructors, p - (uintptr_t)m->rx);
        artbox_call7((void *)(uintptr_t)p, 0, 0, 0, 0, 0, 0, 0);
        ++constructors;
    }
}
static void *run(void *context) {
    (void)context;
    unsigned char random[16];
    char name[] = "artbox-bionic-startup";
    char process_sampling[] = "GWP_ASAN_PROCESS_SAMPLING=1";
    char allocation_sampling[] = "GWP_ASAN_SAMPLE_RATE=1";
    artbox_system_ops system = artbox_native_system();
    if (system.random(random, sizeof(random))) fail("AT_RANDOM");
    // argc, argv, envp, and the Linux ARM64 auxiliary vector. These live on
    // the mapped guest stack; no Darwin auxiliary-vector or pointer tagging.
    uint64_t auxv[] = {
        6, artbox_vm_page_size(vm), 11, 10000, 12, 10000, 13, 10000, 14, 10000,
        16, 0, 17, 100, 23, 0, 25, (uintptr_t)random, 26, 0, 31, (uintptr_t)name, 0, 0};
    uint64_t args[48] = {1, (uintptr_t)name, 0};
    size_t cursor = 3;
    if (force_sampling) {
        args[cursor++] = (uintptr_t)process_sampling;
        args[cursor++] = (uintptr_t)allocation_sampling;
    }
    args[cursor++] = 0;
    memcpy(args + cursor, auxv, sizeof(auxv));
    const artbox_syscall_binding binding = {dispatch, &thread};
    const artbox_syscall_binding *previous = artbox_native_syscall_swap(&binding);
    void **old_tls = artbox_native_tls_swap(NULL);
    fprintf(stderr, "bootstrap entry\n");
    if (artbox_call7(entry(&images[0], "artbox_bootstrap_main"), (uintptr_t)args, 0, 0, 0, 0, 0, 0)) fail("bootstrap return");
    if (!artbox_bionic_get_tls()) fail("no real Bionic TCB");
    fprintf(stderr, "bootstrap complete; running real libc constructors\n");
    construct(&images[0]);
    construct(&images[1]);
    fprintf(stderr, "NDK allocator client entry\n");
    result = (int32_t)artbox_call7(entry(&images[1], "artbox_startup_check"), 0, 0, 0, 0, 0, 0, 0);
    gwp_enabled = artbox_call7(entry(&images[0], "artbox_bootstrap_gwp_enabled"), 0, 0, 0, 0, 0, 0, 0);
    guarded_samples = artbox_call7(entry(&images[0], "artbox_bootstrap_guarded_samples"), 0, 0, 0, 0, 0, 0, 0);
    if (force_sampling && (!gwp_enabled || !guarded_samples)) fail("GWP-ASan sampling did not run");
    artbox_native_tls_swap(old_tls);
    artbox_native_syscall_swap(previous);
    return NULL;
}
int main(int argc, char **argv) {
    if (argc != 5 && (argc != 6 || strcmp(argv[5], "--sampled"))) return 2;
    force_sampling = argc == 6;
    for (unsigned i = 0; i < 4; ++i) {
        const int signals[] = {SIGSEGV, SIGBUS, SIGILL, SIGABRT};
        struct sigaction action;
        memset(&action, 0, sizeof(action)); action.sa_handler = fault;
        sigemptyset(&action.sa_mask);
        if (sigaction(signals[i], &action, NULL)) fail("fault handler");
    }
    artbox_vm_ops ops = artbox_native_vm();
    artbox_system_ops system = artbox_native_system();
    vm = artbox_vm_create(&ops, UINT64_C(32) << 30, 4096);
    devices = artbox_devices_create(256);
    if (!vm || !devices || artbox_kernel_thread_init(&thread, vm, &system, 10000, 10000)) fail("kernel context");
    uint64_t start = now();
    load(&images[0], argv[1], argv[2]); load(&images[1], argv[3], argv[4]);
    if (images[0].dynamic.needed_count || images[1].dynamic.needed_count != 1 ||
        strcmp(images[1].dynamic.needed[0], "libc.so")) fail("fixture dependency graph");
    for (unsigned i = 0; i < 2; ++i) {
        artbox_relocation_memory memory = {1, images[i].rw, (size_t)images[i].elf.segments[1].memory_size};
        artbox_elf_result status = artbox_relocate(&images[i].dynamic, (uintptr_t)images[i].rx, &memory, 1, resolve, NULL, &images[i].relocations);
        if (status != ARTBOX_ELF_OK) { fprintf(stderr, "relocation result %d\n", status); fail("relocations"); }
    }
    uint64_t loaded = now();
    size_t stack_size = 4 * 1024 * 1024, page = ops.page_size;
    int64_t stack = artbox_vm_mmap(vm, 0, stack_size + 2 * page, 0, 0x22, -1, 0);
    if (stack < 0 || artbox_vm_mprotect(vm, (uint64_t)stack + page, stack_size, 3)) fail("guest stack");
    pthread_attr_t attr;
    pthread_t worker;
    if (pthread_attr_init(&attr) || pthread_attr_setstack(&attr, (void *)(uintptr_t)((uint64_t)stack + page), stack_size) ||
        pthread_create(&worker, &attr, run, NULL) || pthread_attr_destroy(&attr) || pthread_join(worker, NULL)) fail("host execution thread");
    uint64_t finished = now(), reserved = artbox_vm_reserved_bytes(vm);
    if (result != 146 || absent_netd != 1 || !constructors) {
        fprintf(stderr, "client result: %" PRId64 ", absent netd: %u, constructors: %u\n", result, absent_netd, constructors);
        fail("allocator acceptance");
    }
    artbox_devices_destroy(devices);
    if (artbox_vm_destroy(vm)) fail("release reservations");
    printf("{\"cases\":146,\"constructors\":%u,\"absent_netd\":%u,\"syscalls\":%u,\"load_relocate_ns\":%" PRIu64
           ",\"startup_client_ns\":%" PRIu64 ",\"reserved_bytes\":%" PRIu64 ",\"gwp_enabled\":%" PRIu64
           ",\"guarded_samples\":%" PRIu64 ",\"unsupported_syscalls\":{",
           constructors, absent_netd, calls, loaded-start, finished-loaded, reserved, gwp_enabled, guarded_samples);
    unsigned printed = 0;
    for (unsigned i = 0; i < 512; ++i) if (unsupported[i]) printf("%s\"%u\":%u", printed++ ? "," : "", i, unsupported[i]);
    puts("}}");
    for (unsigned i = 0; i < 2; ++i) { dlclose(images[i].handle); free(images[i].original); }
    return 0;
}
