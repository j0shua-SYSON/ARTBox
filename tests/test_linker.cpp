#include "artbox/linker.h"
#include "artbox/dlfcn.h"
#include "artbox/guest_dlfcn.h"
#include "artbox/native_vm.h"
#include "artbox/native_dlfcn.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <thread>
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
static void tls_module(Module &m, bool defined) {
    auto p=[&](size_t at,uint64_t v,unsigned width=8) { put(m.bytes.data()+at,v,width); };
    if (defined) {
        p(56,4,2);p(232,7,4);p(236,4,4);p(240,0x1800);p(248,0x4800);
        p(264,8);p(272,64);p(280,32);p(0x1800,0x1234);
    }
    p(0x61c,0x16,1);p(0x620,defined?8:0);p(0x628,8);
    p(0x1400,0x4500);p(0x1408,UINT64_C(1)<<32|1031);p(0x1410,7);
    CHECK(artbox_elf_open(m.bytes.data(),m.bytes.size(),&m.elf)==ARTBOX_ELF_OK);
    CHECK(artbox_dynamic_open(&m.elf,&m.dynamic)==ARTBOX_ELF_OK);
    std::copy(m.bytes.begin()+4096,m.bytes.end(),m.rw.begin());
}
static void tls_tests() {
    Module root("tls-client.so",0x100000,{"tls-provider.so"},{{"tls",false,false}});
    Module provider("tls-provider.so",0x200000,{},{{"tls",true,false}},"tls");
    tls_module(root,false);tls_module(provider,true);
    artbox_link_module pair[]={provider.view,root.view};artbox_load_group *g=nullptr;
    CHECK(artbox_load_group_create(pair,2,"tls-client.so",nullptr,nullptr,&g)==ARTBOX_ELF_OK);
    CHECK(artbox_load_group_tls_count(g)==1);
    artbox_tls_template t{};
    CHECK(artbox_load_group_tls_template(g,0,&t)==ARTBOX_ELF_OK && t.module_id==1 &&
          t.init_data==reinterpret_cast<uintptr_t>(provider.rw.data()+0x800) &&
          t.init_size==8 && t.memory_size==64 && t.alignment==32 && t.skew==0);
    CHECK(artbox_load_group_tls_template(g,1,&t)==ARTBOX_ELF_NOT_FOUND);
    artbox_link_info image{};
    CHECK(artbox_load_group_info(g,0,&image)==ARTBOX_ELF_OK && image.tls_module_id==0);
    CHECK(artbox_load_group_info(g,1,&image)==ARTBOX_ELF_OK && image.tls_module_id==1);
    auto before=root.rw;
    CHECK(artbox_load_group_relocate(g)==ARTBOX_ELF_UNSUPPORTED && root.rw==before);
    CHECK(artbox_load_group_tls_resolver(g,0x204800)==ARTBOX_ELF_INVALID);
    CHECK(artbox_load_group_tls_resolver(g,0x200200)==ARTBOX_ELF_OK);
    CHECK(artbox_load_group_relocate(g)==ARTBOX_ELF_OK);
    for (const auto *m:{&root,&provider}) {
        CHECK(word(m->rw.data()+0x500)==0x200200);
        const auto *index=reinterpret_cast<const uint64_t*>(static_cast<uintptr_t>(word(m->rw.data()+0x508)));
        CHECK(index && index[0]==1 && index[1]==15);
    }
    uint64_t address=0;
    CHECK(artbox_load_group_lookup(g,"tls",nullptr,&address)==ARTBOX_ELF_UNSUPPORTED);
    CHECK(artbox_load_group_lookup_from(g,"tls-provider.so","tls",nullptr,&address)==ARTBOX_ELF_UNSUPPORTED);
    artbox_link_address located{};
    CHECK(artbox_load_group_address(g,0x200008,&located)==ARTBOX_ELF_OK && !located.symbol_name);
    CHECK(artbox_load_group_tls_resolver(g,0x200200)==ARTBOX_ELF_INVALID);
    artbox_load_group_destroy(g);
    /* A late error cannot publish an earlier module's TLS descriptor. */
    Module missing("missing.so",0x300000,{"bad-tls.so"},{{"tls",false,false}});
    Module bad("bad-tls.so",0x400000,{},{{"tls",true,false},{"absent",false,false}},"tls");
    tls_module(missing,false);tls_module(bad,true);
    artbox_link_module failure[]={missing.view,bad.view};before=missing.rw;auto other=bad.rw;
    CHECK(artbox_load_group_create(failure,2,"missing.so",nullptr,nullptr,&g)==ARTBOX_ELF_OK);
    CHECK(artbox_load_group_tls_resolver(g,0x400200)==ARTBOX_ELF_OK);
    CHECK(artbox_load_group_relocate(g)==ARTBOX_ELF_NOT_FOUND && missing.rw==before && bad.rw==other);
    artbox_load_group_destroy(g);
}
static artbox_elf_result invoke(void *context,uint64_t address) {
    auto &init=*static_cast<Initializers*>(context);init.addresses.push_back(address);
    if (init.fail) return ARTBOX_ELF_UNSUPPORTED;
    CHECK(artbox_load_group_initialize(init.group,invoke,context)==ARTBOX_ELF_OK);
    return ARTBOX_ELF_OK;
}
static void query_tests() {
    Module root("root.so",0x100000,{"left.so","right.so"},{});
    Module left("left.so",0x200000,{"leaf.so"},{{"shared",true,true}});
    Module right("right.so",0x300000,{"leaf.so"},{{"shared",true,false},{"right_only",true,false}});
    Module leaf("leaf.so",0x400000,{"left.so"},{{"leaf",true,false}});
    Module excluded("excluded.so",0x500000,{},{{"excluded",true,false}});
    artbox_link_module modules[]={excluded.view,right.view,root.view,leaf.view,left.view};
    artbox_load_group *g=nullptr;
    CHECK(artbox_load_group_create(modules,5,"root.so",nullptr,nullptr,&g)==ARTBOX_ELF_OK);
    uint64_t address=0xdead;
    CHECK(artbox_load_group_lookup_from(g,"root.so","shared",nullptr,&address)==ARTBOX_ELF_OK && address==0x204508);
    CHECK(artbox_load_group_lookup_from(g,"right.so","shared",nullptr,&address)==ARTBOX_ELF_OK && address==0x304508);
    CHECK(artbox_load_group_lookup_from(g,"left.so","leaf",nullptr,&address)==ARTBOX_ELF_OK && address==0x404508);
    address=0xdead;
    CHECK(artbox_load_group_lookup_from(g,"left.so","right_only",nullptr,&address)==ARTBOX_ELF_NOT_FOUND && address==0xdead);
    CHECK(artbox_load_group_lookup_from(g,"excluded.so","excluded",nullptr,&address)==ARTBOX_ELF_NOT_FOUND && address==0xdead);
    CHECK(artbox_load_group_lookup_from(g,"absent.so","shared",nullptr,&address)==ARTBOX_ELF_NOT_FOUND);
    CHECK(artbox_load_group_lookup_from(g,nullptr,"shared",nullptr,&address)==ARTBOX_ELF_INVALID);
    CHECK(artbox_load_group_lookup_from(g,"left.so",nullptr,nullptr,&address)==ARTBOX_ELF_INVALID);
    CHECK(artbox_load_group_lookup_next(g,0x100200,"shared",nullptr,&address)==ARTBOX_ELF_OK && address==0x204508);
    CHECK(artbox_load_group_lookup_next(g,0x200200,"shared",nullptr,&address)==ARTBOX_ELF_OK && address==0x304508);
    CHECK(artbox_load_group_lookup_next(g,0x300200,"leaf",nullptr,&address)==ARTBOX_ELF_OK && address==0x404508);
    address=0xdead;
    CHECK(artbox_load_group_lookup_next(g,0x400200,"shared",nullptr,&address)==ARTBOX_ELF_NOT_FOUND && address==0xdead);
    CHECK(artbox_load_group_lookup_next(g,0x201000,"shared",nullptr,&address)==ARTBOX_ELF_NOT_FOUND);
    CHECK(artbox_load_group_lookup_next(g,0x500200,"shared",nullptr,&address)==ARTBOX_ELF_NOT_FOUND);
    CHECK(artbox_load_group_lookup_next(g,UINT64_MAX,"shared",nullptr,&address)==ARTBOX_ELF_NOT_FOUND);
    CHECK(artbox_load_group_lookup_next(g,0x200200,nullptr,nullptr,&address)==ARTBOX_ELF_INVALID);
    artbox_link_info info{};
    const char *order[]={"root.so","left.so","right.so","leaf.so"};
    for (unsigned i=0;i<4;++i) {
        CHECK(artbox_load_group_info(g,i,&info)==ARTBOX_ELF_OK);
        CHECK(!std::strcmp(info.name,order[i]) && info.load_bias==(i+1)*0x100000 && !info.tls_module_id);
        CHECK(info.dynamic->image->program_header_count==3);
    }
    const auto *old_dynamic=info.dynamic;
    CHECK(artbox_load_group_info(g,4,&info)==ARTBOX_ELF_NOT_FOUND && info.dynamic==old_dynamic);
    CHECK(artbox_load_group_info(nullptr,0,&info)==ARTBOX_ELF_INVALID);
    CHECK(artbox_load_group_info(g,0,nullptr)==ARTBOX_ELF_INVALID);
    artbox_link_address found{};
    CHECK(artbox_load_group_address(g,0x20450b,&found)==ARTBOX_ELF_OK);
    CHECK(found.image.dynamic==&left.dynamic && found.image.load_bias==0x200000 &&
          !std::strcmp(found.symbol_name,"shared") && found.symbol_address==0x204508);
    CHECK(artbox_load_group_address(g,0x204510,&found)==ARTBOX_ELF_OK && !found.symbol_name && !found.symbol_address);
    CHECK(artbox_load_group_address(g,0x200000,&found)==ARTBOX_ELF_OK && !found.symbol_name);
    CHECK(artbox_load_group_address(g,0x201000,&found)==ARTBOX_ELF_NOT_FOUND && found.image.dynamic==&left.dynamic);
    CHECK(artbox_load_group_address(g,0x205000,&found)==ARTBOX_ELF_NOT_FOUND);
    CHECK(artbox_load_group_address(g,0x504508,&found)==ARTBOX_ELF_NOT_FOUND);
    CHECK(artbox_load_group_address(g,UINT64_MAX,&found)==ARTBOX_ELF_NOT_FOUND);
    CHECK(artbox_load_group_address(g,0x204508,nullptr)==ARTBOX_ELF_INVALID);
    CHECK(artbox_load_group_address(nullptr,0x204508,&found)==ARTBOX_ELF_INVALID);
    artbox_load_group_destroy(g);

    Module special("special.so",0x600000,{},{{"absolute",true,false},{"zero",true,false},{"ordinary",true,false}});
    put(special.bytes.data()+0x61e,0xfff1,2);put(special.bytes.data()+0x620,0,8);
    put(special.bytes.data()+0x640,0,8); // second symbol has zero size
    CHECK(artbox_load_group_create(&special.view,1,"special.so",nullptr,nullptr,&g)==ARTBOX_ELF_OK);
    CHECK(artbox_load_group_lookup_from(g,"special.so","absolute",nullptr,&address)==ARTBOX_ELF_OK && address==0);
    CHECK(artbox_load_group_address(g,0x600001,&found)==ARTBOX_ELF_OK && !found.symbol_name);
    CHECK(artbox_load_group_address(g,0x604510,&found)==ARTBOX_ELF_OK && !found.symbol_name);
    CHECK(artbox_load_group_address(g,0x604518,&found)==ARTBOX_ELF_OK && !std::strcmp(found.symbol_name,"ordinary"));
    artbox_load_group_destroy(g);
}
static void dlfcn_tests() {
    Module root("root.so",0x100000,{"libc.so"},{{"shared",true,false}});
    Module libc("libc.so",0x200000,{},{{"shared",true,false},{"zero",true,false}});
    Module excluded("excluded.so",0x300000,{},{{"absent",true,false}});
    put(libc.bytes.data()+0x636,0xfff1,2);put(libc.bytes.data()+0x638,0,8);
    artbox_link_module modules[]={excluded.view,libc.view,root.view};
    artbox_load_group *group=nullptr;
    CHECK(artbox_load_group_create(modules,3,"root.so",nullptr,nullptr,&group)==ARTBOX_ELF_OK);
    artbox_dlfcn *premature=nullptr;
    CHECK(artbox_dlfcn_create(group,nullptr,0,&premature)==ARTBOX_ELF_INVALID && !premature);
    CHECK(artbox_load_group_relocate(group)==ARTBOX_ELF_OK);
    artbox_dl_alias alias{"/system/lib64/libc.so","libc.so"};
    artbox_dlfcn *loader=nullptr;
    CHECK(artbox_dlfcn_create(group,&alias,1,&loader)==ARTBOX_ELF_OK);
    artbox_dl_alias bad{"/system/excluded.so","excluded.so"};
    CHECK(artbox_dlfcn_create(group,&bad,1,&premature)==ARTBOX_ELF_NOT_FOUND && !premature);
    bad={"relative/libc.so","libc.so"};
    CHECK(artbox_dlfcn_create(group,&bad,1,&premature)==ARTBOX_ELF_INVALID && !premature);
    artbox_dl_alias duplicate[]={alias,alias};
    CHECK(artbox_dlfcn_create(group,duplicate,2,&premature)==ARTBOX_ELF_INVALID && !premature);
    artbox_dl_error error{};
    CHECK(!artbox_dlerror(&error));
    uint64_t handle=artbox_dlopen(loader,&error,"libc.so",ARTBOX_RTLD_NOW);
    CHECK(handle && handle!=ARTBOX_RTLD_NEXT && !artbox_dlerror(&error));
    CHECK(artbox_dlopen(loader,&error,alias.path,ARTBOX_RTLD_LAZY|ARTBOX_RTLD_NOLOAD)==handle);
    CHECK(artbox_dlsym(loader,&error,handle,"shared",nullptr,0)==0x204508);
    CHECK(artbox_dlsym(loader,&error,ARTBOX_RTLD_DEFAULT,"shared",nullptr,0)==0x104508);
    CHECK(artbox_dlsym(loader,&error,ARTBOX_RTLD_NEXT,"shared",nullptr,0x100200)==0x204508);
    CHECK(!artbox_dlsym(loader,&error,handle,"zero",nullptr,0) && !artbox_dlerror(&error));
    CHECK(!artbox_dlopen(loader,&error,"/wrong/libc.so",ARTBOX_RTLD_NOW));
    CHECK(artbox_dlerror(&error) && !artbox_dlerror(&error));
    CHECK(!artbox_dlopen(loader,&error,"excluded.so",ARTBOX_RTLD_NOW));
    CHECK(artbox_dlsym(loader,&error,handle,"shared",nullptr,0)==0x204508);
    CHECK(artbox_dlerror(&error) && !artbox_dlerror(&error)); // success retains unread error
    CHECK(!artbox_dlopen(loader,&error,"libc.so",0x80000000));
    CHECK(artbox_dlerror(&error));
    CHECK(artbox_dlclose(loader,&error,handle)==0);
    CHECK(artbox_dlsym(loader,&error,handle,"shared",nullptr,0)==0x204508);
    CHECK(artbox_dlclose(loader,&error,handle)==0);
    CHECK(artbox_dlclose(loader,&error,handle)==-1 && artbox_dlerror(&error));
    CHECK(!artbox_dlsym(loader,&error,handle,"shared",nullptr,0) && artbox_dlerror(&error));
    CHECK(artbox_dlclose(loader,&error,ARTBOX_RTLD_NEXT)==-1 && artbox_dlerror(&error));
    uint64_t main=artbox_dlopen(loader,&error,nullptr,ARTBOX_RTLD_NOW|ARTBOX_RTLD_GLOBAL|ARTBOX_RTLD_NODELETE);
    CHECK(main && artbox_dlsym(loader,&error,main,"shared",nullptr,0)==0x104508);
    artbox_dlfcn *other=nullptr;
    CHECK(artbox_dlfcn_create(group,nullptr,0,&other)==ARTBOX_ELF_OK);
    CHECK(!artbox_dlsym(other,&error,main,"shared",nullptr,0) && artbox_dlerror(&error));
    artbox_dlfcn_destroy(other);
    CHECK(artbox_dlclose(loader,&error,main)==0);
    CHECK(artbox_dlfcn_count(loader)==2);
    artbox_link_info info{};artbox_link_address address{};
    CHECK(artbox_dlfcn_info(loader,1,&info)==ARTBOX_ELF_OK && !std::strcmp(info.name,alias.path));
    CHECK(artbox_dlfcn_address(loader,0x204509,&address)==ARTBOX_ELF_OK && !std::strcmp(address.image.name,alias.path));
    CHECK(!artbox_dlopen(loader,&error,"missing-main.so",ARTBOX_RTLD_NOW));
    std::thread worker([&] {
        artbox_dl_error own{};
        CHECK(!artbox_dlerror(&own));
        for (unsigned i=0;i<100;++i) {
            uint64_t h=artbox_dlopen(loader,&own,"libc.so",ARTBOX_RTLD_NOW);
            CHECK(h && artbox_dlsym(loader,&own,h,"shared",nullptr,0)==0x204508);
            CHECK(artbox_dlclose(loader,&own,h)==0);
        }
        CHECK(!artbox_dlsym(loader,&own,ARTBOX_RTLD_DEFAULT,"missing-worker",nullptr,0));
        CHECK(artbox_dlerror(&own) && !artbox_dlerror(&own));
    });
    for (unsigned i=0;i<100;++i) {
        uint64_t h=artbox_dlopen(loader,&error,"libc.so",ARTBOX_RTLD_NOLOAD);
        CHECK(h && artbox_dlclose(loader,&error,h)==0);
    }
    worker.join();
    CHECK(artbox_dlerror(&error) && !artbox_dlerror(&error));
    artbox_dlfcn_destroy(loader);
    artbox_load_group_destroy(group);
}
struct GuestLoaderCallback {
    artbox_vm *vm;
    artbox_guest_dl_thread *thread;
    uint64_t base,symbol;
    unsigned calls;
};
static artbox_elf_result guest_loader_callback(void *context,uint64_t function,
    const uint64_t arguments[3],uint64_t *result) {
    auto &c=*static_cast<GuestLoaderCallback*>(context);
    CHECK(function==c.base+0x204 && arguments[1]==64 && arguments[2]==0xabc);
    unsigned char bytes[64];
    CHECK(!artbox_vm_read(c.vm,arguments[0],bytes,sizeof(bytes)));
    CHECK(word(bytes)==c.base && word(bytes+16)==c.base+64 && word(bytes+24)==3);
    CHECK(word(bytes+32)==1 && !word(bytes+40) && !word(bytes+48) && !word(bytes+56));
    CHECK(artbox_vm_access(c.vm,word(bytes+8),1,1) && !artbox_vm_access(c.vm,word(bytes+8),1,2));
    CHECK(artbox_guest_dlsym(c.thread,ARTBOX_RTLD_DEFAULT,c.symbol,0,function)==c.base+0x4508);
    ++c.calls;*result=37;return ARTBOX_ELF_OK;
}
static void guest_dlfcn_tests() {
    artbox_vm_ops ops=artbox_native_vm();
    artbox_vm *vm=artbox_vm_create(&ops,64*1024*1024,128);CHECK(vm);
    uint64_t length=5*artbox_vm_page_size(vm);
    int64_t mapping=artbox_vm_mmap(vm,0,length,3,0x22,-1,0);CHECK(mapping>0);
    uint64_t base=static_cast<uint64_t>(mapping);
    // Synthetic ELF metadata in non-executable storage. The callback above
    // checks marshalled bytes; actual signed execution has its own Mac fixture.
    Module module("fixture.so",base,{},{{"value",true,false}});
    artbox_load_group *group=nullptr;
    CHECK(artbox_load_group_create(&module.view,1,"fixture.so",nullptr,nullptr,&group)==ARTBOX_ELF_OK);
    CHECK(artbox_load_group_relocate(group)==ARTBOX_ELF_OK);
    CHECK(!artbox_vm_write(vm,base,module.bytes.data(),4096));
    CHECK(!artbox_vm_write(vm,base+0x4000,module.rw.data(),module.rw.size()));
    artbox_elf_symbol symbol;
    CHECK(artbox_dynamic_lookup(&module.dynamic,"value",&symbol)==ARTBOX_ELF_OK);
    uint64_t symbol_name=base+static_cast<uint64_t>(reinterpret_cast<const unsigned char*>(symbol.name)-module.bytes.data());
    uint64_t library_name=base+static_cast<uint64_t>(reinterpret_cast<const unsigned char*>(module.dynamic.soname)-module.bytes.data());
    artbox_dlfcn *loader=nullptr;
    artbox_dl_alias alias{"/system/lib64/fixture.so","fixture.so"};
    CHECK(artbox_dlfcn_create(group,&alias,1,&loader)==ARTBOX_ELF_OK);
    GuestLoaderCallback callback{vm,nullptr,base,symbol_name,0};
    const artbox_guest_dl_ops guest_ops{&callback,guest_loader_callback,nullptr};
    artbox_guest_dlfcn *service=nullptr;
    CHECK(artbox_guest_dlfcn_create(loader,group,vm,&guest_ops,&service)==ARTBOX_ELF_OK);
    artbox_guest_dl_thread *thread=nullptr,*other=nullptr;
    CHECK(artbox_guest_dl_thread_create(service,&thread)==ARTBOX_ELF_OK);
    CHECK(artbox_guest_dl_thread_create(service,&other)==ARTBOX_ELF_OK);callback.thread=thread;
    uint64_t handle=artbox_guest_dlopen(thread,library_name,ARTBOX_RTLD_NOW);
    CHECK(handle && !artbox_guest_dlerror(thread));
    CHECK(artbox_guest_dlsym(thread,handle,symbol_name,0,base+0x204)==base+0x4508);
    CHECK(artbox_guest_dladdr(thread,base+0x4509,base+0x4700)==1);
    unsigned char location[32];
    CHECK(!artbox_vm_read(vm,base+0x4700,location,sizeof(location)));
    CHECK(word(location+8)==base && word(location+16)==symbol_name && word(location+24)==base+0x4508);
    char name[sizeof("/system/lib64/fixture.so")];
    CHECK(!artbox_vm_read(vm,word(location),name,sizeof(name)) && !std::strcmp(name,alias.path));
    CHECK(!artbox_vm_access(vm,word(location),sizeof(name),2));
    uint64_t reserved=artbox_vm_reserved_bytes(vm);
    CHECK(artbox_guest_dl_iterate_phdr(thread,base+0x204,0xabc)==37 && callback.calls==1);
    CHECK(artbox_vm_reserved_bytes(vm)==reserved);
    CHECK(artbox_guest_dl_iterate_phdr(thread,base+0x4508,0)==-1 && callback.calls==1);
    uint64_t error=artbox_guest_dlerror(thread);CHECK(error && !artbox_guest_dlerror(thread));
    CHECK(!artbox_guest_dlopen(other,UINT64_MAX,ARTBOX_RTLD_NOW));
    uint64_t other_error=artbox_guest_dlerror(other);CHECK(other_error && other_error!=error);
    CHECK(!artbox_guest_dlerror(thread));
    CHECK(!artbox_guest_dlsym(thread,handle,symbol_name,UINT64_MAX,base+0x204));
    CHECK(artbox_guest_dlerror(thread)==error);
    CHECK(!artbox_guest_dladdr(thread,base+0x4508,UINT64_MAX) && artbox_guest_dlerror(thread)==error);
    // Exercise the native TLS binding and the versioned entry point, which the
    // signed 32-case caller does not import. No synthetic ARM64 code is run.
    Module frontend("libdl.so",0x100000,{},{{"__loader_dlvsym",false,true},{"__loader_dlerror",false,true}});
    uint64_t version_entry=0,error_entry=0;
    CHECK(artbox_native_dlfcn_resolve(nullptr,&frontend.dynamic,1,&version_entry)==ARTBOX_ELF_OK);
    CHECK(artbox_native_dlfcn_resolve(nullptr,&frontend.dynamic,2,&error_entry)==ARTBOX_ELF_OK);
    CHECK(artbox_native_dlfcn_resolve(nullptr,&module.dynamic,1,&error_entry)==ARTBOX_ELF_NOT_FOUND);
    auto version_call=reinterpret_cast<void*(*)(void*,const char*,const char*,const void*)>(static_cast<uintptr_t>(version_entry));
    auto error_call=reinterpret_cast<char*(*)(void)>(static_cast<uintptr_t>(error_entry));
    auto guest_name=reinterpret_cast<const char*>(static_cast<uintptr_t>(symbol_name));
    auto guest_handle=reinterpret_cast<void*>(static_cast<uintptr_t>(handle));
    CHECK(!version_call(guest_handle,guest_name,nullptr,nullptr));
    CHECK(!artbox_native_dlfcn_swap(thread));
    CHECK(reinterpret_cast<uintptr_t>(version_call(guest_handle,guest_name,nullptr,nullptr))==base+0x4508);
    CHECK(!error_call());
    std::thread bound_worker([&] {
        CHECK(!error_call() && !artbox_native_dlfcn_swap(other));
        CHECK(!version_call(guest_handle,nullptr,nullptr,nullptr));
        CHECK(reinterpret_cast<uintptr_t>(error_call())==other_error && !error_call());
        CHECK(artbox_native_dlfcn_swap(nullptr)==other);
    });
    bound_worker.join();
    CHECK(!error_call() && artbox_native_dlfcn_swap(nullptr)==thread);
    CHECK(artbox_guest_dlclose(thread,handle)==0);
    artbox_guest_dl_thread_destroy(other);artbox_guest_dl_thread_destroy(thread);
    artbox_guest_dlfcn_destroy(service);artbox_dlfcn_destroy(loader);artbox_load_group_destroy(group);
    CHECK(artbox_vm_reserved_bytes(vm)==length);
    CHECK(!artbox_vm_munmap(vm,base,length) && !artbox_vm_reserved_bytes(vm));
    CHECK(!artbox_vm_destroy(vm));
}
int main() {
    tls_tests();
    query_tests();
    dlfcn_tests();
    guest_dlfcn_tests();
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
