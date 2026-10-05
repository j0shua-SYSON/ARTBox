// SPDX-License-Identifier: MIT
#include "artbox/native_service.h"
#include <stdio.h>
#include <stdlib.h>
static void log_output(void *context,const char *bytes,size_t size) {
    (void)context; if(fwrite(bytes,1,size,stdout)!=size) exit(1);
}
int main(int argc,char **argv) {
    // mode, backing root, then (role root + six framework/ELF pairs) per role.
    if(argc!=16 && argc!=42) return 2;
    artbox_service_input input={0};
    if(argv[1][0]<'0' || argv[1][0]>'3' || argv[1][1]) return 2;
    input.mode=(unsigned)(argv[1][0]-'0'); input.backing_root=argv[2];
    unsigned count=input.mode ? 1 : 3;
    if((unsigned)argc!=3+13*count) return 2;
    unsigned at=3;
    for(unsigned r=0;r<count;++r) {
        input.roots[r]=argv[at++];
        for(unsigned i=0;i<6;++i) { input.frameworks[r][i]=argv[at++]; input.elfs[r][i]=argv[at++]; }
    }
    const artbox_host host={log_output,NULL};
    return artbox_run_native_service(&input,&host);
}
