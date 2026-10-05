// Signed Bionic fixture using the portable manifest-scoped load-group engine.
#include "artbox/native_bionic.h"
#include "artbox/native_art.h"
#include "artbox/looper_result.h"
#include "artbox/binder_libc_result.h"
#include "artbox/native_dlfcn.h"
#include "artbox/dynamic.h"
#include "artbox/relocation.h"
#include "artbox/linker.h"
#include "artbox/native_call.h"
#include "artbox/native_tls.h"
#include "artbox/native_syscall.h"
#include "artbox/native_signal_binding.h"
#include "artbox/native_signal_delivery.h"
#include "artbox/native_signal_context.h"
#include "artbox/signals.h"
#include "artbox/native_vm.h"
#include "artbox/native_system.h"
#include "artbox/native_files.h"
#include "artbox/native_wake.h"
#include "artbox/futex.h"
#include "artbox/native_atomic.h"
#include "artbox/native_thread.h"
#include <setjmp.h>
#include <stdatomic.h>
#include <dlfcn.h>
#include <inttypes.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/resource.h>
#include <unistd.h>

typedef struct module {
    unsigned char *original, *rx, *rw;
    void *handle;
    artbox_elf elf;
    artbox_dynamic dynamic;
    artbox_relocation_stats relocations;
} module;
enum { IMAGE_CAPACITY = 15 };
static module images[IMAGE_CAPACITY];
static unsigned image_count;
static const artbox_host *guest_console;
typedef struct native_input {
    const char *const *frameworks;
    const char *const *elfs;
    const char *root;
    unsigned sampled, count, mutation;
} native_input;
static struct artbox_looper_result looper_result;
static unsigned looper_mutation;
static int looper_status;
static uint64_t looper_ns;
static artbox_load_group *load_group;
static artbox_vm *vm;
static artbox_vfs *filesystem;
static artbox_native_files *backing_files;
static int64_t file_cases, mapping_cases, version_result;
static int64_t vm_cases, timeout_cases;
static int64_t proc_cases;
static int64_t art_libc_cases;
static int32_t allocator_pressure_cases;
static int32_t binder_libc_cases, binder_libc_path_control, binder_libc_clock_control;
static int32_t binder_regex_cases, binder_regex_newline_control, binder_regex_capture_control;
static int64_t vfork_cases;
static int64_t libcore_frontend_cases;
static int64_t unlink_cases;
static int64_t cwd_cases;
static int64_t futex_requeue_cases;
static int64_t signal_wait_cases;
static artbox_signals *process_signals;
static artbox_signal_memory_range signal_code[IMAGE_CAPACITY],signal_data[IMAGE_CAPACITY];
static artbox_native_signal_thread signal_template;
static int32_t signal_handler_cases,signal_handler_mutation;
static int32_t signal_stack_cases,signal_stack_handler_cases,signal_stack_mutation;
static uint64_t signal_stack_threads;
static int32_t signal_mask_cases,signal_mask_mutation;
static int32_t signal_fault_cases,signal_fault_edit_mutation,signal_fault_address_mutation;
static int32_t signal_realtime_cases,signal_interrupt_cases,signal_interrupt_mutation;
static uint64_t signal_interrupt_threads;
static uint64_t signal_mask_threads;
static uint64_t file_ns;
static artbox_futex *futex;
static artbox_threads *threads;
static _Thread_local artbox_kernel_thread *current_kernel;
static _Thread_local jmp_buf *exit_boundary;
static _Thread_local artbox_thread_finish *thread_finish;
static int32_t pthread_result;
static uint64_t reaped, pthread_ns, thread_guarded_samples;
static uint64_t tls_queries;
static artbox_kernel_thread thread;
static _Atomic unsigned calls;
static unsigned absent_netd, constructors;
static _Atomic unsigned unsupported[512];
static int64_t result;
static unsigned force_sampling;
static unsigned art_bootstrap;
static unsigned tls_count;
static uint64_t icu_check_ns;
static uint64_t libcore_check_ns, integer128_check_ns;
static uint64_t art_startup_ns, art_managed_bytes, art_window_bytes, art_bootstrap_window_bytes;
static artbox_dlfcn *guest_loader;
static artbox_guest_dlfcn *guest_dl_service;
static uint64_t gwp_enabled, guarded_samples;
static int64_t futex_cases;
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
static char *fault_text(char *out,const char *text) {
    while(*text) *out++=*text++;
    return out;
}
static char *fault_hex(char *out,uint64_t value) {
    static const char digits[]="0123456789abcdef";
    for(unsigned i=0;i<16;++i) *out++=digits[(value>>(60-4*i))&15];
    return out;
}
static void deliver_fault(int number,siginfo_t *info,void *context) {
    artbox_native_signal_thread *thread=artbox_native_signal_thread_context();
    int error=number==SIGUSR1 ? artbox_native_signal_deliver_interrupt(thread,context) :
        artbox_native_signal_deliver_fault(thread,number,info,context);
    if(error) {
        // The owner is terminating. Format fixed-size diagnostics without stdio,
        // allocation, TLS lookup or entering any guest/VM service.
        artbox_arm64_signal_state state={0};
        (void)artbox_native_signal_capture(context,&state);
        char message[256],*at=message;
        at=fault_text(at,"startup fault: number="); at=fault_hex(at,(unsigned)number);
        at=fault_text(at," error="); at=fault_hex(at,(uint64_t)(int64_t)error);
        at=fault_text(at," code="); at=fault_hex(at,(unsigned)info->si_code);
        at=fault_text(at," pc="); at=fault_hex(at,state.pc);
        at=fault_text(at," address="); at=fault_hex(at,(uintptr_t)info->si_addr);
        at=fault_text(at," far="); at=fault_hex(at,state.fault_address);
        at=fault_text(at," esr="); at=fault_hex(at,state.esr);
        for (unsigned i = 0; i < image_count; ++i) {
            if (state.pc >= signal_code[i].address &&
                    state.pc - signal_code[i].address < signal_code[i].size) {
                at=fault_text(at," image="); at=fault_hex(at,i);
                at=fault_text(at," offset="); at=fault_hex(at,state.pc-signal_code[i].address);
                break;
            }
        }
        *at++='\n';
        (void)write(2,message,(size_t)(at-message));
        fault(number);
    }
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

static int64_t host_pthread_clone(const artbox_thread_start *guest_start) {
    artbox_thread_start start;
    if (artbox_vm_read(vm, (uintptr_t)guest_start, &start, sizeof(start))) return -14;
    uint64_t rx = (uintptr_t)images[0].rx;
    if (start.entry < rx || start.entry - rx >= images[0].elf.segments[0].file_size || start.entry % 4)
        return -22;
    return artbox_threads_start(threads, current_kernel, &start);
}
static _Noreturn void finish_thread(uint64_t base, uint64_t size, int error) {
    if (!exit_boundary || !thread_finish) fail("exit outside a guest child thread");
    thread_finish->unmap_address = base;
    thread_finish->unmap_size = size;
    thread_finish->error = error;
    longjmp(*exit_boundary, 1);
}
static _Noreturn void exit_with_stack_teardown(void *base, size_t size) {
    finish_thread((uintptr_t)base, size, 0);
}
static artbox_elf_result resolve(void *context, const artbox_dynamic *dynamic, uint32_t index, uint64_t *address) {
    artbox_elf_symbol symbol;
    (void)context;
    if (artbox_dynamic_symbol(dynamic, index, &symbol) != ARTBOX_ELF_OK) return ARTBOX_ELF_INVALID;
    if (art_bootstrap) {
        artbox_elf_result loader = artbox_native_dlfcn_resolve(NULL, dynamic, index, address);
        if (loader != ARTBOX_ELF_NOT_FOUND) return loader;
    }
#define HOST(import_name, entry) if (!strcmp(symbol.name, import_name)) { \
    void (*p)(void) = (void (*)(void))(entry); \
    _Static_assert(sizeof(p) == sizeof(*address), "ARM64 code pointer"); \
    memcpy(address, &p, sizeof(p)); return ARTBOX_ELF_OK; }
    HOST("artbox_bionic_syscall", artbox_bionic_syscall)
    HOST("artbox_bionic_get_tls", artbox_bionic_get_tls)
    HOST("artbox_bionic_set_tls", artbox_bionic_set_tls)
    if (art_bootstrap && dynamic->soname && !strcmp(dynamic->soname, "libart.so")) {
        HOST("artbox_vm_access", artbox_vm_access)
        HOST("artbox_vm_mmap_window", artbox_vm_mmap_window)
        HOST("artbox_vm_mprotect", artbox_vm_mprotect)
        HOST("artbox_vm_munmap", artbox_vm_munmap)
        HOST("artbox_vm_page_size", artbox_vm_page_size)
        HOST("artbox_vm_reserve_window", artbox_vm_reserve_window)
        HOST("artbox_vm_reserved_bytes", artbox_vm_reserved_bytes)
    }
    HOST("android_get_application_target_sdk_version", target_sdk)
    if (!art_bootstrap) {
        HOST("dlopen", missing_netd)
        HOST("dlerror", guest_dlerror)
        HOST("dlsym", unexpected)
        HOST("dlclose", unexpected)
    }
    HOST("artbox_host_pthread_clone", host_pthread_clone)
    HOST("_exit_with_stack_teardown", exit_with_stack_teardown)
    // Unsupported loader/process interfaces still fail the controlled test.
    HOST("vfork", unexpected)
    HOST("android_dlopen_ext", unexpected)
    HOST("android_get_exported_namespace", unexpected)
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
static artbox_elf_result loader_invoke(void *context, uint64_t address,
                                     const uint64_t args[3], uint64_t *value) {
    (void)context;
    *value = artbox_call7((void *)(uintptr_t)address, args[0], args[1], args[2], 0, 0, 0, 0);
    return ARTBOX_ELF_OK;
}
static artbox_elf_result loader_tls(void *context, uint64_t id, uint64_t *address) {
    (void)context;
    *address = artbox_call7(entry(&images[0], "artbox_bootstrap_tls_data"), id, 0, 0, 0, 0, 0, 0);
    return ARTBOX_ELF_OK;
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
        m->elf.segments[1].flags != 6 || !m->elf.segments[1].memory_size ||
        m->elf.segments[1].memory_size > 64 * 1024 * 1024 ||
        artbox_dynamic_open(&m->elf, &m->dynamic) != ARTBOX_ELF_OK || m->dynamic.init || m->dynamic.preinit_array.size) {
        fprintf(stderr, "rejected startup ELF: %s\n", file);
        fail("controlled ELF shape");
    }
    // The signed wrapper pads RW to 16 KiB; ELF p_memsz itself need not be a
    // page multiple (the verified libm/libdl images have 232/336-byte spans).
    // Check that padding too, then borrow complete pages in the shared mapper.
    const size_t rw_bytes = (size_t)((m->elf.segments[1].memory_size + 16383) & ~UINT64_C(16383));
    m->handle = dlopen(framework, RTLD_NOW | RTLD_LOCAL);
    if (!m->handle) fail(dlerror());
    m->rx = dlsym(m->handle, "artbox_dynamic_rx");
    m->rw = dlsym(m->handle, "artbox_dynamic_rw");
    if (!m->rx || !m->rw || (uintptr_t)m->rx % 16384 ||
        (uintptr_t)m->rw != (uintptr_t)m->rx + m->elf.segments[1].virtual_address ||
        memcmp(m->rx, m->original, (size_t)m->elf.segments[0].file_size) ||
        memcmp(m->rw, m->original + m->elf.segments[1].file_offset, (size_t)m->elf.segments[1].file_size))
        fail("signed bytes or load bias");
    for (uint64_t i = m->elf.segments[1].file_size; i < rw_bytes; ++i)
        if (m->rw[i]) fail("BSS");
    if (artbox_vm_register_readonly(vm, m->rx, (size_t)m->elf.segments[0].file_size) ||
        artbox_vm_register_data(vm, m->rw, rw_bytes, 3)) fail("borrow image");
}
static int64_t dispatch(void *context, uint64_t n, uint64_t a0, uint64_t a1, uint64_t a2,
                        uint64_t a3, uint64_t a4, uint64_t a5) {
    if (n == 93 && exit_boundary) finish_thread(0, 0, a0 ? -5 : 0);
    int64_t value = n == 222 ? artbox_vfs_mmap(filesystem, vm, a0, a1, a2, a3, (int64_t)a4, a5) :
        artbox_kernel_call(context, n, a0, a1, a2, a3, a4, a5);
    if (n == 98) value = artbox_futex_call_interruptible(futex, context, a0, a1, a2, a3, a4, a5);
    if (value == -38) value = artbox_vfs_syscall(filesystem, context, n, a0, a1, a2, a3, a4, a5);
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
        else {
            value = fwrite(buffer, 1, (size_t)a2, stderr) == (size_t)a2 ? (int64_t)a2 : -5;
            if(value >= 0 && a0 == 1 && guest_console)
                guest_console->log(guest_console->context,buffer,(size_t)a2);
        }
    }
    if (value == -38 && n < 512) ++unsupported[n];
    if (++calls <= 128 || value < 0) fprintf(stderr, "syscall %" PRIu64 "(%" PRIx64 ",%" PRIx64 ",%" PRIx64 ",%" PRIx64 ") = %" PRId64 "\n", n,a0,a1,a2,a3,value);
    if (n == 93 || n == 94) fail("Bionic requested process exit");
    return value;
}
static void run_child(void *context, artbox_kernel_thread *kernel, const artbox_thread_start *start,
                      artbox_thread_finish *finish) {
    (void)context;
    current_kernel = kernel;
    const artbox_syscall_binding binding = {dispatch, kernel};
    const artbox_syscall_binding *previous = artbox_native_syscall_swap(&binding);
    void **old_tls = artbox_native_tls_swap((void **)(uintptr_t)start->tls);
    artbox_native_signal_thread signal_thread=signal_template;
    signal_thread.kernel=kernel; signal_thread.stack_address=start->stack_base;
    signal_thread.stack_size=start->stack_size; signal_thread.guest_tls=(void **)(uintptr_t)start->tls;
    if(artbox_native_signal_thread_attach(&signal_thread)) fail("child signal attachment");
    artbox_guest_dl_thread *dl_thread = NULL, *old_dl = NULL;
    if (guest_dl_service) {
        if (artbox_guest_dl_thread_create(guest_dl_service, &dl_thread) != ARTBOX_ELF_OK) fail("child loader state");
        old_dl = artbox_native_dlfcn_swap(dl_thread);
    }
    jmp_buf boundary;
    exit_boundary = &boundary; thread_finish = finish;
    if (!setjmp(boundary)) {
        artbox_call7(entry(&images[0], "artbox_bootstrap_thread"), start->entry, start->argument, 0, 0, 0, 0, 0);
        finish->error = -5; // Bionic __pthread_start must end through guest exit.
    }
    exit_boundary = NULL; thread_finish = NULL; current_kernel = NULL;
    if(artbox_native_signal_thread_detach(&signal_thread)) fail("child signal detach");
    if (dl_thread) {
        artbox_native_dlfcn_swap(old_dl);
        artbox_guest_dl_thread_destroy(dl_thread);
    }
    artbox_native_tls_swap(old_tls);
    artbox_native_syscall_swap(previous);
    // Return normally; the portable reaper joins before clear-TID or unmap.
}
static artbox_elf_result construct(void *context, uint64_t address) {
    (void)context;
    fprintf(stderr, "constructor %u through manifest load group\n", constructors);
    artbox_call7((void *)(uintptr_t)address, 0, 0, 0, 0, 0, 0, 0);
    ++constructors;
    return ARTBOX_ELF_OK;
}
static void check_art_bootstrap(void) {
    // Fixed-word JNI invocation APIs only. JNI variadic method calls belong in
    // an NDK-compiled guest entry, never in the Apple calling convention.
    uint64_t registered = UINT64_MAX;
    int32_t count = -1;
    if ((int32_t)artbox_call7(entry(&images[1], "JNI_GetCreatedJavaVMs"),
            (uintptr_t)&registered, 1, (uintptr_t)&count, 0, 0, 0, 0) || count != 0 || registered != UINT64_MAX)
        fail("ART pre-start VM registration");
    // The pinned ART explicitly returns JNI_ERR for this unsupported API.
    if ((int32_t)artbox_call7(entry(&images[1], "JNI_GetDefaultJavaVMInitArgs"), 0, 0, 0, 0, 0, 0, 0) != -1)
        fail("ART default initialization contract");
    // This pre-start contract needs only a guard and a few addressable pages.
    // Unbinding retains ownership until VM destruction, so do not spend the
    // full compressed-reference range on this preliminary check.
    const size_t page = artbox_vm_page_size(vm);
    art_bootstrap_window_bytes = 4 * page;
    int32_t binding = (int32_t)artbox_call7(entry(&images[1], "artbox_art_heap_initialize"),
            (uintptr_t)vm, art_bootstrap_window_bytes, page, 0, 0, 0, 0);
    if (binding) {
        fprintf(stderr, "ART bootstrap window: bytes=%" PRIu64 " error=%d\n", art_bootstrap_window_bytes, binding);
        fail("ART shared heap binding");
    }
    if (artbox_call7(entry(&images[1], "artbox_art_reference_compress"), 0, 0, 0, 0, 0, 0, 0) ||
        artbox_call7(entry(&images[1], "artbox_art_reference_decompress"), 0, 0, 0, 0, 0, 0, 0))
        fail("ART null reference contract");
    artbox_call7(entry(&images[1], "artbox_art_heap_unbind"), 0, 0, 0, 0, 0, 0, 0);
    const void *sigchain_check=entry(&images[1],"artbox_sigchain_check");
    uint64_t chain_action=(uintptr_t)entry(&images[1],"sigaction");
    uint64_t chain_mask=(uintptr_t)entry(&images[1],"sigprocmask");
    int positive=(int32_t)artbox_call7(sigchain_check,chain_action,chain_mask,0,0,0,0,0);
    if(positive!=22) {
        fprintf(stderr,"ART sigchain result: %d\n",positive);
        const void *detail=entry(&images[1],"artbox_sigchain_detail");
        for(unsigned i=0;i<25;++i)
            fprintf(stderr,"ART sigchain detail[%u]=0x%" PRIx64 "\n",i,
                artbox_call7(detail,i,0,0,0,0,0,0));
        fail("ART sigchain registration and forwarding");
    }
    int negative=(int32_t)artbox_call7(sigchain_check,chain_action,chain_mask,1,0,0,0,0);
    if(negative!=-1005) {
        fprintf(stderr,"ART sigchain mutation result: %d\n",negative);
        fail("ART sigchain missing-handler control");
    }
    result = 0;
}
static void *run(void *context) {
    (void)context;
    unsigned char random[16];
    char name[] = "artbox-bionic-startup";
    if (artbox_vfs_set_commandline(filesystem, name, sizeof(name))) fail("initial argv snapshot");
    char process_sampling[] = "GWP_ASAN_PROCESS_SAMPLING=1";
    char allocation_sampling[] = "GWP_ASAN_SAMPLE_RATE=1";
    char guarded_capacity[] = "GWP_ASAN_MAX_ALLOCS=32";
    char android_data[] = "ANDROID_DATA=/data";
    char android_i18n[] = "ANDROID_I18N_ROOT=/system/i18n";
    char android_tzdata[] = "ANDROID_TZDATA_ROOT=/system/tzdata";
    char android_root[] = "ANDROID_ROOT=/system";
    char android_art[] = "ANDROID_ART_ROOT=/system/art";
    char system_ext[] = "SYSTEM_EXT_ROOT=/system_ext";
    artbox_system_ops system = artbox_native_system();
    if (system.random(random, sizeof(random))) fail("AT_RANDOM");
    // argc, argv, envp, and the Linux ARM64 auxiliary vector. These live on
    // the mapped guest stack; no Darwin auxiliary-vector or pointer tagging.
    uint64_t auxv[] = {
        6, artbox_vm_page_size(vm), 11, 10000, 12, 10000, 13, 10000, 14, 10000,
        16, 0, 17, 100, 23, 0, 25, (uintptr_t)random, 26, 0, 31, (uintptr_t)name,
        51, ARTBOX_SIGNAL_STACK_MINIMUM, 0, 0}; // AT_MINSIGSTKSZ includes the native bridge budget.
    uint64_t args[48] = {1, (uintptr_t)name, 0};
    size_t cursor = 3;
    if (force_sampling) {
        args[cursor++] = (uintptr_t)process_sampling;
        args[cursor++] = (uintptr_t)allocation_sampling;
        args[cursor++] = (uintptr_t)guarded_capacity;
    }
    if (art_bootstrap >= 2) {
        args[cursor++] = (uintptr_t)android_data;
        args[cursor++] = (uintptr_t)android_i18n;
        args[cursor++] = (uintptr_t)android_tzdata;
    }
    if (art_bootstrap == 4) {
        args[cursor++] = (uintptr_t)android_root;
        args[cursor++] = (uintptr_t)android_art;
        args[cursor++] = (uintptr_t)system_ext;
    }
    args[cursor++] = 0;
    if (sizeof(auxv)/sizeof(auxv[0]) > sizeof(args)/sizeof(args[0])-cursor) fail("startup argument capacity");
    memcpy(args + cursor, auxv, sizeof(auxv));
    current_kernel = &thread;
    const artbox_syscall_binding binding = {dispatch, &thread};
    const artbox_syscall_binding *previous = artbox_native_syscall_swap(&binding);
    void **old_tls = artbox_native_tls_swap(NULL);
    artbox_guest_dl_thread *dl_thread = NULL, *old_dl = NULL;
    if (guest_dl_service) {
        if (artbox_guest_dl_thread_create(guest_dl_service, &dl_thread) != ARTBOX_ELF_OK) fail("primary loader state");
        old_dl = artbox_native_dlfcn_swap(dl_thread);
    }
    if (artbox_call7(entry(&images[0], "artbox_bootstrap_tls_data"), 1, 0, 0, 0, 0, 0, 0))
        fail("TLS query before bootstrap");
    fprintf(stderr, "bootstrap entry\n");
    artbox_tls_template templates[64];
    tls_count = artbox_load_group_tls_count(load_group);
    if (!tls_count || tls_count > 64 || (!art_bootstrap && tls_count != 2)) fail("ELF TLS template count");
    for (unsigned i = 0; i < tls_count; ++i)
        if (artbox_load_group_tls_template(load_group, i, &templates[i]) != ARTBOX_ELF_OK) fail("ELF TLS template");
    if (artbox_call7(entry(&images[0], "artbox_bootstrap_main"), (uintptr_t)args,
                    (uintptr_t)templates, tls_count, 0, 0, 0, 0)) fail("bootstrap return");
    if (!artbox_bionic_get_tls()) fail("no real Bionic TCB");
    if(artbox_call7(entry(&images[0],"getauxval"),51,0,0,0,0,0,0)!=ARTBOX_SIGNAL_STACK_MINIMUM)
        fail("guest alternate-stack minimum auxiliary value");
    artbox_signal_stack bionic_stack;
    if(artbox_signals_stack_snapshot(&thread,0,&bionic_stack) || bionic_stack.flags || bionic_stack.size<ARTBOX_SIGNAL_STACK_MINIMUM)
        fail("Bionic main-thread alternate-stack initialization");
    artbox_native_signal_thread signal_thread=signal_template;
    signal_thread.kernel=&thread; signal_thread.guest_tls=artbox_bionic_get_tls();
    if(artbox_native_signal_thread_attach(&signal_thread)) fail("primary signal attachment");
    fprintf(stderr, "bootstrap complete; running real libc constructors\n");
    if (artbox_load_group_initialize(load_group, construct, NULL) != ARTBOX_ELF_OK) fail("load-group constructors");
    unsigned initialized = constructors;
    if (artbox_load_group_initialize(load_group, construct, NULL) != ARTBOX_ELF_OK || constructors != initialized)
        fail("constructor idempotence");
    if (art_bootstrap) {
        check_art_bootstrap();
        if (art_bootstrap == 2) {
            uint64_t started = now();
            if ((int32_t)artbox_call7(entry(&images[9], "artbox_native_icu_check"),
                    0, 0, 0, 0, 0, 0, 0)) fail("native ICU dependency checks");
            icu_check_ns = now() - started;
        }
        if (art_bootstrap == 3) {
            uint64_t started = now();
            if ((int32_t)artbox_call7(entry(&images[14], "artbox_uint128_check"),
                    0, 0, 0, 0, 0, 0, 0) != 228) fail("unsigned 128-bit division oracle");
            integer128_check_ns = now() - started;
            started = now();
            if ((int32_t)artbox_call7(entry(&images[14], "artbox_native_libcore_check"),
                    0, 0, 0, 0, 0, 0, 0)) fail("native libcore dependency checks");
            libcore_check_ns = now() - started;
            if (artbox_threads_drain(threads, 5000)) fail("libcore child thread reaper");
            reaped = artbox_threads_reaped(threads);
            if (reaped != 1) fail("libcore monitor worker count");
        }
        if (art_bootstrap == 4) {
            uint64_t metrics[3] = {0, 0, 0};
            fprintf(stderr, "signed ART runtime acceptance entry\n");
            int status = (int32_t)artbox_call7(entry(&images[1], "artbox_native_runtime_check"),
                (uintptr_t)vm, artbox_vm_page_size(vm), (uintptr_t)metrics, 0, 0, 0, 0);
            if (status || !metrics[0] || !metrics[1] || metrics[2] != (UINT64_C(512) << 20)) {
                fprintf(stderr, "signed ART runtime result: %d\n", status);
                fail("ART JavaVM, DEX and lifecycle acceptance");
            }
            art_startup_ns = metrics[0]; art_managed_bytes = metrics[1]; art_window_bytes = metrics[2];
            if (artbox_threads_drain(threads, 5000)) fail("ART child thread reaper");
            reaped = artbox_threads_reaped(threads);
        }
        if (art_bootstrap == 5) {
            uint64_t started = now();
            looper_status = (int32_t)artbox_call7(entry(&images[4], "artbox_native_looper_check"),
                looper_mutation, (uintptr_t)&looper_result, 0, 0, 0, 0, 0);
            looper_ns = now() - started;
            fprintf(stderr, "signed Looper result: %d, cases: %u, failure: %u\n",
                looper_status, looper_result.cases, looper_result.failure);
            if (artbox_threads_drain(threads, 5000)) fail("Looper child thread reaper");
            reaped = artbox_threads_reaped(threads);
            if (reaped != 1) fail("Looper worker count");
        }
        artbox_native_dlfcn_swap(old_dl);
        artbox_guest_dl_thread_destroy(dl_thread);
        if(artbox_native_signal_thread_detach(&signal_thread)) fail("ART primary signal detach");
        artbox_native_tls_swap(old_tls);
        artbox_native_syscall_swap(previous);
        current_kernel = NULL;
        return NULL;
    }
    for (unsigned i = 0; i < 2; ++i)
        if (artbox_call7(entry(&images[1], "artbox_tls_abi_check"), 0, 0, 0, 0, 0, 0, 0)) fail("TLSDESC register preservation");
    version_result = (int64_t)artbox_call7(entry(&images[1], "version_client"), 0, 0, 0, 0, 0, 0, 0);
    if (version_result != 46) fail("versioned dependency execution");
    fprintf(stderr, "NDK allocator client entry\n");
    result = (int32_t)artbox_call7(entry(&images[1], "artbox_startup_check"), 0, 0, 0, 0, 0, 0, 0);
    allocator_pressure_cases = (int32_t)artbox_call7(entry(&images[1], "artbox_allocator_pressure_check"), 0, 0, 0, 0, 0, 0, 0);
    if (allocator_pressure_cases != 3) fail("allocator region exhaustion and secondary fallback");
    art_libc_cases = (int32_t)artbox_call7(entry(&images[1], "artbox_art_bionic_check"), 0, 0, 0, 0, 0, 0, 0);
    if (art_libc_cases != 73) {
        fprintf(stderr, "ART libc caller: %" PRId64 "\n", art_libc_cases);
        fail("ART libc dependency acceptance");
    }
    const void *binder_libc = entry(&images[1], "artbox_binder_libc_check");
    binder_libc_cases = (int32_t)artbox_call7(binder_libc, 0, 0, 0, 0, 0, 0, 0);
    binder_libc_path_control = (int32_t)artbox_call7(binder_libc, 1, 0, 0, 0, 0, 0, 0);
    binder_libc_clock_control = (int32_t)artbox_call7(binder_libc, 2, 0, 0, 0, 0, 0, 0);
    if (binder_libc_cases != ARTBOX_BINDER_LIBC_CASES ||
        binder_libc_path_control != ARTBOX_BINDER_LIBC_PATH_CONTROL ||
        binder_libc_clock_control != ARTBOX_BINDER_LIBC_CLOCK_CONTROL) {
        fprintf(stderr, "Binder libc caller: %d, controls %d/%d\n", binder_libc_cases,
                binder_libc_path_control, binder_libc_clock_control);
        fail("Binder libc dependency acceptance");
    }
    const void *binder_regex = entry(&images[1], "artbox_binder_regex_check");
    binder_regex_cases = (int32_t)artbox_call7(binder_regex, 0, 0, 0, 0, 0, 0, 0);
    binder_regex_newline_control = (int32_t)artbox_call7(binder_regex, 1, 0, 0, 0, 0, 0, 0);
    binder_regex_capture_control = (int32_t)artbox_call7(binder_regex, 2, 0, 0, 0, 0, 0, 0);
    if (binder_regex_cases != ARTBOX_BINDER_REGEX_CASES ||
        binder_regex_newline_control != ARTBOX_BINDER_REGEX_NEWLINE_CONTROL ||
        binder_regex_capture_control != ARTBOX_BINDER_REGEX_CAPTURE_CONTROL) {
        fprintf(stderr, "Binder regex caller: %d, controls %d/%d\n", binder_regex_cases,
                binder_regex_newline_control, binder_regex_capture_control);
        fail("Binder regex dependency acceptance");
    }
    vfork_cases = (int32_t)artbox_call7(entry(&images[1], "artbox_vfork_check"), 0, 0, 0, 0, 0, 0, 0);
    if (vfork_cases != 28) {
        fprintf(stderr, "vfork capture caller: %" PRId64 "\n", vfork_cases);
        fail("Bionic vfork register/state acceptance");
    }
    int32_t vfork_rejection = (int32_t)artbox_call7(entry(&images[1], "artbox_vfork_rejection_check"), 0, 0, 0, 0, 0, 0, 0);
    if (vfork_rejection != 2) {
        fprintf(stderr, "vfork rejection caller: %d\n", vfork_rejection);
        fail("Bionic vfork unsupported-process contract");
    }
    vfork_cases += vfork_rejection;
    libcore_frontend_cases = (int32_t)artbox_call7(entry(&images[1], "artbox_libcore_frontend_common"), 0, 0, 0, 0, 0, 0, 0);
    if (libcore_frontend_cases != 45) {
        fprintf(stderr, "libcore libc frontend caller: %" PRId64 "\n", libcore_frontend_cases);
        fail("Numeric resolver and string acceptance");
    }
    int32_t account_cases = (int32_t)artbox_call7(entry(&images[1], "artbox_libcore_frontend_accounts"), 0, 0, 0, 0, 0, 0, 0);
    if (account_cases != 30) {
        fprintf(stderr, "Android account caller: %d\n", account_cases);
        fail("Generated Android account table acceptance");
    }
    libcore_frontend_cases += account_cases;
    unlink_cases = (int64_t)artbox_call7(entry(&images[1], "artbox_files_unlink_check"),
                                      artbox_vm_page_size(vm), 0, 0, 0, 0, 0, 0);
    if (unlink_cases != 29) {
        fprintf(stderr, "unlink caller: %" PRId64 "\n", unlink_cases);
        fail("unlink and descriptor lifetime checks");
    }
    cwd_cases=(int64_t)artbox_call7(entry(&images[1],"artbox_files_cwd_check"),artbox_vm_page_size(vm),0,0,0,0,0,0);
    if(cwd_cases!=22) {
        fprintf(stderr,"getcwd caller: %" PRId64 "\n",cwd_cases);
        fail("virtual current directory contract");
    }
    signal_wait_cases = (int64_t)artbox_call7(entry(&images[1], "artbox_signal_wait_check"),
        artbox_vm_page_size(vm), 10000, 10000, 10000, 0, 0, 0);
    if (signal_wait_cases != 33) {
        fprintf(stderr, "signal wait caller: %" PRId64 "\n", signal_wait_cases);
        fail("blocked signal wait contract");
    }
    signal_handler_cases=(int32_t)artbox_call7(entry(&images[1],"artbox_signal_handler_check"),0,0,0,0,0,0,0);
    signal_handler_mutation=(int32_t)artbox_call7(entry(&images[1],"artbox_signal_handler_check"),1,0,0,0,0,0,0);
    if(signal_handler_cases!=16 || signal_handler_mutation!=-1000) {
        fprintf(stderr,"signal handler caller: %d mutation: %d\n",signal_handler_cases,signal_handler_mutation);
        fail("signed Android handler delivery");
    }
    signal_fault_cases=(int32_t)artbox_call7(entry(&images[1],"artbox_signal_fault_check"),0,0,0,0,0,0,0);
    signal_fault_edit_mutation=(int32_t)artbox_call7(entry(&images[1],"artbox_signal_fault_check"),1,0,0,0,0,0,0);
    signal_fault_address_mutation=(int32_t)artbox_call7(entry(&images[1],"artbox_signal_fault_check"),2,0,0,0,0,0,0);
    if(signal_fault_cases!=5 || signal_fault_edit_mutation!=-1006 || signal_fault_address_mutation!=-1007) {
        fprintf(stderr,"signal fault caller: %d edit: %d address: %d\n",signal_fault_cases,
            signal_fault_edit_mutation,signal_fault_address_mutation);
        fail("signal fault contract");
    }
    signal_stack_cases=(int32_t)artbox_call7(entry(&images[1],"artbox_signal_stack_check"),artbox_vm_page_size(vm),0,0,0,0,0,0);
    signal_stack_handler_cases=(int32_t)artbox_call7(entry(&images[1],"artbox_signal_stack_handler_check"),0,signal_thread.stack_address,signal_thread.stack_size,0,0,0,0);
    signal_stack_mutation=(int32_t)artbox_call7(entry(&images[1],"artbox_signal_stack_handler_check"),1,signal_thread.stack_address,signal_thread.stack_size,0,0,0,0);
    if(signal_stack_cases!=17 || signal_stack_handler_cases!=24 || signal_stack_mutation!=-1001) {
        fprintf(stderr,"signal stack caller: %d handler: %d mutation: %d\n",signal_stack_cases,signal_stack_handler_cases,signal_stack_mutation);
        fail("signed Android alternate-stack delivery");
    }
    if(artbox_threads_drain(threads,5000) || (signal_stack_threads=artbox_threads_reaped(threads))!=1)
        fail("alternate-stack worker cleanup");
    signal_mask_cases=(int32_t)artbox_call7(entry(&images[1],"artbox_signal_mask_handler_check"),0,0,0,0,0,0,0);
    signal_mask_mutation=(int32_t)artbox_call7(entry(&images[1],"artbox_signal_mask_handler_check"),1,0,0,0,0,0,0);
    if(signal_mask_cases!=18 || signal_mask_mutation!=-1004) {
        fprintf(stderr,"signal mask caller: %d mutation: %d\n",signal_mask_cases,signal_mask_mutation);
        fail("signed Android handler masks");
    }
    if(artbox_threads_drain(threads,5000) || (signal_mask_threads=artbox_threads_reaped(threads)-signal_stack_threads)!=2)
        fail("handler-mask worker cleanup");
    signal_realtime_cases=(int32_t)artbox_call7(entry(&images[1],"artbox_signal_realtime_check"),
        artbox_vm_page_size(vm),10000,10000,10000,0,0,0);
    signal_interrupt_cases=(int32_t)artbox_call7(entry(&images[1],"artbox_signal_interrupt_check"),0,0,0,0,0,0,0);
    signal_interrupt_mutation=(int32_t)artbox_call7(entry(&images[1],"artbox_signal_interrupt_check"),1,0,0,0,0,0,0);
    if(signal_realtime_cases!=26 || signal_interrupt_cases!=4 || signal_interrupt_mutation!=-1008) {
        fprintf(stderr,"signal interruption caller: queue=%d handler=%d mutation=%d\n",signal_realtime_cases,
            signal_interrupt_cases,signal_interrupt_mutation);
        fail("signed Android realtime interruption");
    }
    if(artbox_threads_drain(threads,5000) ||
        (signal_interrupt_threads=artbox_threads_reaped(threads)-signal_stack_threads-signal_mask_threads)!=1)
        fail("interruption worker cleanup");
    int64_t scratch = artbox_vm_mmap(vm, 0, artbox_vm_page_size(vm), 3, 0x22, -1, 0);
    if (scratch < 0) fail("futex fixture storage");
    futex_cases = (int64_t)artbox_call7(entry(&images[1], "artbox_futex_check"), (uint64_t)scratch, 0, 0, 0, 0, 0, 0);
    futex_requeue_cases=(int64_t)artbox_call7(entry(&images[1],"artbox_futex_requeue_check"),(uint64_t)scratch,0,0,0,0,0,0);
    if (artbox_vm_munmap(vm, (uint64_t)scratch, artbox_vm_page_size(vm))) fail("futex fixture cleanup");
    if (futex_cases != 19) { fprintf(stderr, "futex caller: %" PRId64 "\n", futex_cases); fail("futex caller"); }
    if(futex_requeue_cases!=18) { fprintf(stderr,"futex requeue caller: %" PRId64 "\n",futex_requeue_cases); fail("futex requeue caller"); }
    uint64_t before_vm = artbox_vm_reserved_bytes(vm);
    vm_cases = (int64_t)artbox_call7(entry(&images[1], "artbox_vm_check"), artbox_vm_page_size(vm), 0, 0, 0, 0, 0, 0);
    if (vm_cases != 35 || artbox_vm_reserved_bytes(vm) != before_vm) {
        fprintf(stderr, "anonymous memory caller: %" PRId64 "\n", vm_cases); fail("anonymous memory acceptance");
    }
    timeout_cases = (int32_t)artbox_call7(entry(&images[1], "artbox_timeout_check"), 0, 0, 0, 0, 0, 0, 0);
    if (timeout_cases != 18) { fprintf(stderr, "timeout caller: %" PRId64 "\n", timeout_cases); fail("pthread timeout acceptance"); }
    proc_cases = (int64_t)artbox_call7(entry(&images[1], "artbox_proc_check"), (uintptr_t)name, sizeof(name), artbox_vm_page_size(vm), 0, 0, 0, 0);
    if (proc_cases != 22) { fprintf(stderr, "proc caller: %" PRId64 "\n", proc_cases); fail("proc acceptance"); }
    fprintf(stderr, "NDK file client entry\n");
    uint64_t file_start = now();
    file_cases = (int64_t)artbox_call7(entry(&images[1], "artbox_files_check"), artbox_vm_page_size(vm), 0, 0, 0, 0, 0, 0);
    uint64_t before_maps = artbox_vm_reserved_bytes(vm);
    mapping_cases = (int64_t)artbox_call7(entry(&images[1], "artbox_file_mapping_check"), artbox_vm_page_size(vm), 0, 0, 0, 0, 0, 0);
    if (mapping_cases != 43) { fprintf(stderr, "mapping caller: %" PRId64 "\n", mapping_cases); fail("file mapping acceptance"); }
    if (artbox_vm_reserved_bytes(vm) != before_maps) fail("file mapping cleanup");
    file_ns = now() - file_start;
    if (file_cases != 41) { fprintf(stderr, "file caller: %" PRId64 "\n", file_cases); fail("file acceptance"); }
    fprintf(stderr, "NDK pthread client entry\n");
    uint64_t thread_start = now();
    pthread_result = (int32_t)artbox_call7(entry(&images[1], "artbox_pthread_check"), force_sampling, 0, 0, 0, 0, 0, 0);
    if (pthread_result) { fprintf(stderr, "pthread client: %d\n", pthread_result); fail("pthread acceptance"); }
    if (artbox_threads_drain(threads, 5000)) fail("child thread reaper");
    pthread_ns = now() - thread_start;
    thread_guarded_samples = artbox_call7(entry(&images[1], "artbox_pthread_guarded_samples"), 0, 0, 0, 0, 0, 0, 0);
    tls_queries = artbox_call7(entry(&images[1], "artbox_pthread_tls_queries"), 0, 0, 0, 0, 0, 0, 0);
    if (tls_queries != 14) fail("existing static TLS queries");
    reaped = artbox_threads_reaped(threads)-signal_stack_threads-signal_mask_threads-signal_interrupt_threads;
    if (reaped != 6) fail("child thread count");
    gwp_enabled = artbox_call7(entry(&images[0], "artbox_bootstrap_gwp_enabled"), 0, 0, 0, 0, 0, 0, 0);
    guarded_samples = artbox_call7(entry(&images[0], "artbox_bootstrap_guarded_samples"), 0, 0, 0, 0, 0, 0, 0);
    if (force_sampling && (!gwp_enabled || !guarded_samples)) fail("GWP-ASan sampling did not run");
    if(artbox_native_signal_thread_detach(&signal_thread)) fail("primary signal detach");
    artbox_native_tls_swap(old_tls);
    artbox_native_syscall_swap(previous);
    current_kernel = NULL;
    return NULL;
}
static int run_native(const native_input *input, const artbox_host *host, unsigned art_mode,
                      const artbox_host *console) {
    static atomic_flag used = ATOMIC_FLAG_INIT;
    if (!input || !input->root || !host || !host->log || input->sampled > 1) return -22;
    if(console && !console->log) return -22;
    if (art_mode > 5 || input->count != (art_mode == 5 ? 5u : art_mode >= 3 ? 15u : art_mode == 2 ? 10u : 4u)) return -22;
    if (input->mutation > (art_mode == 5 ? 2u : 0u)) return -22;
    for (unsigned i = 0; i < input->count; ++i)
        if (!input->frameworks[i] || !input->elfs[i]) return -22;
    if (atomic_flag_test_and_set(&used)) return -114;
    guest_console = console;
    art_bootstrap = art_mode;
    looper_mutation = input->mutation;
    image_count = input->count;
    force_sampling = input->sampled;
    for (unsigned i = 0; i < 4; ++i) {
        const int signals[] = {SIGSEGV, SIGBUS, SIGILL, SIGABRT};
        struct sigaction action;
        memset(&action, 0, sizeof(action)); action.sa_handler = fault;
        sigemptyset(&action.sa_mask);
        if (sigaction(signals[i], &action, NULL)) fail("fault handler");
    }
    artbox_vm_ops ops = artbox_native_vm();
    artbox_system_ops system = artbox_native_system();
    // Exercise all M2 cases within a bounded address budget on Mac as well as iOS.
    // The previous 8.25 GiB Scudo reservation must fail this contract everywhere.
    // ART also rejects the old 4 GiB and 1 GiB managed window reservations.
    const uint64_t vm_budget = (art_mode ? UINT64_C(1536) : UINT64_C(1024)) << 20;
    vm = artbox_vm_create(&ops, vm_budget, 4096);
    if (artbox_native_files_open(input->root, &backing_files)) fail("rooted filesystem");
    artbox_file_ops files = artbox_native_files_ops(backing_files);
    filesystem = artbox_vfs_create(&files, 256);
    artbox_atomic_u32_ops atomic = artbox_native_atomic_u32();
    futex = artbox_futex_create(vm, &atomic, &system, 4096);
    if (!vm || !filesystem || !futex || artbox_kernel_thread_init(&thread, vm, &system, 10000, 10000)) fail("kernel context");
    artbox_wake_ops wake = artbox_native_wake();
    if (artbox_vfs_set_epoll(filesystem, &wake, 1024, 128)) fail("epoll context");
    process_signals = artbox_signals_create(vm, 10000, 10000, 65);
    if (!process_signals) fail("signal process context");
    artbox_thread_ops native_threads = artbox_native_threads();
    threads = artbox_threads_create(vm, futex, &atomic, &system, &native_threads, 10000, 10001, 64, run_child, NULL);
    if (!threads) fail("native thread manager");
    uint64_t start = now();
    for (unsigned i = 0; i < image_count; ++i) load(&images[i], input->frameworks[i], input->elfs[i]);
    for(unsigned i=0;i<image_count;++i) {
        signal_code[i]=(artbox_signal_memory_range){(uintptr_t)images[i].rx,images[i].elf.segments[0].file_size};
        signal_data[i]=(artbox_signal_memory_range){(uintptr_t)images[i].rw,images[i].elf.segments[1].memory_size};
    }
    signal_template.code=signal_code; signal_template.code_count=image_count;
    signal_template.data=signal_data; signal_template.data_count=image_count;
    signal_template.interrupt_signal=34;
    if(artbox_signals_enable_interrupt(process_signals,34,256) ||
        artbox_signals_enable_actions(process_signals,4096,artbox_native_signal_validate_action,&signal_template,UINT64_C(0x18000004)) ||
        artbox_signals_enable_stacks(process_signals,ARTBOX_SIGNAL_STACK_MINIMUM,4096) ||
        artbox_signals_attach(process_signals,&thread)) fail("signal action owner");
    signal_template.actions=artbox_signals_action_table(process_signals);
    artbox_relocation_memory memory[IMAGE_CAPACITY]; artbox_link_module modules[IMAGE_CAPACITY];
    for (unsigned i = 0; i < image_count; ++i) {
        memory[i] = (artbox_relocation_memory){1, images[i].rw, (size_t)images[i].elf.segments[1].memory_size};
        modules[i] = (artbox_link_module){images[i].dynamic.soname, &images[i].dynamic, (uintptr_t)images[i].rx, &memory[i], 1};
    }
    artbox_elf_result linked = artbox_load_group_create(modules, image_count,
        art_bootstrap == 5 ? "libartbox_looper_check.so" : art_bootstrap >= 3 ? "libartbox_libcore_check.so" :
        art_bootstrap == 2 ? "libartbox_icu_check.so" : art_bootstrap ? "libart.so" : "libstartup_client.so",
        resolve, NULL, &load_group);
    if (linked == ARTBOX_ELF_OK) linked = artbox_load_group_tls_resolver(load_group, (uintptr_t)entry(&images[0], "artbox_tlsdesc_absolute"));
    if (linked == ARTBOX_ELF_OK) linked = artbox_load_group_relocate(load_group);
    if (linked != ARTBOX_ELF_OK) { fprintf(stderr, "load group result %d\n", linked); fail("manifest load group"); }
    if (art_bootstrap) {
        const char *names[] = {"libc.so", "libart.so", "libm.so", "libdl.so", "libnativehelper.so",
            "libicuuc.so", "libicui18n.so", "libicu.so", "libicu_jni.so", "libexpat.so", "libandroidio.so",
            "libopenjdkjvm.so", "libjavacore.so", "libopenjdk.so", "libartbox_libcore_check.so"};
        if (art_bootstrap == 2) names[9] = "libartbox_icu_check.so";
        if (art_bootstrap == 5) names[4] = "libartbox_looper_check.so";
        for (unsigned i = 0; i < image_count; ++i)
            if (!modules[i].name || strcmp(modules[i].name, names[i])) fail("ART bootstrap image order");
        const artbox_guest_dl_ops loader_ops = {NULL, loader_invoke, loader_tls};
        if (artbox_dlfcn_create(load_group, NULL, 0, &guest_loader) != ARTBOX_ELF_OK ||
            artbox_guest_dlfcn_create(guest_loader, load_group, vm, &loader_ops, &guest_dl_service) != ARTBOX_ELF_OK)
            fail("ART guest loader service");
    }
    for (unsigned i = 0; i < image_count; ++i)
        if (artbox_load_group_stats(load_group, modules[i].name, &images[i].relocations) != ARTBOX_ELF_OK) fail("relocation statistics");
    uint64_t loaded = now();
    size_t stack_size = 4 * 1024 * 1024, page = ops.page_size;
    int64_t stack = artbox_vm_mmap(vm, 0, stack_size + 2 * page, 0, 0x22, -1, 0);
    if (stack < 0 || artbox_vm_mprotect(vm, (uint64_t)stack + page, stack_size, 3)) fail("guest stack");
    signal_template.stack_address=(uint64_t)stack+page; signal_template.stack_size=stack_size;
    const int host_faults[]={SIGTRAP,SIGSEGV,SIGBUS,SIGILL,SIGUSR1};
    struct sigaction trap={0},saved_faults[5];
    trap.sa_sigaction=deliver_fault; trap.sa_flags=SA_SIGINFO|SA_RESTART|SA_ONSTACK;
    if(sigemptyset(&trap.sa_mask)) fail("host fault mask");
    if(sigaddset(&trap.sa_mask,SIGUSR1)) fail("host interruption mask");
    for(unsigned i=0;i<5;++i) {
        if(i==4) trap.sa_flags=SA_SIGINFO|SA_ONSTACK;
        if(sigaction(host_faults[i],&trap,&saved_faults[i])) fail("host fault disposition");
    }
    pthread_attr_t attr;
    pthread_t worker;
    if (pthread_attr_init(&attr) || pthread_attr_setstack(&attr, (void *)(uintptr_t)((uint64_t)stack + page), stack_size) ||
        pthread_create(&worker, &attr, run, NULL) || pthread_attr_destroy(&attr) || pthread_join(worker, NULL)) fail("host execution thread");
    uint64_t finished = now(), reserved = artbox_vm_reserved_bytes(vm);
    if (!art_bootstrap && (result != 146 || absent_netd != 1 || !constructors)) {
        fprintf(stderr, "client result: %" PRId64 ", absent netd: %u, constructors: %u\n", result, absent_netd, constructors);
        fail("allocator acceptance");
    }
    struct rusage usage;
    if (getrusage(RUSAGE_SELF, &usage) || usage.ru_maxrss <= 0) fail("native resident-memory measurement");
    if (artbox_threads_destroy(threads)) fail("thread manager cleanup");
    for(unsigned i=0;i<5;++i)
        if(sigaction(host_faults[i],&saved_faults[i],NULL)) fail("restore host fault disposition");
    if (artbox_signals_thread_count(process_signals) != 1 || artbox_signals_waiter_count(process_signals) ||
        artbox_signals_detach(&thread) || artbox_signals_destroy(process_signals)) fail("signal process cleanup");
    artbox_guest_dlfcn_destroy(guest_dl_service);
    artbox_dlfcn_destroy(guest_loader);
    if (artbox_vfs_destroy(filesystem) || artbox_native_files_close(backing_files)) fail("filesystem cleanup");
    if (artbox_futex_destroy(futex)) fail("futex cleanup");
    if (artbox_vm_destroy(vm)) fail("release reservations");
    if (artbox_load_group_count(load_group) != image_count) fail("reachable load-group size");
    artbox_load_group_destroy(load_group);
    char report[16384];
    int length;
    if (art_bootstrap) {
        if (result || !constructors || artbox_bionic_get_tls()) fail("ART bootstrap completion");
        char extra[768] = "";
        if (art_bootstrap == 2) {
            int count = snprintf(extra, sizeof(extra), "\"icu_cases\":8,\"icu_check_ns\":%" PRIu64 ",", icu_check_ns);
            if (count < 0 || (size_t)count >= sizeof(extra)) fail("ICU result formatting");
        }
        if (art_bootstrap == 3) {
            int count = snprintf(extra, sizeof(extra),
                "\"libcore_cases\":17,\"libcore_check_ns\":%" PRIu64 ",\"integer128_cases\":228,"
                "\"integer128_check_ns\":%" PRIu64 ",\"threads_reaped\":%" PRIu64 ",",
                libcore_check_ns, integer128_check_ns, reaped);
            if (count < 0 || (size_t)count >= sizeof(extra)) fail("libcore result formatting");
        }
        if (art_bootstrap == 4) {
            int count = snprintf(extra, sizeof(extra),
                "\"startup_ns\":%" PRIu64 ",\"managed_bytes\":%" PRIu64 ",\"threads_reaped\":%" PRIu64 ","
                "\"process_peak_rss_bytes\":%ld,\"managed_window_bytes\":%" PRIu64 ",",
                art_startup_ns, art_managed_bytes, reaped, usage.ru_maxrss, art_window_bytes);
            if (count < 0 || (size_t)count >= sizeof(extra)) fail("ART runtime result formatting");
        }
        if (art_bootstrap == 5) {
            int count = snprintf(extra, sizeof(extra),
                "\"looper\":{\"status\":%d,\"cases\":%u,\"failure\":%u,\"wake_threads\":%u,"
                "\"message_calls\":%u,\"fd_callbacks\":%u,\"timer_callbacks\":%u},"
                "\"looper_check_ns\":%" PRIu64 ",\"threads_reaped\":%" PRIu64 ",",
                looper_status, looper_result.cases, looper_result.failure, looper_result.wake_threads,
                looper_result.message_calls, looper_result.fd_callbacks, looper_result.timer_callbacks,
                looper_ns, reaped);
            if (count < 0 || (size_t)count >= sizeof(extra)) fail("Looper result formatting");
        }
        length = snprintf(report, sizeof(report),
            "{\"constructors\":%u,\"tls_modules\":%u,\"linked_images\":%u,\"registered_vms\":0,%s"
            "\"vm_budget_bytes\":%" PRIu64 ",\"reserved_bytes\":%" PRIu64 ",\"bootstrap_window_bytes\":%" PRIu64 ","
            "\"heap_binding_verified\":true,\"sigchain_cases\":22,\"sigchain_mutation\":-1005,"
            "\"runtime_started\":%s,\"dex_executed\":%s,"
            "\"load_relocate_ns\":%" PRIu64 ",\"bootstrap_ns\":%" PRIu64 ",\"cleanup\":true}",
            constructors, tls_count, image_count, extra, vm_budget, reserved, art_bootstrap_window_bytes,
            art_bootstrap == 4 ? "true" : "false", art_bootstrap == 4 ? "true" : "false",
            loaded-start, finished-loaded);
        if (length < 0 || (size_t)length >= sizeof(report)) fail("ART result formatting");
        host->log(host->context, report, (size_t)length);
        for (unsigned i = 0; i < image_count; ++i) { dlclose(images[i].handle); free(images[i].original); }
        return art_bootstrap == 5 && looper_status ? 1 : 0;
    }
    length = snprintf(report, sizeof(report), "{\"cases\":146,\"constructors\":%u,\"absent_netd\":%u,\"syscalls\":%u,\"load_relocate_ns\":%" PRIu64
           ",\"startup_client_ns\":%" PRIu64 ",\"reserved_bytes\":%" PRIu64 ",\"gwp_enabled\":%" PRIu64
           ",\"guarded_samples\":%" PRIu64 ",\"futex_cases\":%" PRId64
           ",\"pthread_result\":%d,\"threads_reaped\":%" PRIu64 ",\"pthread_client_ns\":%" PRIu64
           ",\"thread_guarded_samples\":%" PRIu64 ",\"process_peak_rss_bytes\":%ld,"
           "\"linked_images\":4,\"tls_modules\":2,\"tls_threads\":7,\"tls_result\":0,\"tls_queries\":%" PRIu64 ",\"version_result\":%" PRId64 ",\"mapping_cases\":%" PRId64 ",\"file_cases\":%" PRId64 ",\"file_client_ns\":%" PRIu64
           ",\"vm_cases\":%" PRId64 ",\"timeout_cases\":%" PRId64 ",\"proc_cases\":%" PRId64 ",\"art_libc_cases\":%" PRId64 ",\"vfork_cases\":%" PRId64 ",\"libcore_frontend_cases\":%" PRId64 ",\"unlink_cases\":%" PRId64 ",\"signal_wait_cases\":%" PRId64 ",\"signal_handler_cases\":%d,\"signal_handler_mutation\":%d,"
           "\"signal_stack_cases\":%d,\"signal_stack_handler_cases\":%d,\"signal_stack_mutation\":%d,\"signal_stack_threads\":%" PRIu64 ","
           "\"signal_mask_cases\":%d,\"signal_mask_mutation\":%d,\"signal_mask_threads\":%" PRIu64 ","
           "\"futex_requeue_cases\":%" PRId64 ",\"cwd_cases\":%" PRId64 ",\"signal_realtime_cases\":%d,\"signal_interrupt_cases\":%d,\"signal_interrupt_mutation\":%d,\"signal_interrupt_threads\":%" PRIu64 ","
           "\"signal_fault_cases\":%d,\"signal_fault_edit_mutation\":%d,\"signal_fault_address_mutation\":%d,"
           "\"binder_libc_cases\":%d,\"binder_libc_path_control\":%d,\"binder_libc_clock_control\":%d,"
           "\"binder_regex_cases\":%d,\"binder_regex_newline_control\":%d,\"binder_regex_capture_control\":%d,"
           "\"vm_budget_bytes\":%" PRIu64 ",\"allocator_pressure_cases\":%d,\"unsupported_syscalls\":{",
           constructors, absent_netd, calls, loaded-start, finished-loaded, reserved, gwp_enabled, guarded_samples, futex_cases, pthread_result, reaped, pthread_ns, thread_guarded_samples, usage.ru_maxrss, tls_queries, version_result, mapping_cases, file_cases, file_ns, vm_cases, timeout_cases, proc_cases, art_libc_cases, vfork_cases, libcore_frontend_cases, unlink_cases, signal_wait_cases,signal_handler_cases,signal_handler_mutation,
           signal_stack_cases,signal_stack_handler_cases,signal_stack_mutation,signal_stack_threads,
           signal_mask_cases,signal_mask_mutation,signal_mask_threads,
           futex_requeue_cases,cwd_cases,signal_realtime_cases,signal_interrupt_cases,signal_interrupt_mutation,signal_interrupt_threads,
           signal_fault_cases,signal_fault_edit_mutation,signal_fault_address_mutation,
           binder_libc_cases,binder_libc_path_control,binder_libc_clock_control,
           binder_regex_cases,binder_regex_newline_control,binder_regex_capture_control,vm_budget,allocator_pressure_cases);
    if (length < 0 || (size_t)length >= sizeof(report)) fail("result formatting");
    size_t used_bytes = (size_t)length;
    unsigned printed = 0;
    for (unsigned i = 0; i < 512; ++i) if (unsupported[i]) {
        length = snprintf(report + used_bytes, sizeof(report) - used_bytes, "%s\"%u\":%u", printed++ ? "," : "", i, unsupported[i]);
        if (length < 0 || (size_t)length >= sizeof(report) - used_bytes) fail("result formatting");
        used_bytes += (size_t)length;
    }
    if (used_bytes + 3 > sizeof(report)) fail("result formatting");
    memcpy(report + used_bytes, "}}", 3); used_bytes += 2;
    host->log(host->context, report, used_bytes);
    for (unsigned i = 0; i < image_count; ++i) { dlclose(images[i].handle); free(images[i].original); }
    guest_console = NULL;
    return 0;
}
int artbox_run_native_bionic(const artbox_bionic_input *input, const artbox_host *host) {
    if (!input) return -22;
    const native_input shared = {input->frameworks, input->elfs, input->root, input->sampled, 4, 0};
    return run_native(&shared, host, 0, NULL);
}
int artbox_run_native_art_bootstrap(const artbox_art_input *input, const artbox_host *host) {
    if (!input) return -22;
    const native_input shared = {input->frameworks, input->elfs, input->root, 0, 4, 0};
    return run_native(&shared, host, 1, NULL);
}
int artbox_run_native_icu(const artbox_icu_input *input, const artbox_host *host) {
    if (!input) return -22;
    const native_input shared = {input->frameworks, input->elfs, input->root, 0, 10, 0};
    return run_native(&shared, host, 2, NULL);
}
int artbox_run_native_libcore(const artbox_libcore_input *input, const artbox_host *host) {
    if (!input) return -22;
    const native_input shared = {input->frameworks, input->elfs, input->root, 0, 15, 0};
    return run_native(&shared, host, 3, NULL);
}
int artbox_run_native_art_runtime(const artbox_libcore_input *input, const artbox_host *host) {
    return artbox_run_native_art_runtime_logged(input, host, NULL);
}
int artbox_run_native_art_runtime_logged(const artbox_libcore_input *input,
    const artbox_host *host, const artbox_host *console) {
    if (!input) return -22;
    const native_input shared = {input->frameworks, input->elfs, input->root, 0, 15, 0};
    return run_native(&shared, host, 4, console);
}
int artbox_run_native_looper(const artbox_looper_input *input, const artbox_host *host) {
    if (!input) return -22;
    const native_input shared = {input->frameworks, input->elfs, input->root, 0, 5, input->mutation};
    return run_native(&shared, host, 5, NULL);
}
