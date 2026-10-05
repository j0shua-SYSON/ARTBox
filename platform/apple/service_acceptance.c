// Independent native guest roles sharing one portable Binder device. MIT.
#include "artbox/native_service.h"
#include "artbox/binder_device.h"
#include "artbox/dynamic.h"
#include "artbox/linker.h"
#include "artbox/native_call.h"
#include "artbox/native_dlfcn.h"
#include "artbox/native_files.h"
#include "artbox/native_system.h"
#include "artbox/native_vm.h"
#include "artbox/native_wake.h"
#include "artbox/native_atomic.h"
#include "artbox/native_thread.h"
#include "artbox/native_syscall.h"
#include "artbox/native_tls.h"
#include "artbox/native_signal_binding.h"
#include "artbox/native_signal_context.h"
#include "artbox/native_signal_delivery.h"
#include "artbox/signals.h"
#include <dlfcn.h>
#include <inttypes.h>
#include <pthread.h>
#include <setjmp.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/resource.h>
#include <unistd.h>

enum { MODULES=6, ROLES=3 };
typedef struct service_image {
    unsigned char *bytes,*rx,*rw;
    void *handle;
    artbox_elf elf;
    artbox_dynamic dynamic;
} service_image;
typedef struct service_role {
    service_image images[MODULES];
    // The load group borrows these descriptors through constructor execution.
    artbox_relocation_memory writable[MODULES];
    artbox_vm *vm;
    artbox_native_files *files;
    artbox_vfs *vfs;
    artbox_futex *futex;
    artbox_threads *threads;
    artbox_signals *signals;
    artbox_kernel_thread kernel;
    artbox_load_group *group;
    artbox_dlfcn *loader;
    artbox_guest_dlfcn *dl;
    artbox_signal_memory_range code[MODULES],data[MODULES];
    artbox_native_signal_thread signal_template;
    void *worker;
    unsigned index,mode,constructors,tls_modules;
    uint64_t started_ns,bootstrap_ns,reserved;
    int result,access_cases,policy_cases;
    _Atomic unsigned ready,done,calls;
} service_role;
static _Thread_local service_role *active;
static _Thread_local artbox_kernel_thread *current;
static _Thread_local jmp_buf *exit_boundary;
static _Thread_local artbox_thread_finish *finish_state;

static _Noreturn void fail(const char *reason) {
    fprintf(stderr,"service failure: %s\n",reason); _Exit(1);
}
static uint64_t now(void) {
    struct timespec t;
    if(clock_gettime(CLOCK_MONOTONIC,&t)) fail("clock");
    return (uint64_t)t.tv_sec*UINT64_C(1000000000)+(uint64_t)t.tv_nsec;
}
static void pause_owner(void) { struct timespec t={0,1000000}; nanosleep(&t,NULL); }
static const void *entry(service_role *role,unsigned image,const char *name) {
    service_image *m=&role->images[image]; artbox_elf_symbol s;
    if(artbox_dynamic_lookup(&m->dynamic,name,&s)!=ARTBOX_ELF_OK || s.type!=2 || s.value%4 ||
            s.value>=m->elf.segments[0].file_size || s.size>m->elf.segments[0].file_size-s.value) fail(name);
    return m->rx+s.value;
}
static uint64_t call(service_role *r,unsigned image,const char *name,uint64_t a,uint64_t b,uint64_t c) {
    return artbox_call7(entry(r,image,name),a,b,c,0,0,0,0);
}
static _Noreturn void finish(uint64_t address,uint64_t size,int error) {
    if(!exit_boundary || !finish_state) fail("unexpected guest thread exit");
    finish_state->unmap_address=address; finish_state->unmap_size=size; finish_state->error=error;
    longjmp(*exit_boundary,1);
}
static _Noreturn void teardown(void *address,size_t size) { finish((uintptr_t)address,size,0); }
static int64_t clone_guest(const artbox_thread_start *source) {
    artbox_thread_start start; service_role *r=active;
    if(!r || artbox_vm_read(r->vm,(uintptr_t)source,&start,sizeof(start))) return -14;
    uint64_t rx=(uintptr_t)r->images[0].rx;
    if(start.entry<rx || start.entry-rx>=r->images[0].elf.segments[0].file_size || start.entry%4) return -22;
    return artbox_threads_start(r->threads,current,&start);
}
static int sdk(void) { return 35; }
static uint64_t unexpected(void) { fail("unsupported loader/process interface"); }
static artbox_elf_result resolve(void *opaque,const artbox_dynamic *dynamic,uint32_t index,uint64_t *value) {
    (void)opaque;
    artbox_elf_symbol s;
    if(artbox_dynamic_symbol(dynamic,index,&s)!=ARTBOX_ELF_OK) return ARTBOX_ELF_INVALID;
    artbox_elf_result found=artbox_native_dlfcn_resolve(NULL,dynamic,index,value);
    if(found!=ARTBOX_ELF_NOT_FOUND) return found;
#define HOST(import_name,function) if(!strcmp(s.name,import_name)) { void (*p)(void)=(void (*)(void))(function); \
    _Static_assert(sizeof(p)==sizeof(*value),"code pointer"); memcpy(value,&p,sizeof(p)); return ARTBOX_ELF_OK; }
    HOST("artbox_bionic_syscall",artbox_bionic_syscall)
    HOST("artbox_bionic_get_tls",artbox_bionic_get_tls)
    HOST("artbox_bionic_set_tls",artbox_bionic_set_tls)
    if(dynamic->soname && !strcmp(dynamic->soname,"libart.so")) {
        HOST("artbox_vm_access",artbox_vm_access)
        HOST("artbox_vm_mmap_window",artbox_vm_mmap_window)
        HOST("artbox_vm_mprotect",artbox_vm_mprotect)
        HOST("artbox_vm_munmap",artbox_vm_munmap)
        HOST("artbox_vm_page_size",artbox_vm_page_size)
        HOST("artbox_vm_reserve_window",artbox_vm_reserve_window)
        HOST("artbox_vm_reserved_bytes",artbox_vm_reserved_bytes)
    }
    HOST("android_get_application_target_sdk_version",sdk)
    HOST("artbox_host_pthread_clone",clone_guest)
    HOST("_exit_with_stack_teardown",teardown)
    HOST("vfork",unexpected)
    HOST("android_dlopen_ext",unexpected)
    HOST("android_get_exported_namespace",unexpected)
#undef HOST
    if(s.binding!=2) fprintf(stderr,"unresolved service symbol: %s\n",s.name);
    return ARTBOX_ELF_NOT_FOUND;
}
static int64_t dispatch(void *opaque,uint64_t n,uint64_t a,uint64_t b,uint64_t c,
                        uint64_t d,uint64_t e,uint64_t f) {
    artbox_kernel_thread *kernel=opaque; service_role *r=active;
    if(!r || current!=kernel || kernel->vm!=r->vm) fail("foreign role dispatch");
    if(n==93 && exit_boundary) finish(0,0,a ? -5 : 0);
    int64_t result=n==222 ? artbox_vfs_mmap(r->vfs,r->vm,a,b,c,d,(int64_t)e,f) :
        artbox_kernel_call(kernel,n,a,b,c,d,e,f);
    if(n==98) result=artbox_futex_call_interruptible(r->futex,kernel,a,b,c,d,e,f);
    if(result==-38) result=artbox_vfs_syscall(r->vfs,kernel,n,a,b,c,d,e,f);
    if(n==64 && (a==1 || a==2)) {
        char text[4096];
        if(c>sizeof(text)) result=-22;
        else if(artbox_vm_read(r->vm,b,text,(size_t)c)) result=-14;
        else result=fwrite(text,1,(size_t)c,stderr)==(size_t)c ? (int64_t)c : -5;
    }
    // Observe successful original driver registration; do not invent readiness.
    if(r->index==0 && n==29 && !result &&
            (b==ARTBOX_BINDER_SET_CONTEXT_MGR || b==ARTBOX_BINDER_SET_CONTEXT_MGR_EXT))
        atomic_store(&r->ready,1);
    if(atomic_fetch_add(&r->calls,1)<100 || result<0)
        fprintf(stderr,"role %u syscall %" PRIu64 "(%" PRIx64 ",%" PRIx64 ",%" PRIx64 ")=%" PRId64 "\n",
                r->index,n,a,b,c,result);
    if(n==93 || n==94) fail("guest process exit");
    return result;
}
static artbox_elf_result invoke(void *context,uint64_t address,const uint64_t args[3],uint64_t *value) {
    (void)context;
    *value=artbox_call7((void *)(uintptr_t)address,args[0],args[1],args[2],0,0,0,0);
    return ARTBOX_ELF_OK;
}
static artbox_elf_result tls(void *context,uint64_t id,uint64_t *value) {
    *value=call(context,0,"artbox_bootstrap_tls_data",id,0,0); return ARTBOX_ELF_OK;
}
static artbox_elf_result construct(void *context,uint64_t address) {
    service_role *r=context;
    artbox_call7((void *)(uintptr_t)address,0,0,0,0,0,0,0); ++r->constructors;
    return ARTBOX_ELF_OK;
}
static void run_child(void *opaque,artbox_kernel_thread *kernel,const artbox_thread_start *start,
                      artbox_thread_finish *state) {
    service_role *r=opaque; active=r; current=kernel;
    const artbox_syscall_binding binding={dispatch,kernel};
    const artbox_syscall_binding *previous=artbox_native_syscall_swap(&binding);
    void **old_tls=artbox_native_tls_swap((void **)(uintptr_t)start->tls);
    artbox_native_signal_thread signal_thread=r->signal_template;
    signal_thread.kernel=kernel; signal_thread.stack_address=start->stack_base;
    signal_thread.stack_size=start->stack_size; signal_thread.guest_tls=(void **)(uintptr_t)start->tls;
    if(artbox_native_signal_thread_attach(&signal_thread)) fail("child signal attachment");
    artbox_guest_dl_thread *dl=NULL;
    if(artbox_guest_dl_thread_create(r->dl,&dl)!=ARTBOX_ELF_OK) fail("child loader");
    artbox_guest_dl_thread *old_dl=artbox_native_dlfcn_swap(dl);
    jmp_buf boundary; exit_boundary=&boundary; finish_state=state;
    if(!setjmp(boundary)) {
        call(r,0,"artbox_bootstrap_thread",start->entry,start->argument,0); state->error=-5;
    }
    exit_boundary=NULL; finish_state=NULL;
    if(artbox_native_signal_thread_detach(&signal_thread)) fail("child signal detach");
    artbox_native_dlfcn_swap(old_dl); artbox_guest_dl_thread_destroy(dl);
    artbox_native_tls_swap(old_tls); artbox_native_syscall_swap(previous);
    current=NULL; active=NULL;
}
static void run_role(void *opaque) {
    service_role *r=opaque; active=r; current=&r->kernel;
    const artbox_syscall_binding binding={dispatch,current};
    const artbox_syscall_binding *previous=artbox_native_syscall_swap(&binding);
    void **old_tls=artbox_native_tls_swap(NULL);
    artbox_guest_dl_thread *dl=NULL;
    if(artbox_guest_dl_thread_create(r->dl,&dl)!=ARTBOX_ELF_OK) fail("primary loader");
    artbox_guest_dl_thread *old_dl=artbox_native_dlfcn_swap(dl);
    unsigned char random[16]; char name[]="artbox-service";
    if(r->kernel.system.random(random,sizeof(random))) fail("random");
    uint32_t uid=r->kernel.credentials.uid;
    uint64_t args[]={1,(uintptr_t)name,0,0,6,artbox_vm_page_size(r->vm),
        11,uid,12,uid,13,uid,14,uid,16,0,17,100,23,0,25,(uintptr_t)random,26,0,31,(uintptr_t)name,
        51,ARTBOX_SIGNAL_STACK_MINIMUM,0,0};
    artbox_tls_template templates[16];
    r->tls_modules=artbox_load_group_tls_count(r->group);
    if(!r->tls_modules || r->tls_modules>16) fail("TLS module count");
    for(unsigned i=0;i<r->tls_modules;++i)
        if(artbox_load_group_tls_template(r->group,i,&templates[i])!=ARTBOX_ELF_OK) fail("TLS template");
    if(call(r,0,"artbox_bootstrap_main",(uintptr_t)args,(uintptr_t)templates,r->tls_modules) ||
            !artbox_bionic_get_tls()) fail("Bionic bootstrap");
    artbox_native_signal_thread signal_thread=r->signal_template;
    signal_thread.kernel=current; signal_thread.guest_tls=artbox_bionic_get_tls();
    if(artbox_native_signal_thread_attach(&signal_thread)) fail("primary signal attachment");
    artbox_elf_result initialized=artbox_load_group_initialize(r->group,construct,r);
    if(initialized!=ARTBOX_ELF_OK) {
        fprintf(stderr,"role %u constructor result: %d after %u calls\n",
                r->index,initialized,r->constructors);
        fail("constructors");
    }
    if((int32_t)call(r,0,"getpid",0,0,0)!=r->kernel.pid ||
            call(r,0,"getuid",0,0,0)!=uid) fail("Bionic process identity");
    r->bootstrap_ns=now()-r->started_ns;
    if(!r->index) {
        r->policy_cases=(int32_t)call(r,5,"artbox_service_kernel_policy_check",0,0,0);
        if(!r->mode) {
            // Original main sets restrictions before creating IPCThreadState.
            // The standalone Access contract intentionally runs in a separate
            // process; its IPCThreadState must not preinitialize this daemon.
            r->access_cases=(int32_t)call(r,5,"artbox_service_access_prepare",0,0,0);
            if(r->access_cases || r->policy_cases!=5) fail("Access or VINTF policy setup");
            char *argv[]={name,NULL};
            r->result=(int32_t)call(r,5,"main",1,(uintptr_t)argv,0);
            fail("original servicemanager returned");
        }
        r->access_cases=(int32_t)call(r,5,"artbox_service_access_check",r->mode>1 ? r->mode-1 : 0,0,0);
        r->result=r->access_cases;
    } else r->result=(int32_t)call(r,5,r->index==1 ? "artbox_service_provider" : "artbox_service_client",0,0,0);
    if(artbox_native_signal_thread_detach(&signal_thread)) fail("primary signal detach");
    artbox_native_dlfcn_swap(old_dl); artbox_guest_dl_thread_destroy(dl);
    artbox_native_tls_swap(old_tls); artbox_native_syscall_swap(previous);
    current=NULL; active=NULL; atomic_store(&r->done,1);
}
static void load(service_role *r,unsigned index,const char *framework,const char *path) {
    service_image *m=&r->images[index]; FILE *f=fopen(path,"rb"); long size;
    if(!f || fseek(f,0,SEEK_END) || (size=ftell(f))<=0 || size>64*1024*1024 || fseek(f,0,SEEK_SET)) fail("ELF input");
    m->bytes=malloc((size_t)size);
    if(!m->bytes || fread(m->bytes,1,(size_t)size,f)!=(size_t)size || fclose(f)) fail("ELF read");
    if(artbox_elf_open(m->bytes,(size_t)size,&m->elf)!=ARTBOX_ELF_OK || m->elf.type!=3 ||
            m->elf.segment_count!=2 || m->elf.segments[0].virtual_address || m->elf.segments[0].flags!=5 ||
            m->elf.segments[1].flags!=6 || !m->elf.segments[1].memory_size ||
            m->elf.segments[1].memory_size>64*1024*1024 ||
            artbox_dynamic_open(&m->elf,&m->dynamic)!=ARTBOX_ELF_OK || m->dynamic.init || m->dynamic.preinit_array.size)
        fail("controlled ELF shape");
    size_t rw=(size_t)((m->elf.segments[1].memory_size+16383)&~UINT64_C(16383));
    m->handle=dlopen(framework,RTLD_NOW|RTLD_LOCAL);
    if(!m->handle) fail(dlerror());
    m->rx=dlsym(m->handle,"artbox_dynamic_rx"); m->rw=dlsym(m->handle,"artbox_dynamic_rw");
    if(!m->rx || !m->rw || (uintptr_t)m->rx%16384 ||
            (uintptr_t)m->rw!=(uintptr_t)m->rx+m->elf.segments[1].virtual_address ||
            memcmp(m->rx,m->bytes,(size_t)m->elf.segments[0].file_size) ||
            memcmp(m->rw,m->bytes+m->elf.segments[1].file_offset,(size_t)m->elf.segments[1].file_size))
        fail("signed bytes or load bias");
    for(uint64_t i=m->elf.segments[1].file_size;i<rw;++i) if(m->rw[i]) fail("BSS");
    if(artbox_vm_register_readonly(r->vm,m->rx,(size_t)m->elf.segments[0].file_size) ||
            artbox_vm_register_data(r->vm,m->rw,rw,3)) fail("borrow signed image");
    r->code[index]=(artbox_signal_memory_range){(uintptr_t)m->rx,m->elf.segments[0].file_size};
    r->data[index]=(artbox_signal_memory_range){(uintptr_t)m->rw,m->elf.segments[1].memory_size};
}
static void prepare_role(service_role *r,const artbox_service_input *input,unsigned index,artbox_binder_device *device) {
    r->index=index; r->mode=input->mode;
    artbox_vm_ops memory=artbox_native_vm(); artbox_system_ops system=artbox_native_system();
    r->vm=artbox_vm_create(&memory,UINT64_C(1024)<<20,4096);
    if(!r->vm || artbox_native_files_open(input->roots[index],&r->files)) fail("role backing");
    artbox_file_ops files=artbox_native_files_ops(r->files); r->vfs=artbox_vfs_create(&files,256);
    artbox_atomic_u32_ops atomic=artbox_native_atomic_u32();
    r->futex=artbox_futex_create(r->vm,&atomic,&system,4096);
    uint32_t uid=index==0 ? 1000 : index==1 ? 1001 : 10000;
    int32_t pid=(int32_t)(100+index); artbox_credentials credentials={uid,uid,uid,uid};
    if(!r->vfs || !r->futex || artbox_kernel_thread_init_with_credentials(&r->kernel,r->vm,&system,pid,pid,&credentials))
        fail("role kernel");
    artbox_wake_ops wake=artbox_native_wake();
    if(artbox_vfs_set_epoll(r->vfs,&wake,1024,64) || artbox_vfs_set_binder(r->vfs,device,uid) ||
            artbox_vfs_set_commandline(r->vfs,"artbox-service",15)) fail("role VFS");
    r->signals=artbox_signals_create(r->vm,pid,uid,65);
    artbox_thread_ops threads=artbox_native_threads();
    r->threads=artbox_threads_create(r->vm,r->futex,&atomic,&system,&threads,pid,1000+index*100,64,run_child,r);
    if(!r->signals || !r->threads) fail("role thread ownership");
    for(unsigned i=0;i<MODULES;++i) load(r,i,input->frameworks[index][i],input->elfs[index][i]);
    r->signal_template.code=r->code; r->signal_template.code_count=MODULES;
    r->signal_template.data=r->data; r->signal_template.data_count=MODULES;
    r->signal_template.interrupt_signal=34;
    if(artbox_signals_enable_interrupt(r->signals,34,256) ||
            artbox_signals_enable_actions(r->signals,4096,artbox_native_signal_validate_action,&r->signal_template,UINT64_C(0x18000004)) ||
            artbox_signals_enable_stacks(r->signals,ARTBOX_SIGNAL_STACK_MINIMUM,4096) ||
            artbox_signals_attach(r->signals,&r->kernel)) fail("role signals");
    r->signal_template.actions=artbox_signals_action_table(r->signals);
    const char *names[]={"libc.so","libart.so","libm.so","libdl.so","libdl_android.so","libartbox_servicemanager.so"};
    artbox_link_module modules[MODULES];
    for(unsigned i=0;i<MODULES;++i) {
        if(!r->images[i].dynamic.soname || strcmp(r->images[i].dynamic.soname,names[i])) fail("role image order");
        r->writable[i]=(artbox_relocation_memory){1,r->images[i].rw,(size_t)r->images[i].elf.segments[1].memory_size};
        modules[i]=(artbox_link_module){names[i],&r->images[i].dynamic,(uintptr_t)r->images[i].rx,&r->writable[i],1};
    }
    artbox_elf_result linked=artbox_load_group_create(modules,MODULES,names[5],resolve,r,&r->group);
    if(linked==ARTBOX_ELF_OK) linked=artbox_load_group_tls_resolver(r->group,(uintptr_t)entry(r,0,"artbox_tlsdesc_absolute"));
    if(linked==ARTBOX_ELF_OK) linked=artbox_load_group_relocate(r->group);
    if(linked!=ARTBOX_ELF_OK) { fprintf(stderr,"service link result: %d\n",linked); fail("load group"); }
    artbox_guest_dl_ops loader={r,invoke,tls};
    if(artbox_dlfcn_create(r->group,NULL,0,&r->loader)!=ARTBOX_ELF_OK ||
            artbox_guest_dlfcn_create(r->loader,r->group,r->vm,&loader,&r->dl)!=ARTBOX_ELF_OK) fail("role loader");
    size_t stack_size=4*1024*1024,page=memory.page_size;
    int64_t stack=artbox_vm_mmap(r->vm,0,stack_size+2*page,0,0x22,-1,0);
    if(stack<0 || artbox_vm_mprotect(r->vm,(uint64_t)stack+page,stack_size,3)) fail("role stack");
    r->signal_template.stack_address=(uint64_t)stack+page; r->signal_template.stack_size=stack_size;
}
static void start_role(service_role *r) {
    artbox_thread_ops ops=artbox_native_threads(); r->started_ns=now();
    if(ops.start((void *)(uintptr_t)r->signal_template.stack_address,r->signal_template.stack_size,run_role,r,&r->worker))
        fail("start role");
}
static void destroy_role(service_role *r) {
    artbox_thread_ops ops=artbox_native_threads();
    if(!atomic_load(&r->done) || ops.join(r->worker)) fail("join finite role");
    r->reserved=artbox_vm_reserved_bytes(r->vm);
    if(artbox_threads_destroy(r->threads) || artbox_signals_detach(&r->kernel) || artbox_signals_destroy(r->signals))
        fail("role thread cleanup");
    artbox_guest_dlfcn_destroy(r->dl); artbox_dlfcn_destroy(r->loader);
    artbox_load_group_destroy(r->group);
    if(artbox_vfs_destroy(r->vfs) || artbox_futex_destroy(r->futex) || artbox_vm_destroy(r->vm) ||
            artbox_native_files_close(r->files)) fail("finite role cleanup");
    // Signed images stay mapped: process globals cannot safely be reused.
}
static void fault(int n,siginfo_t *info,void *state) {
    artbox_native_signal_thread *thread=artbox_native_signal_thread_context();
    int error=n==SIGUSR1 ? artbox_native_signal_deliver_interrupt(thread,state) :
        artbox_native_signal_deliver_fault(thread,n,info,state);
    if(error) { const char text[]="service native fault\n"; (void)write(2,text,sizeof(text)-1); _Exit(1); }
}
int artbox_run_native_service(const artbox_service_input *input,const artbox_host *host) {
    static atomic_flag used=ATOMIC_FLAG_INIT;
    if(!input || !host || !host->log || !input->backing_root || input->mode>3) return -22;
    unsigned count=input->mode ? 1 : ROLES;
    for(unsigned r=0;r<count;++r) {
        if(!input->roots[r]) return -22;
        for(unsigned i=0;i<MODULES;++i) if(!input->frameworks[r][i] || !input->elfs[r][i]) return -22;
    }
    if(atomic_flag_test_and_set(&used)) return -114;
    service_role *roles=calloc(ROLES,sizeof(*roles));
    artbox_native_files *backing=NULL;
    artbox_binder_device *device=artbox_binder_device_create(8,64);
    if(!roles || !device || artbox_native_files_open(input->backing_root,&backing)) fail("Binder owner");
    artbox_vm_ops memory=artbox_native_vm(); artbox_file_ops files=artbox_native_files_ops(backing);
    const artbox_binder_memory_ops shared={memory,files.mapping,backing,artbox_native_files_temporary,files.close};
    if(artbox_binder_device_set_memory(device,&shared,4*1024*1024)) fail("Binder receive backing");
    struct sigaction action={0},saved[6]; const int signals[]={SIGTRAP,SIGSEGV,SIGBUS,SIGILL,SIGUSR1,SIGABRT};
    action.sa_sigaction=fault; action.sa_flags=SA_SIGINFO|SA_ONSTACK|SA_RESTART;
    sigemptyset(&action.sa_mask); sigaddset(&action.sa_mask,SIGUSR1);
    for(unsigned i=0;i<6;++i) {
        action.sa_flags=SA_SIGINFO|SA_ONSTACK|(signals[i]==SIGUSR1 ? 0 : SA_RESTART);
        if(sigaction(signals[i],&action,&saved[i])) fail("fault handler");
    }
    uint64_t began=now();
    for(unsigned r=0;r<count;++r) {
        prepare_role(&roles[r],input,r,device);
        for(unsigned prior=0;prior<r;++prior) for(unsigned i=0;i<MODULES;++i)
            if(roles[r].images[i].rx==roles[prior].images[i].rx || roles[r].images[i].rw==roles[prior].images[i].rw)
                fail("dyld reused a role image");
    }
    start_role(&roles[0]);
    if(input->mode) {
        while(!atomic_load(&roles[0].done) && now()-began<UINT64_C(20000000000)) pause_owner();
        destroy_role(&roles[0]);
    } else {
        while(!atomic_load(&roles[0].ready) && now()-began<UINT64_C(10000000000)) pause_owner();
        if(!atomic_load(&roles[0].ready)) fail("manager registration deadline");
        start_role(&roles[1]); start_role(&roles[2]);
        while(!atomic_load(&roles[1].done) && now()-began<UINT64_C(20000000000)) pause_owner();
        if(!atomic_load(&roles[1].done)) fail("provider deadline");
        fprintf(stderr,"provider result: %d\n",roles[1].result);
        destroy_role(&roles[1]); // Close plus final unmap produces real death.
        while(!atomic_load(&roles[2].done) && now()-began<UINT64_C(25000000000)) pause_owner();
        if(!atomic_load(&roles[2].done)) fail("client deadline");
        fprintf(stderr,"client result: %d\n",roles[2].result);
        destroy_role(&roles[2]);
    }
    int expected=input->mode==2 ? -109 : input->mode==3 ? -107 : input->mode==1 ? 20 : 0;
    int passed=roles[0].access_cases==expected && roles[0].policy_cases==5 &&
        (input->mode || (roles[1].result==32 && roles[2].result==32));
    struct rusage usage;
    if(getrusage(RUSAGE_SELF,&usage)) fail("RSS");
    char report[2048];
    int length=snprintf(report,sizeof(report),"{\"access_cases\":%d,\"policy_cases\":%d,\"provider\":%d,\"client\":%d,"
        "\"roles\":%u,\"independent_images\":%u,\"manager_retained\":%s,\"finite_roles_cleaned\":true,"
        "\"elapsed_ns\":%" PRIu64 ",\"peak_rss_bytes\":%" PRIu64 ",\"passed\":%s}\n",
        roles[0].access_cases,roles[0].policy_cases,roles[1].result,roles[2].result,count,count*MODULES,
        input->mode ? "false" : "true",now()-began,(uint64_t)usage.ru_maxrss,passed ? "true" : "false");
    if(length<0 || (size_t)length>=sizeof(report)) fail("report size");
    host->log(host->context,report,(size_t)length);
    if(input->mode) {
        if(artbox_binder_device_destroy(device) || artbox_native_files_close(backing)) fail("Binder cleanup");
        for(unsigned i=0;i<6;++i) if(sigaction(signals[i],&saved[i],NULL)) fail("restore signals");
        free(roles);
    }
    return passed ? 0 : 1;
}
