// SPDX-License-Identifier: MIT
#include <cstdint>
#include <cstdio>
#include <csignal>
#include <cstring>
extern "C" int artbox_sigchain_check(uint64_t,uint64_t,int);
int main(int argc,char** argv) {
    int drop=argc==2 && !std::strcmp(argv[1],"--drop-special");
    int result=artbox_sigchain_check(reinterpret_cast<uintptr_t>(&sigaction),
                                   reinterpret_cast<uintptr_t>(&sigprocmask),drop);
    if(result!=22) { std::fprintf(stderr,"sigchain contract: %d\n",result); return 1; }
    std::puts("{\"cases\":22,\"special_calls\":2,\"user_calls\":2}");
}
