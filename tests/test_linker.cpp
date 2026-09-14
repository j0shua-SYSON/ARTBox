#include "artbox/linker.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#define CHECK(c) do { if (!(c)) { std::fprintf(stderr, "%d: %s\n", __LINE__, #c); std::exit(1); } } while (0)
static void put(unsigned char *p, uint64_t value, unsigned width) { for (unsigned i=0; i<width; ++i) p[i]=static_cast<unsigned char>(value>>(i*8)); }
static uint64_t word(const unsigned char *p) { uint64_t v=0; for (unsigned i=0; i<8; ++i) v|=static_cast<uint64_t>(p[i])<<(i*8); return v; }
struct Symbol { const char *name; bool defined, weak; };
struct Module {
    std::vector<unsigned char> bytes, rw;
    std::string name;
    artbox_elf elf{}; artbox_dynamic dynamic{}; artbox_relocation_memory memory{}; artbox_link_module view{};
    Module(const char *n, uint64_t bias, std::vector<const char*> needed, std::vector<Symbol> symbols,
           const char *reference=nullptr, bool symbolic=false) : bytes(8192), name(n) {
        auto p=[&](size_t at,uint64_t v,unsigned width=8) { put(bytes.data()+at,v,width); };
        std::memcpy(bytes.data(), "\177ELF\2\1\1", 7);
        p(16,3,2);p(18,183,2);p(20,1,4);p(32,64);p(52,64,2);p(54,56,2);p(56,3,2);
        auto ph=[&](size_t at,unsigned type,unsigned flags,uint64_t off,uint64_t va,uint64_t size,uint64_t align) {
            p(at,type,4);p(at+4,flags,4);p(at+8,off);p(at+16,va);p(at+32,size);p(at+40,size);p(at+48,align);
        };
        ph(64,1,5,0,0,4096,4096);ph(120,1,6,4096,16384,4096,4096);ph(176,2,6,4096,16384,512,8);
        size_t string_size=1, tags=0;
        auto string=[&](const char *text) { size_t offset=string_size, length=std::strlen(text)+1; std::memcpy(bytes.data()+0x300+offset,text,length); string_size+=length; return offset; };
        auto tag=[&](uint64_t key,uint64_t value) { p(0x1000+tags*16,key);p(0x1008+tags*16,value);++tags; };
        tag(14,string(n));for (const char *dependency:needed) tag(1,string(dependency));
        unsigned index=1, relocations=0;
        for (const auto &s:symbols) {
            size_t at=0x600+index*24;
            p(at,string(s.name),4);p(at+4,s.weak?0x21:0x11,1);p(at+6,s.defined?1:0,2);
            if (s.defined) { p(at+8,0x4500+index*8);p(at+16,8); }
            if (!s.defined || (reference && !std::strcmp(reference,s.name))) {
                size_t relocation=0x1400+relocations*24;
                p(relocation,0x4500+index*8);p(relocation+8,(static_cast<uint64_t>(index)<<32)|1025);++relocations;
            }
            p(0xa0c+index*4,index<symbols.size()?index+1:0,4);++index;
        }
        p(0xa00,1,4);p(0xa04,index,4);p(0xa08,symbols.empty()?0:1,4);
        tag(5,0x300);tag(10,string_size);tag(6,0x600);tag(11,24);tag(4,0xa00);tag(30,symbolic?2:0);
        size_t rel=0x1400+relocations*24;p(rel,0x4600);p(rel+8,1027);p(rel+16,0x204);++relocations;
        tag(7,0x4400);tag(8,relocations*24);tag(9,24);tag(12,0x200);tag(25,0x4600);tag(27,16);
        p(0x1608,UINT64_MAX);
        CHECK(artbox_elf_open(bytes.data(),bytes.size(),&elf)==ARTBOX_ELF_OK);
        CHECK(artbox_dynamic_open(&elf,&dynamic)==ARTBOX_ELF_OK);
        rw.assign(bytes.begin()+4096,bytes.end());
        memory={1,rw.data(),rw.size()};view={name.c_str(),&dynamic,bias,&memory,1};
    }
};
static unsigned bridges;
static artbox_elf_result host(void*,const artbox_dynamic *d,uint32_t index,uint64_t *address) {
    artbox_elf_symbol s; CHECK(artbox_dynamic_symbol(d,index,&s)==ARTBOX_ELF_OK);
    if (std::strcmp(s.name,"bridge")) return ARTBOX_ELF_NOT_FOUND;
    ++bridges;*address=0xabcdef;return ARTBOX_ELF_OK;
}
struct Initializers { artbox_load_group *group; std::vector<uint64_t> addresses; bool fail=false; };
static artbox_elf_result invoke(void *context,uint64_t address) {
    auto &init=*static_cast<Initializers*>(context);init.addresses.push_back(address);
    if (init.fail) return ARTBOX_ELF_UNSUPPORTED;
    CHECK(artbox_load_group_initialize(init.group,invoke,context)==ARTBOX_ELF_OK);
    return ARTBOX_ELF_OK;
}
int main() {
    Module root("root.so",0x100000,{"left.so","right.so"},{{"root",true,false},{"shared",false,false},{"optional",false,true},{"bridge",false,false}});
    Module left("left.so",0x200000,{"leaf.so"},{{"shared",true,true},{"root",false,false}});
    Module right("right.so",0x300000,{"leaf.so"},{{"shared",true,false}});
    Module leaf("leaf.so",0x400000,{"root.so"},{});
    Module excluded("excluded.so",0x500000,{},{{"only_excluded",true,false}});
    artbox_link_module modules[]={right.view,leaf.view,excluded.view,root.view,left.view};
    artbox_load_group *group=nullptr;
    CHECK(artbox_load_group_create(modules,5,"root.so",host,nullptr,&group)==ARTBOX_ELF_OK && group);
    CHECK(artbox_load_group_count(group)==4);
    uint64_t address=0;
    CHECK(artbox_load_group_lookup(group,"shared",nullptr,&address)==ARTBOX_ELF_OK && address==0x204508);
    CHECK(artbox_load_group_lookup(group,"only_excluded",nullptr,&address)==ARTBOX_ELF_NOT_FOUND);
    Initializers init{group,{},false};
    CHECK(artbox_load_group_initialize(group,invoke,&init)==ARTBOX_ELF_INVALID && init.addresses.empty());
    CHECK(artbox_load_group_relocate(group)==ARTBOX_ELF_OK);
    CHECK(word(root.rw.data()+0x510)==0x204508 && word(root.rw.data()+0x518)==0 && word(root.rw.data()+0x520)==0xabcdef);
    CHECK(word(left.rw.data()+0x510)==0x104508 && bridges==1);
    auto relocated=root.rw;
    CHECK(artbox_load_group_relocate(group)==ARTBOX_ELF_OK && root.rw==relocated && bridges==1);
    put(root.rw.data()+0x600,0xabcdef,8);
    CHECK(artbox_load_group_initialize(group,invoke,&init)==ARTBOX_ELF_INVALID && init.addresses.empty());
    put(root.rw.data()+0x600,0x100204,8);
    CHECK(artbox_load_group_initialize(group,invoke,&init)==ARTBOX_ELF_OK);
    CHECK((init.addresses==std::vector<uint64_t>{0x400200,0x400204,0x200200,0x200204,0x300200,0x300204,0x100200,0x100204}));
    CHECK(artbox_load_group_initialize(group,invoke,&init)==ARTBOX_ELF_OK && init.addresses.size()==8);
    artbox_relocation_stats stats{};
    CHECK(artbox_load_group_stats(group,"root.so",&stats)==ARTBOX_ELF_OK && stats.rela_count==4);
    artbox_load_group_destroy(group);

    Module bad("bad.so",0x600000,{"right.so"},{{"shared",false,false}});
    Module fresh("right.so",0x700000,{},{{"shared",true,false},{"unresolved",false,false}});
    artbox_link_module failure[]={bad.view,fresh.view};auto a=bad.rw,b=fresh.rw;
    CHECK(artbox_load_group_create(failure,2,"bad.so",nullptr,nullptr,&group)==ARTBOX_ELF_OK);
    CHECK(artbox_load_group_relocate(group)==ARTBOX_ELF_NOT_FOUND && bad.rw==a && fresh.rw==b);
    artbox_load_group_destroy(group);
    group=nullptr;
    CHECK(artbox_load_group_create(failure,1,"bad.so",nullptr,nullptr,&group)==ARTBOX_ELF_NOT_FOUND && !group);
    failure[1].name="bad.so";
    CHECK(artbox_load_group_create(failure,2,"bad.so",nullptr,nullptr,&group)==ARTBOX_ELF_INVALID);

    for (bool symbolic:{false,true}) {
        Module parent("parent.so",0x900000,{"child.so"},{{"same",true,false}});
        Module child("child.so",0xa00000,{},{{"same",true,false}},"same",symbolic);
        artbox_link_module pair[]={parent.view,child.view};
        CHECK(artbox_load_group_create(pair,2,"parent.so",nullptr,nullptr,&group)==ARTBOX_ELF_OK);
        CHECK(artbox_load_group_relocate(group)==ARTBOX_ELF_OK);
        CHECK(word(child.rw.data()+0x508)==(symbolic?0xa04508u:0x904508u));
        artbox_load_group_destroy(group);
    }

    Module poison("poison.so",0x800000,{},{});
    CHECK(artbox_load_group_create(&poison.view,1,"poison.so",nullptr,nullptr,&group)==ARTBOX_ELF_OK);
    CHECK(artbox_load_group_relocate(group)==ARTBOX_ELF_OK);
    Initializers failed{group,{},true};
    CHECK(artbox_load_group_initialize(group,invoke,&failed)==ARTBOX_ELF_UNSUPPORTED && failed.addresses.size()==1);
    CHECK(artbox_load_group_initialize(group,invoke,&failed)==ARTBOX_ELF_INVALID && failed.addresses.size()==1);
    artbox_load_group_destroy(group);
    std::puts("Manifest dependencies, BFS scope, weak binding, cycles, atomic relocation and constructor lifecycle passed");
}
