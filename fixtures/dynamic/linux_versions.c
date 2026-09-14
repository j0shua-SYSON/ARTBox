#include <stdio.h>
extern int version_client(void);
int main(void) {
    int result = version_client();
    if (result != 46) { fprintf(stderr, "Versioned NDK calls returned %d\n", result); return 1; }
    puts("{\"version_result\":46}");
    return 0;
}
