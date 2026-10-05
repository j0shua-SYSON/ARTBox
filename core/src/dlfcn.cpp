// SPDX-License-Identifier: MIT
#include "artbox/dlfcn.h"
#include <array>
#include <atomic>
#include <cstring>
#include <exception>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace {
std::atomic<uint64_t> next_identity{1};
void fail(artbox_dl_error *error, const char *message) {
    if (!error) return;
    size_t length=0;
    while (length+1<sizeof(error->message) && message[length]) ++length;
    std::memcpy(error->message,message,length);
    error->message[length]=0;
    error->pending=1;
}
struct Alias { std::string path; unsigned index; };
}

struct artbox_dlfcn {
    const artbox_load_group *group=nullptr;
    unsigned count=0;
    uint64_t identity=0;
    std::string namespace_name;
    std::array<artbox_link_info,64> images{};
    std::array<std::string,64> names;
    std::array<uint64_t,64> references{};
    std::vector<Alias> aliases;
    std::mutex mutex;

    unsigned find(const char *name) const {
        for (unsigned i=0;i<count;++i) if (!std::strcmp(images[i].name,name)) return i;
        return count;
    }
    unsigned opened(uint64_t handle) const {
        unsigned low=static_cast<unsigned>(handle&255), index=low/2;
        return (handle&~UINT64_C(255))==identity && (low&1) && index<count && references[index]
            ? index : count;
    }
};

artbox_elf_result artbox_dlfcn_create(const artbox_load_group *group,
    const artbox_dl_alias *aliases,unsigned alias_count,artbox_dlfcn **out) {
    return artbox_dlfcn_create_with_namespace(group,aliases,alias_count,nullptr,out);
}
artbox_elf_result artbox_dlfcn_create_with_namespace(const artbox_load_group *group,
    const artbox_dl_alias *aliases,unsigned alias_count,const char *namespace_name,artbox_dlfcn **out) {
    unsigned count=artbox_load_group_count(group);
    if (!group || !out || !count || count>64 || alias_count>128 || (alias_count && !aliases))
        return ARTBOX_ELF_INVALID;
    if (namespace_name && (!*namespace_name || std::strlen(namespace_name)>4096)) return ARTBOX_ELF_INVALID;
    try {
        std::unique_ptr<artbox_dlfcn> loader(new artbox_dlfcn);
        loader->group=group;loader->count=count;
        if (namespace_name) loader->namespace_name=namespace_name;
        for (unsigned i=0;i<count;++i) {
            artbox_elf_result r=artbox_load_group_info(group,i,&loader->images[i]);
            if (r!=ARTBOX_ELF_OK) return r;
            loader->names[i]=loader->images[i].name;
        }
        artbox_relocation_stats stats;
        artbox_elf_result r=artbox_load_group_stats(group,loader->images[0].name,&stats);
        if (r!=ARTBOX_ELF_OK) return r;
        std::array<bool,64> named{};
        for (unsigned i=0;i<alias_count;++i) {
            const auto &a=aliases[i];
            if (!a.path || a.path[0]!='/' || std::strlen(a.path)>4096 || !a.module) return ARTBOX_ELF_INVALID;
            unsigned index=loader->find(a.module);
            if (index==count) return ARTBOX_ELF_NOT_FOUND;
            for (const auto &existing:loader->aliases) if (existing.path==a.path) return ARTBOX_ELF_INVALID;
            loader->aliases.push_back({a.path,index});
            if (!named[index]) { loader->names[index]=a.path;named[index]=true; }
        }
        uint64_t identity=next_identity.fetch_add(1,std::memory_order_relaxed);
        if (!identity || identity>=(UINT64_MAX>>8)) return ARTBOX_ELF_UNSUPPORTED;
        loader->identity=identity<<8;
        *out=loader.release();return ARTBOX_ELF_OK;
    } catch (const std::exception&) { return ARTBOX_ELF_NO_MEMORY; }
}
void artbox_dlfcn_destroy(artbox_dlfcn *loader) { delete loader; }
const char *artbox_dlerror(artbox_dl_error *error) {
    if (!error || !error->pending) return nullptr;
    error->pending=0;return error->message;
}
uint64_t artbox_dlopen(artbox_dlfcn *loader,artbox_dl_error *error,const char *name,unsigned flags) {
    return artbox_android_dlopen_ext(loader,error,name,flags,0,0);
}
uint64_t artbox_android_get_exported_namespace(const artbox_dlfcn *loader,const char *name) {
    // Even low byte cannot alias any odd library handle. Identity is never reused.
    return loader && name && !loader->namespace_name.empty() && loader->namespace_name==name
        ? loader->identity|UINT64_C(0x80) : 0;
}
uint64_t artbox_android_dlopen_ext(artbox_dlfcn *loader,artbox_dl_error *error,
    const char *name,unsigned flags,uint64_t extension_flags,uint64_t namespace_handle) {
    constexpr unsigned allowed=ARTBOX_RTLD_LAZY|ARTBOX_RTLD_NOW|ARTBOX_RTLD_NOLOAD|
        ARTBOX_RTLD_GLOBAL|ARTBOX_RTLD_NODELETE;
    if (!loader || (flags&~allowed)) { fail(error,"dlopen: invalid loader or flags");return 0; }
    if (extension_flags&~static_cast<uint64_t>(ARTBOX_DLEXT_USE_NAMESPACE)) {
        fail(error,"android_dlopen_ext: unsupported extension flags");return 0;
    }
    if ((extension_flags&ARTBOX_DLEXT_USE_NAMESPACE) && (loader->namespace_name.empty() ||
        namespace_handle!=(loader->identity|UINT64_C(0x80)))) {
        fail(error,"android_dlopen_ext: namespace does not belong to this signed group");return 0;
    }
    unsigned index=name?loader->find(name):0;
    if (name && index==loader->count) for (const auto &alias:loader->aliases)
        if (alias.path==name) { index=alias.index;break; }
    if (index==loader->count) { fail(error,"dlopen: library absent from the signed startup group");return 0; }
    try {
        std::lock_guard<std::mutex> lock(loader->mutex);
        if (loader->references[index]==UINT64_MAX) { fail(error,"dlopen: reference count exhausted");return 0; }
        ++loader->references[index];
        return loader->identity|(static_cast<uint64_t>(index)*2+1);
    } catch (const std::exception&) { fail(error,"dlopen: handle lock failed");return 0; }
}
int artbox_dlclose(artbox_dlfcn *loader,artbox_dl_error *error,uint64_t handle) {
    if (!loader) { fail(error,"dlclose: invalid loader");return -1; }
    try {
        std::lock_guard<std::mutex> lock(loader->mutex);
        unsigned index=loader->opened(handle);
        if (index==loader->count) { fail(error,"dlclose: invalid or closed handle");return -1; }
        --loader->references[index];return 0;
    } catch (const std::exception&) { fail(error,"dlclose: handle lock failed");return -1; }
}
uint64_t artbox_dlsym(artbox_dlfcn *loader,artbox_dl_error *error,uint64_t handle,
    const char *name,const char *version,uint64_t caller) {
    if (!loader || !name) { fail(error,"dlsym: invalid loader or symbol name");return 0; }
    uint64_t address=0;
    artbox_elf_result result;
    if (handle==ARTBOX_RTLD_DEFAULT) result=artbox_load_group_lookup(loader->group,name,version,&address);
    else if (handle==ARTBOX_RTLD_NEXT) result=artbox_load_group_lookup_next(loader->group,caller,name,version,&address);
    else {
        unsigned index;
        try {
            std::lock_guard<std::mutex> lock(loader->mutex);
            index=loader->opened(handle);
        } catch (const std::exception&) { fail(error,"dlsym: handle lock failed");return 0; }
        if (index==loader->count) { fail(error,"dlsym: invalid or closed handle");return 0; }
        result=artbox_load_group_lookup_from(loader->group,loader->images[index].name,name,version,&address);
    }
    if (result!=ARTBOX_ELF_OK) { fail(error,artbox_elf_result_string(result));return 0; }
    return address;
}
unsigned artbox_dlfcn_count(const artbox_dlfcn *loader) { return loader?loader->count:0; }
artbox_elf_result artbox_dlfcn_info(const artbox_dlfcn *loader,unsigned index,artbox_link_info *out) {
    if (!loader || !out) return ARTBOX_ELF_INVALID;
    if (index>=loader->count) return ARTBOX_ELF_NOT_FOUND;
    *out=loader->images[index];out->name=loader->names[index].c_str();return ARTBOX_ELF_OK;
}
artbox_elf_result artbox_dlfcn_address(const artbox_dlfcn *loader,uint64_t address,artbox_link_address *out) {
    if (!loader || !out) return ARTBOX_ELF_INVALID;
    artbox_link_address found;
    artbox_elf_result r=artbox_load_group_address(loader->group,address,&found);
    if (r!=ARTBOX_ELF_OK) return r;
    unsigned index=loader->find(found.image.name);
    if (index==loader->count) return ARTBOX_ELF_INVALID;
    found.image.name=loader->names[index].c_str();*out=found;return ARTBOX_ELF_OK;
}
