// SPDX-License-Identifier: MIT
#include "artbox/guest_dlfcn.h"
#include <array>
#include <cstring>
#include <exception>
#include <memory>

struct artbox_guest_dlfcn {
    artbox_dlfcn *loader=nullptr;
    const artbox_load_group *group=nullptr;
    artbox_vm *vm=nullptr;
    artbox_guest_dl_ops ops{};
    uint64_t storage=0,storage_size=0;
    unsigned count=0;
    std::array<uint64_t,64> names{};
    ~artbox_guest_dlfcn() { if (storage) artbox_vm_munmap(vm,storage,storage_size); }
};
struct artbox_guest_dl_thread {
    artbox_guest_dlfcn *service=nullptr;
    artbox_dl_error error{};
    uint64_t storage=0;
    ~artbox_guest_dl_thread() {
        if (storage) artbox_vm_munmap(service->vm,storage,artbox_vm_page_size(service->vm));
    }
};
namespace {
void fail(artbox_guest_dl_thread *t,const char *text) {
    if (!t) return;
    size_t length=0;
    while (length+1<sizeof(t->error.message) && text[length]) ++length;
    std::memcpy(t->error.message,text,length);
    t->error.message[length]=0;t->error.pending=1;
}
void word(unsigned char *bytes,unsigned offset,uint64_t value,unsigned size=8) {
    for (unsigned i=0;i<size;++i) bytes[offset+i]=static_cast<unsigned char>(value>>(i*8));
}
uint64_t read_word(const unsigned char *bytes) {
    uint64_t value=0;
    for (unsigned i=0;i<8;++i) value|=static_cast<uint64_t>(bytes[i])<<(i*8);
    return value;
}
bool string(artbox_guest_dl_thread *t,uint64_t address,char (&out)[4097]) {
    if (!address) { fail(t,"loader: null string");return false; }
    for (unsigned i=0;i<sizeof(out);++i) {
        if (address>UINT64_MAX-i || artbox_vm_read(t->service->vm,address+i,out+i,1)) {
            fail(t,"loader: unreadable string");return false;
        }
        if (!out[i]) return true;
    }
    fail(t,"loader: string exceeds 4096 bytes");return false;
}
bool file_address(const artbox_link_info &info,uint64_t offset,uint64_t length,uint64_t *out) {
    const artbox_elf &elf=*info.dynamic->image;
    for (unsigned i=0;i<elf.segment_count;++i) {
        const auto &s=elf.segments[i];
        if ((s.flags&4) && offset>=s.file_offset && offset-s.file_offset<=s.file_size &&
            length<=s.file_size-(offset-s.file_offset)) {
            *out=info.load_bias+s.virtual_address+offset-s.file_offset;return true;
        }
    }
    return false;
}
bool callable(artbox_guest_dlfcn *s,uint64_t address) {
    if (!address || (address&3)) return false;
    artbox_link_address found;
    if (artbox_load_group_address(s->group,address,&found)!=ARTBOX_ELF_OK) return false;
    const auto &elf=*found.image.dynamic->image;
    for (unsigned i=0;i<elf.segment_count;++i) {
        const auto &segment=elf.segments[i];uint64_t base=found.image.load_bias+segment.virtual_address;
        if ((segment.flags&5)==5 && address>=base && segment.file_size>=4 && address-base<=segment.file_size-4)
            return true;
    }
    return false;
}
unsigned image_index(const artbox_guest_dlfcn *s,const artbox_dynamic *dynamic) {
    for (unsigned i=0;i<s->count;++i) {
        artbox_link_info info;
        if (artbox_dlfcn_info(s->loader,i,&info)==ARTBOX_ELF_OK && info.dynamic==dynamic) return i;
    }
    return s->count;
}
}

artbox_elf_result artbox_guest_dlfcn_create(artbox_dlfcn *loader,const artbox_load_group *group,
    artbox_vm *vm,const artbox_guest_dl_ops *ops,artbox_guest_dlfcn **out) {
    unsigned count=artbox_dlfcn_count(loader);
    if (!loader || !group || !vm || !ops || !ops->invoke || !out || !count || count>64 ||
        count!=artbox_load_group_count(group)) return ARTBOX_ELF_INVALID;
    try {
        std::unique_ptr<artbox_guest_dlfcn> s(new artbox_guest_dlfcn);
        s->loader=loader;s->group=group;s->vm=vm;s->ops=*ops;s->count=count;
        std::array<size_t,64> lengths{};uint64_t length=0;
        for (unsigned i=0;i<count;++i) {
            artbox_link_info info,original;
            if (artbox_dlfcn_info(loader,i,&info)!=ARTBOX_ELF_OK ||
                artbox_load_group_info(group,i,&original)!=ARTBOX_ELF_OK ||
                info.dynamic!=original.dynamic || info.load_bias!=original.load_bias) return ARTBOX_ELF_INVALID;
            if (info.tls_module_id && !ops->tls_data) return ARTBOX_ELF_UNSUPPORTED;
            lengths[i]=std::strlen(info.name)+1;
            if (lengths[i]>4097) return ARTBOX_ELF_INVALID;
            length+=lengths[i];
            const auto &elf=*info.dynamic->image;uint64_t phdr;
            if (!file_address(info,elf.program_header_offset,static_cast<uint64_t>(elf.program_header_count)*56,&phdr) ||
                !artbox_vm_access(vm,phdr,static_cast<uint64_t>(elf.program_header_count)*56,1)) return ARTBOX_ELF_INVALID;
        }
        uint64_t page=artbox_vm_page_size(vm);
        if (!page) return ARTBOX_ELF_INVALID;
        s->storage_size=(length+page-1)/page*page;
        int64_t memory=artbox_vm_mmap(vm,0,s->storage_size,3,0x22,-1,0);
        if (memory<0) return ARTBOX_ELF_NO_MEMORY;
        s->storage=static_cast<uint64_t>(memory);length=0;
        for (unsigned i=0;i<count;++i) {
            artbox_link_info info;
            if (artbox_dlfcn_info(loader,i,&info)!=ARTBOX_ELF_OK) return ARTBOX_ELF_INVALID;
            s->names[i]=s->storage+length;
            if (artbox_vm_write(vm,s->names[i],info.name,lengths[i])) return ARTBOX_ELF_INVALID;
            length+=lengths[i];
        }
        if (artbox_vm_mprotect(vm,s->storage,s->storage_size,1)) return ARTBOX_ELF_INVALID;
        *out=s.release();return ARTBOX_ELF_OK;
    } catch (const std::exception&) { return ARTBOX_ELF_NO_MEMORY; }
}
void artbox_guest_dlfcn_destroy(artbox_guest_dlfcn *s) { delete s; }
artbox_elf_result artbox_guest_dl_thread_create(artbox_guest_dlfcn *s,artbox_guest_dl_thread **out) {
    if (!s || !out) return ARTBOX_ELF_INVALID;
    try {
        std::unique_ptr<artbox_guest_dl_thread> t(new artbox_guest_dl_thread);
        t->service=s;
        int64_t address=artbox_vm_mmap(s->vm,0,artbox_vm_page_size(s->vm),3,0x22,-1,0);
        if (address<0) return ARTBOX_ELF_NO_MEMORY;
        t->storage=static_cast<uint64_t>(address);*out=t.release();return ARTBOX_ELF_OK;
    } catch (const std::exception&) { return ARTBOX_ELF_NO_MEMORY; }
}
void artbox_guest_dl_thread_destroy(artbox_guest_dl_thread *t) { delete t; }
uint64_t artbox_guest_dlopen(artbox_guest_dl_thread *t,uint64_t name,unsigned flags) {
    return artbox_guest_android_dlopen_ext(t,name,flags,0);
}
uint64_t artbox_guest_android_dlopen_ext(artbox_guest_dl_thread *t,
    uint64_t name,unsigned flags,uint64_t extinfo) {
    if (!t) return 0;
    char text[4097];
    if (name && !string(t,name,text)) return 0;
    unsigned char extension[48]{};
    if (extinfo && artbox_vm_read(t->service->vm,extinfo,extension,sizeof(extension))) {
        fail(t,"android_dlopen_ext: unreadable extension record");return 0;
    }
    return artbox_android_dlopen_ext(t->service->loader,&t->error,name?text:nullptr,flags,
        read_word(extension),read_word(extension+40));
}
uint64_t artbox_guest_android_get_exported_namespace(artbox_guest_dl_thread *t,uint64_t name) {
    if (!t || !name) return 0;
    char text[4097];
    if (!string(t,name,text)) return 0;
    return artbox_android_get_exported_namespace(t->service->loader,text);
}
uint64_t artbox_guest_dlsym(artbox_guest_dl_thread *t,uint64_t handle,uint64_t name,uint64_t version,uint64_t caller) {
    if (!t) return 0;
    char symbol[4097],version_name[4097];
    if (!string(t,name,symbol) || (version && !string(t,version,version_name))) return 0;
    return artbox_dlsym(t->service->loader,&t->error,handle,symbol,version?version_name:nullptr,caller);
}
int artbox_guest_dlclose(artbox_guest_dl_thread *t,uint64_t handle) {
    return t?artbox_dlclose(t->service->loader,&t->error,handle):-1;
}
uint64_t artbox_guest_dlerror(artbox_guest_dl_thread *t) {
    if (!t) return 0;
    const char *text=artbox_dlerror(&t->error);
    if (!text) return 0;
    if (artbox_vm_write(t->service->vm,t->storage,text,std::strlen(text)+1)) {
        fail(t,"dlerror: error storage unavailable");return 0;
    }
    return t->storage;
}
int artbox_guest_dladdr(artbox_guest_dl_thread *t,uint64_t address,uint64_t output) {
    if (!t) return 0;
    auto *s=t->service;artbox_link_address found;
    if (artbox_dlfcn_address(s->loader,address,&found)!=ARTBOX_ELF_OK) return 0;
    unsigned index=image_index(s,found.image.dynamic);
    if (index==s->count) return 0;
    const auto &elf=*found.image.dynamic->image;
    uint64_t first=UINT64_MAX,symbol_name=0;
    for (unsigned i=0;i<elf.segment_count;++i)
        if (elf.segments[i].virtual_address<first) first=elf.segments[i].virtual_address;
    if (found.symbol_name) {
        uint64_t offset=reinterpret_cast<const unsigned char*>(found.symbol_name)-elf.data;
        if (!file_address(found.image,offset,std::strlen(found.symbol_name)+1,&symbol_name)) return 0;
    }
    unsigned char info[32]{};
    word(info,0,s->names[index]);word(info,8,found.image.load_bias+first);
    word(info,16,symbol_name);word(info,24,found.symbol_address);
    if (artbox_vm_write(s->vm,output,info,sizeof(info))) { fail(t,"dladdr: output is not writable");return 0; }
    return 1;
}
int artbox_guest_dl_iterate_phdr(artbox_guest_dl_thread *t,uint64_t callback,uint64_t data) {
    if (!t) return -1;
    auto *s=t->service;
    if (!callable(s,callback)) { fail(t,"dl_iterate_phdr: callback is outside signed code");return -1; }
    struct Scratch {
        artbox_vm *vm;uint64_t address,length;
        ~Scratch() { if (address) artbox_vm_munmap(vm,address,length); }
    } scratch{s->vm,0,artbox_vm_page_size(s->vm)};
    int64_t storage=artbox_vm_mmap(s->vm,0,scratch.length,3,0x22,-1,0);
    if (storage<0) { fail(t,"dl_iterate_phdr: no callback storage");return -1; }
    scratch.address=static_cast<uint64_t>(storage);
    for (unsigned i=0;i<s->count;++i) {
        artbox_link_info info;uint64_t phdr=0,tls=0;
        if (artbox_dlfcn_info(s->loader,i,&info)!=ARTBOX_ELF_OK) return -1;
        const auto &elf=*info.dynamic->image;
        if (!file_address(info,elf.program_header_offset,static_cast<uint64_t>(elf.program_header_count)*56,&phdr)) return -1;
        if (info.tls_module_id && s->ops.tls_data(s->ops.context,info.tls_module_id,&tls)!=ARTBOX_ELF_OK) {
            fail(t,"dl_iterate_phdr: TLS state unavailable");return -1;
        }
        if (tls && !artbox_vm_access(s->vm,tls,elf.tls.memory_size,1)) {
            fail(t,"dl_iterate_phdr: TLS block is not readable");return -1;
        }
        unsigned char record[64]{};
        word(record,0,info.load_bias);word(record,8,s->names[i]);word(record,16,phdr);
        word(record,24,elf.program_header_count,2);word(record,32,s->count);
        word(record,48,info.tls_module_id);word(record,56,tls);
        if (artbox_vm_write(s->vm,scratch.address,record,sizeof(record))) return -1;
        uint64_t arguments[3]={scratch.address,sizeof(record),data},result=0;
        if (s->ops.invoke(s->ops.context,callback,arguments,&result)!=ARTBOX_ELF_OK) {
            fail(t,"dl_iterate_phdr: callback failed");return -1;
        }
        uint32_t value=static_cast<uint32_t>(result);
        if (value) return static_cast<int>(value>INT32_MAX?static_cast<int64_t>(value)-INT64_C(0x100000000):value);
    }
    return 0;
}
