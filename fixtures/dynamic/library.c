/* Keep dependencies, TLS, constructors and a relative data pointer in the ELF. */
extern int artbox_dependency(void);
static int value = 1;
int *artbox_pointer = &value;
_Thread_local int artbox_thread_value = 7;
__attribute__((constructor)) static void initialize(void) { ++value; }
int artbox_probe(void) { return artbox_dependency() + *artbox_pointer + artbox_thread_value; }
