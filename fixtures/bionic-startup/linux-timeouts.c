#include <stdio.h>
extern int artbox_timeout_check(void);
int main(void) {
    int result = artbox_timeout_check();
    printf("{\"timeout_cases\":%d}\n", result);
    return result == 18 ? 0 : 1;
}
