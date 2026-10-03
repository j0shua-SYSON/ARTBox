// Original ARTBox DEX loading contract, using the unmodified AOSP loader API.
#include "dex/dex_file_loader.h"
#include "dex/dex_file-inl.h"
#include "dex/class_accessor-inl.h"
#include "dex/code_item_accessors-inl.h"
#include "dex/signature-inl.h"
#include "base/mem_map.h"
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

static int inspect(const unsigned char* data, size_t size) {
    if (!data || size < 112 || size > 1024*1024 || reinterpret_cast<uintptr_t>(data)%4) return 2;
    std::string error;
    art::DexFileLoader loader(data,size,"ARTBoxHello.dex");
    auto dex=loader.Open(0,true,true,&error);
    if (!dex) { std::fprintf(stderr,"AOSP DEX verifier: %s\n",error.c_str()); return 2; }
    if (dex->NumClassDefs()!=1 || dex->NumMethodIds()!=1) return 3;
    const auto& cls=dex->GetClassDef(0);
    const auto& method=dex->GetMethodId(0);
    if (std::strcmp(dex->GetClassDescriptor(cls),"Lartbox/Hello;") ||
        std::strcmp(dex->GetMethodName(method),"message") ||
        dex->GetMethodSignature(method).ToString()!="()Ljava/lang/String;") return 3;
    art::ClassAccessor accessor(*dex,cls);
    unsigned methods=0;
    for (const auto& m:accessor.GetMethods()) {
        if (++methods!=1 || m.GetAccessFlags()!=9 || !m.GetCodeItem()) return 3;
        auto code=m.GetInstructions();
        if (code.InsnsSizeInCodeUnits()!=3 || code.Insns()[0]!=0x001a || code.Insns()[2]!=0x0011) return 3;
        auto index=art::dex::StringIndex(code.Insns()[1]);
        if (index.index_>=dex->NumStringIds() || std::strcmp(dex->GetStringData(index),"hello from ARTBox ART")) return 3;
    }
    if (methods!=1) return 3;
    return 0;
}
extern "C" __attribute__((visibility("default")))
int artbox_dex_inspect(const unsigned char* data, size_t size) {
    // Diagnostic entry, invoked once in each process. No ART Runtime is started.
    art::MemMap::Init();
    int result=inspect(data,size);
    art::MemMap::Shutdown();
    return result;
}
#ifdef ARTBOX_DEX_EXECUTABLE
int main(int argc,char** argv) {
    if (argc!=2) return 1;
    FILE* f=std::fopen(argv[1],"rb");
    if (!f || std::fseek(f,0,SEEK_END)) return 1;
    long length=std::ftell(f);
    if (length<0 || length>1024*1024 || std::fseek(f,0,SEEK_SET)) return 1;
    std::vector<unsigned char> data(static_cast<size_t>(length));
    if (std::fread(data.data(),1,data.size(),f)!=data.size() || std::fclose(f)) return 1;
    int result=artbox_dex_inspect(data.data(),data.size());
    std::printf("{\"dex_verified\":%s,\"dex_executed\":false,\"result\":%d}\n",result==0?"true":"false",result);
    return result;
}

#endif
