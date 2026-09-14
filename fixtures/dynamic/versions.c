// Original versioned-export fixture, MIT.
int old_api(void) { return 17; }
int new_api(void) { return 29; }
__asm__(".symver old_api,versioned@ARTBOX_1");
__asm__(".symver new_api,versioned@@ARTBOX_2");
