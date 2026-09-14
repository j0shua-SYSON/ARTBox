#include "artbox/linker.h"
#include <algorithm>
#include <array>
#include <cstring>
#include <exception>
#include <memory>
#include <string>
#include <vector>

namespace {
struct Node {
    artbox_link_module module{};
    std::vector<unsigned> dependencies;
    artbox_relocation_stats stats{};
    uint64_t tls_id=0;
};
bool name_valid(const char *name) {
    if (!name || !*name || !std::strcmp(name,".") || !std::strcmp(name,"..")) return false;
    size_t length=0;
    for (;name[length];++length) if (length==255 || name[length]=='/' || name[length]=='\\') return false;
    return true;
}
bool overlap(const void *a,size_t an,const void *b,size_t bn) {
    uintptr_t av=reinterpret_cast<uintptr_t>(a),bv=reinterpret_cast<uintptr_t>(b);
    return an && bn && (av<=bv ? bv-av<an : av-bv<bn);
}
uint64_t word(const unsigned char *p) {
    uint64_t result=0; for (unsigned i=0;i<8;++i) result|=static_cast<uint64_t>(p[i])<<(i*8); return result;
}
artbox_elf_result lookup(const Node &node,const char *name,const char *version,uint64_t *out) {
    artbox_elf_symbol symbol;
    artbox_elf_result result=artbox_dynamic_lookup_version(node.module.dynamic,name,version,&symbol);
    if (result!=ARTBOX_ELF_OK) return result;
    if (symbol.type>2 || symbol.binding>2) return ARTBOX_ELF_UNSUPPORTED;
    if (symbol.section==0xfff1) { *out=symbol.value; return ARTBOX_ELF_OK; }
    if (symbol.section>=0xff00) return ARTBOX_ELF_UNSUPPORTED;
    for (unsigned i=0;i<node.module.dynamic->image->segment_count;++i) {
        const auto &s=node.module.dynamic->image->segments[i];
        if (symbol.value>=s.virtual_address && symbol.value-s.virtual_address<=s.memory_size &&
            symbol.size<=s.memory_size-(symbol.value-s.virtual_address)) {
            *out=node.module.load_bias+symbol.value; return ARTBOX_ELF_OK;
        }
    }
    return ARTBOX_ELF_INVALID;
}
}
struct artbox_load_group {
    std::vector<Node> nodes;
    std::vector<unsigned> scope, initialization;
    std::vector<artbox_tls_template> tls_templates;
    std::vector<std::unique_ptr<std::array<uint64_t,2>>> tls_arguments;
    uint64_t tls_resolver=0;
    artbox_relocation_resolver host=nullptr;
    void *host_context=nullptr;
    enum State { created, relocating, relocated, initializing, ready, failed } state=created;

    unsigned find(const char *name) const {
        for (unsigned i=0;i<nodes.size();++i) if (!std::strcmp(nodes[i].module.name,name)) return i;
        return static_cast<unsigned>(nodes.size());
    }
    void visit(unsigned index,std::vector<bool> &visited) {
        if (visited[index]) return;
        visited[index]=true;
        for (unsigned dependency:nodes[index].dependencies) visit(dependency,visited);
        initialization.push_back(index);
    }
    bool executable(uint64_t address) const {
        if (address&3) return false;
        for (unsigned index:scope) {
            const auto &m=nodes[index].module;
            for (unsigned i=0;i<m.dynamic->image->segment_count;++i) {
                const auto &s=m.dynamic->image->segments[i];
                uint64_t base=m.load_bias+s.virtual_address;
                if ((s.flags&5)==5 && s.file_size>=4 && address>=base && address-base<=s.file_size-4) return true;
            }
        }
        return false;
    }
};

artbox_elf_result artbox_load_group_create(const artbox_link_module *modules,unsigned count,
    const char *root,artbox_relocation_resolver host,void *context,artbox_load_group **out) {
    if (!modules || !count || count>64 || !name_valid(root) || !out) return ARTBOX_ELF_INVALID;
    try {
        std::unique_ptr<artbox_load_group> group(new artbox_load_group);
        group->nodes.resize(count);group->host=host;group->host_context=context;
        for (unsigned i=0;i<count;++i) {
            const auto &m=modules[i];
            if (!name_valid(m.name) || !m.dynamic || !m.dynamic->image || !m.dynamic->image->data ||
                (m.dynamic->soname && std::strcmp(m.name,m.dynamic->soname)) ||
                m.memory_count>ARTBOX_ELF_MAX_SEGMENTS || (m.memory_count && !m.memory)) return ARTBOX_ELF_INVALID;
            for (unsigned j=0;j<i;++j) if (!std::strcmp(m.name,modules[j].name) || m.dynamic==modules[j].dynamic) return ARTBOX_ELF_INVALID;
            group->nodes[i].module=m;
        }
        unsigned first=group->find(root);
        if (first==count) return ARTBOX_ELF_NOT_FOUND;
        std::vector<bool> reachable(count,false);reachable[first]=true;group->scope.push_back(first);
        uint64_t total=0;
        for (size_t cursor=0;cursor<group->scope.size();++cursor) {
            unsigned index=group->scope[cursor];Node &node=group->nodes[index];
            const auto &m=node.module;const auto &d=*m.dynamic;
            if (d.preinit_array.size) return ARTBOX_ELF_UNSUPPORTED;
            for (unsigned n=0;n<d.image->segment_count;++n) {
                const auto &s=d.image->segments[n];
                if (s.virtual_address>UINT64_MAX-m.load_bias || s.memory_size>UINT64_MAX-m.load_bias-s.virtual_address)
                    return ARTBOX_ELF_INVALID;
            }
            for (unsigned n=0;n<m.memory_count;++n) {
                const auto &memory=m.memory[n];
                if (memory.segment_index>=d.image->segment_count || !memory.data || !memory.size ||
                    memory.size>UINTPTR_MAX-reinterpret_cast<uintptr_t>(memory.data)) return ARTBOX_ELF_INVALID;
                if (memory.size>UINT64_C(256)*1024*1024-total) return ARTBOX_ELF_UNSUPPORTED;
                total+=memory.size;
                for (unsigned j=0;j<count;++j) {
                    const auto &other=modules[j];const auto &image=*other.dynamic->image;
                    if (overlap(memory.data,memory.size,image.data,image.size)) return ARTBOX_ELF_INVALID;
                    for (unsigned k=0;k<other.memory_count;++k)
                        if ((index!=j || n!=k) && overlap(memory.data,memory.size,other.memory[k].data,other.memory[k].size))
                            return ARTBOX_ELF_INVALID;
                }
            }
            if (d.image->has_tls) {
                const auto &t=d.image->tls;
                uint64_t alignment=std::max(UINT64_C(1),t.alignment);
                if (!t.memory_size || t.memory_size>16*1024*1024 || alignment>1024*1024)
                    return ARTBOX_ELF_UNSUPPORTED;
                if ((alignment&(alignment-1)) || t.file_size>t.memory_size) return ARTBOX_ELF_INVALID;
                uint64_t init=0;
                for (unsigned n=0;n<m.memory_count;++n) {
                    const auto &mem=m.memory[n];uint64_t base=d.image->segments[mem.segment_index].virtual_address;
                    if (t.virtual_address>=base && t.virtual_address-base<=mem.size &&
                        t.file_size<=mem.size-(t.virtual_address-base)) {
                        init=reinterpret_cast<uintptr_t>(mem.data)+t.virtual_address-base;break;
                    }
                }
                if (!init && t.file_size) {
                    const void *bytes=nullptr;
                    if (artbox_elf_virtual_span(d.image,t.virtual_address,t.file_size,&bytes)!=ARTBOX_ELF_OK)
                        return ARTBOX_ELF_INVALID;
                    init=reinterpret_cast<uintptr_t>(bytes);
                }
                node.tls_id=group->tls_templates.size()+1;
                group->tls_templates.push_back({node.tls_id,init,t.file_size,t.memory_size,alignment,t.virtual_address%alignment});
            }
            for (unsigned n=0;n<d.needed_count;++n) {
                if (!name_valid(d.needed[n])) return ARTBOX_ELF_INVALID;
                unsigned dependency=group->find(d.needed[n]);
                if (dependency==count) return ARTBOX_ELF_NOT_FOUND;
                node.dependencies.push_back(dependency);
                if (!reachable[dependency]) { reachable[dependency]=true;group->scope.push_back(dependency); }
            }
            // Version provider names were checked against DT_NEEDED during
            // parsing; the graph above now requires those actual bundle entries.
        }
        std::vector<bool> visited(count,false);group->visit(first,visited);
        *out=group.release();return ARTBOX_ELF_OK;
    } catch (const std::exception&) { return ARTBOX_ELF_NO_MEMORY; }
}
void artbox_load_group_destroy(artbox_load_group *group) { delete group; }
unsigned artbox_load_group_tls_count(const artbox_load_group *g) { return g?static_cast<unsigned>(g->tls_templates.size()):0; }
artbox_elf_result artbox_load_group_tls_template(const artbox_load_group *g,unsigned index,artbox_tls_template *out) {
    if (!g || !out) return ARTBOX_ELF_INVALID;
    if (index>=g->tls_templates.size()) return ARTBOX_ELF_NOT_FOUND;
    *out=g->tls_templates[index];return ARTBOX_ELF_OK;
}
artbox_elf_result artbox_load_group_tls_resolver(artbox_load_group *g,uint64_t address) {
    if (!g || g->state!=artbox_load_group::created || !g->executable(address)) return ARTBOX_ELF_INVALID;
    g->tls_resolver=address;return ARTBOX_ELF_OK;
}
unsigned artbox_load_group_count(const artbox_load_group *group) { return group?static_cast<unsigned>(group->scope.size()):0; }
artbox_elf_result artbox_load_group_lookup(const artbox_load_group *group,const char *name,const char *version,uint64_t *address) {
    if (!group || !name || !address) return ARTBOX_ELF_INVALID;
    for (unsigned index:group->scope) {
        artbox_elf_result result=lookup(group->nodes[index],name,version,address);
        if (result!=ARTBOX_ELF_NOT_FOUND) return result;
    }
    return ARTBOX_ELF_NOT_FOUND;
}
static artbox_elf_result resolve(void *context,const artbox_dynamic *d,uint32_t index,uint64_t *address) {
    auto *group=static_cast<artbox_load_group*>(context);
    artbox_elf_symbol symbol;artbox_elf_version version;
    artbox_elf_result result=artbox_dynamic_symbol(d,index,&symbol);
    if (result!=ARTBOX_ELF_OK) return result;
    result=artbox_dynamic_version(d,index,&version);
    if (result!=ARTBOX_ELF_OK) return result;
    if (d->flags&2) {
        for (unsigned n:group->scope) if (group->nodes[n].module.dynamic==d) {
            result=lookup(group->nodes[n],symbol.name,version.name,address);
            if (result!=ARTBOX_ELF_NOT_FOUND) return result;
            break;
        }
    }
    result=artbox_load_group_lookup(group,symbol.name,version.name,address);
    if (result==ARTBOX_ELF_NOT_FOUND && group->host) result=group->host(group->host_context,d,index,address);
    return result;
}
static artbox_elf_result tls_symbol(const Node &node,const artbox_elf_symbol &s,uint64_t *id,uint64_t *offset) {
    if (s.type!=6 || s.binding>2 || s.section>=0xff00) return ARTBOX_ELF_UNSUPPORTED;
    if (!node.tls_id || !s.section || s.value>node.module.dynamic->image->tls.memory_size ||
        s.size>node.module.dynamic->image->tls.memory_size-s.value) return ARTBOX_ELF_INVALID;
    *id=node.tls_id;*offset=s.value;return ARTBOX_ELF_OK;
}
static artbox_elf_result bind_tls(void *context,const artbox_dynamic *d,uint32_t index,uint64_t addend,
    uint64_t *entry,uint64_t *argument) try {
    auto *g=static_cast<artbox_load_group*>(context);
    if (!g->tls_resolver) return ARTBOX_ELF_UNSUPPORTED;
    const Node *origin=nullptr;
    for (unsigned n:g->scope) if (g->nodes[n].module.dynamic==d) { origin=&g->nodes[n];break; }
    if (!origin) return ARTBOX_ELF_INVALID;
    uint64_t id=origin->tls_id, offset=0;
    if (index) {
        artbox_elf_symbol s;artbox_elf_version version;
        artbox_elf_result r=artbox_dynamic_symbol(d,index,&s);
        if (r!=ARTBOX_ELF_OK) return r;
        r=artbox_dynamic_version(d,index,&version);if (r!=ARTBOX_ELF_OK) return r;
        bool found=false;
        if (s.section && (!s.binding || s.visibility || (d->flags&2))) {
            r=tls_symbol(*origin,s,&id,&offset);if (r!=ARTBOX_ELF_OK) return r;found=true;
        }
        if (!found) for (unsigned n:g->scope) {
            artbox_elf_symbol candidate;
            r=artbox_dynamic_lookup_version(g->nodes[n].module.dynamic,s.name,version.name,&candidate);
            if (r==ARTBOX_ELF_NOT_FOUND) continue;
            if (r!=ARTBOX_ELF_OK) return r;
            r=tls_symbol(g->nodes[n],candidate,&id,&offset);
            if (r!=ARTBOX_ELF_OK) return r;
            found=true;break;
        }
        if (!found && s.section) {
            r=tls_symbol(*origin,s,&id,&offset);if (r!=ARTBOX_ELF_OK) return r;found=true;
        }
        if (!found) {
            if (s.binding!=2) return ARTBOX_ELF_NOT_FOUND;
            id=0;offset=0; // The bridge returns the addend for undefined weak TLS.
        }
    } else if (!id) return ARTBOX_ELF_INVALID;
    offset+=addend; // AAELF64 offsets retain the low 64 bits, including negative addends.
    if (g->tls_arguments.size()>=1024*1024) return ARTBOX_ELF_UNSUPPORTED;
    std::unique_ptr<std::array<uint64_t,2>> value(new std::array<uint64_t,2>{{id,offset}});
    *entry=g->tls_resolver;*argument=reinterpret_cast<uintptr_t>(value->data());
    g->tls_arguments.push_back(std::move(value));return ARTBOX_ELF_OK;
} catch (const std::exception&) { return ARTBOX_ELF_NO_MEMORY; }
artbox_elf_result artbox_load_group_relocate(artbox_load_group *group) {
    if (!group || group->state==artbox_load_group::failed || group->state==artbox_load_group::relocating) return ARTBOX_ELF_INVALID;
    if (group->state!=artbox_load_group::created) return ARTBOX_ELF_OK;
    struct Staging { std::vector<std::vector<unsigned char>> bytes;std::vector<artbox_relocation_memory> views;artbox_relocation_stats stats{}; };
    group->state=artbox_load_group::relocating;
    try {
        std::vector<Staging> staging(group->nodes.size());
        for (unsigned index:group->scope) {
            const auto &m=group->nodes[index].module;auto &stage=staging[index];
            stage.bytes.resize(m.memory_count);stage.views.resize(m.memory_count);
            for (unsigned n=0;n<m.memory_count;++n) {
                const auto &memory=m.memory[n];stage.bytes[n].assign(memory.data,memory.data+memory.size);
                stage.views[n]={memory.segment_index,stage.bytes[n].data(),stage.bytes[n].size()};
            }
            artbox_elf_result error=artbox_relocate_tls(m.dynamic,m.load_bias,stage.views.data(),m.memory_count,resolve,bind_tls,group,&stage.stats);
            if (error!=ARTBOX_ELF_OK) { group->tls_arguments.clear();group->state=artbox_load_group::created;return error; }
        }
        for (unsigned index:group->scope) {
            auto &node=group->nodes[index];const auto &stage=staging[index];
            for (unsigned n=0;n<node.module.memory_count;++n)
                std::memcpy(node.module.memory[n].data,stage.bytes[n].data(),stage.bytes[n].size());
            node.stats=stage.stats;
        }
        group->state=artbox_load_group::relocated;return ARTBOX_ELF_OK;
    } catch (const std::exception&) { group->tls_arguments.clear();group->state=artbox_load_group::created;return ARTBOX_ELF_NO_MEMORY; }
}
static bool array_word(const artbox_link_module &m,uint64_t address,uint64_t *value) {
    for (unsigned i=0;i<m.memory_count;++i) {
        const auto &memory=m.memory[i];uint64_t start=m.dynamic->image->segments[memory.segment_index].virtual_address;
        if (address>=start && memory.size>=8 && address-start<=memory.size-8) { *value=word(memory.data+static_cast<size_t>(address-start));return true; }
    }
    const void *data=nullptr;
    if (artbox_elf_virtual_span(m.dynamic->image,address,8,&data)!=ARTBOX_ELF_OK) return false;
    *value=word(static_cast<const unsigned char*>(data));return true;
}
artbox_elf_result artbox_load_group_initialize(artbox_load_group *group,artbox_elf_result (*invoke)(void*,uint64_t),void *context) {
    if (!group || !invoke) return ARTBOX_ELF_INVALID;
    if (group->state==artbox_load_group::ready || group->state==artbox_load_group::initializing) return ARTBOX_ELF_OK;
    if (group->state!=artbox_load_group::relocated) return ARTBOX_ELF_INVALID;
    try {
        std::vector<uint64_t> calls;
        for (unsigned index:group->initialization) {
            const auto &m=group->nodes[index].module;const auto &d=*m.dynamic;
            if (d.init) calls.push_back(m.load_bias+d.init);
            if (d.init_array.size/8>65536) return ARTBOX_ELF_UNSUPPORTED;
            for (uint64_t offset=0;offset<d.init_array.size;offset+=8) {
                uint64_t address=0;
                if (!array_word(m,d.init_array.address+offset,&address)) return ARTBOX_ELF_INVALID;
                if (address && address!=UINT64_MAX) calls.push_back(address);
            }
        }
        for (uint64_t address:calls) if (!group->executable(address)) return ARTBOX_ELF_INVALID;
        group->state=artbox_load_group::initializing;
        for (uint64_t address:calls) {
            artbox_elf_result error=invoke(context,address);
            if (error!=ARTBOX_ELF_OK) { group->state=artbox_load_group::failed;return error; }
        }
        group->state=artbox_load_group::ready;return ARTBOX_ELF_OK;
    } catch (const std::exception&) { group->state=artbox_load_group::failed;return ARTBOX_ELF_NO_MEMORY; }
}
artbox_elf_result artbox_load_group_stats(const artbox_load_group *group,const char *name,artbox_relocation_stats *out) {
    if (!group || !name || !out || group->state==artbox_load_group::created || group->state==artbox_load_group::relocating)
        return ARTBOX_ELF_INVALID;
    for (unsigned index:group->scope) if (!std::strcmp(group->nodes[index].module.name,name)) { *out=group->nodes[index].stats;return ARTBOX_ELF_OK; }
    return ARTBOX_ELF_NOT_FOUND;
}
