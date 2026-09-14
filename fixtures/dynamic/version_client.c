// Both symbol versions must bind despite sharing their ELF symbol name.
extern int previous(void);
extern int versioned(void);
__asm__(".symver previous,versioned@ARTBOX_1");
int version_client(void) { return previous() + versioned(); }
