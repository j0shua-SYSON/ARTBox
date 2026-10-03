// Native Linux runner for the shared handler source. SPDX-License-Identifier: MIT
#include <stdio.h>
#include <string.h>
extern int artbox_signal_handler_check(int);
int main(int argc,char **argv) {
    int drop=argc==2 && !strcmp(argv[1],"--drop-register-edit");
    if(argc>2 || (argc==2 && !drop)) return 64;
    int result=artbox_signal_handler_check(drop);
    if(result!=16) {
        fprintf(stderr,"signal handler contract: %d\n",result);
        return 1;
    }
    puts("{\"cases\":16,\"deliveries\":1,\"registers_resumed\":true}");
    return 0;
}
