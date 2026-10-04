// Original native Linux reference runner. SPDX-License-Identifier: MIT
#include <stdio.h>
#include <string.h>
int artbox_signal_fault_check(int mode);
int main(int argc,char **argv) {
    int mode=0;
    if(argc==2 && !strcmp(argv[1],"--drop-register-edit")) mode=1;
    else if(argc==2 && !strcmp(argv[1],"--drop-fault-address")) mode=2;
    else if(argc!=1) return 64;
    int result=artbox_signal_fault_check(mode);
    if(result!=5) { fprintf(stderr,"signal fault contract: %d\n",result); return 1; }
    puts("{\"fault_cases\":5,\"native_resume\":true,\"alternate_stack\":true,\"mask_preserved\":true}");
    return 0;
}
