// Native Linux runner for the handler mask source. SPDX-License-Identifier: MIT
#include <stdio.h>
#include <string.h>
extern int artbox_signal_mask_handler_check(int);
int main(int argc,char **argv) {
    int drop=argc==2 && !strcmp(argv[1],"--drop-unblock");
    if(argc>2 || (argc==2 && !drop)) return 64;
    int result=artbox_signal_mask_handler_check(drop);
    if(result!=18) { fprintf(stderr,"signal mask contract: %d\n",result); return 1; }
    puts("{\"handler_cases\":18,\"workers\":1,\"return_mask_edited\":true,\"queued_signal_preserved\":true}");
    return 0;
}
