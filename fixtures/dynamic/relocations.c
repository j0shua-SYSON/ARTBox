/* Original data fixture: no libc, kernel calls or runtime TLS. */
extern int artbox_dependency(void);
extern int artbox_dependency_data;
extern int artbox_missing_weak __attribute__((weak));

static unsigned char local_data[] = {1, 2, 3, 4};
#define TWO(x) x, x
#define EIGHT(x) TWO(x), TWO(x), TWO(x), TWO(x)
#define SIXTY_FOUR(x) EIGHT(x), EIGHT(x), EIGHT(x), EIGHT(x), EIGHT(x), EIGHT(x), EIGHT(x), EIGHT(x)
void *artbox_relr[130] = {SIXTY_FOUR(local_data), SIXTY_FOUR(local_data + 1), local_data + 2, local_data + 3};
int *artbox_external = &artbox_dependency_data;
int *artbox_optional = &artbox_missing_weak;
int artbox_relocation_probe(void) {
    return artbox_dependency() + *artbox_external + *(unsigned char *)artbox_relr[0] + (artbox_optional == 0);
}
