/* SPDX-License-Identifier: MIT */
#include <errno.h>
#include <grp.h>
#include <pwd.h>
#include <stdint.h>
#include <string.h>

#define CHECK(value) do { if (!(value)) return -__LINE__; ++cases; } while (0)
static int inside(const void *p, const void *buffer, size_t length) {
    return (uintptr_t)p >= (uintptr_t)buffer && (uintptr_t)p - (uintptr_t)buffer < length;
}

/* Android's account table is not the host machine's passwd/group database. */
int artbox_libcore_frontend_accounts(void) {
    static const struct { const char *name; unsigned id; } accounts[] = {
        {"root", 0}, {"system", 1000}, {"shell", 2000}, {"nobody", 9999}
    };
    int cases = 0;
    struct { uint64_t before; char value[8192]; uint64_t after; } storage;
    storage.before = storage.after = UINT64_C(0xface012389abcdef);
    for (unsigned i = 0; i < sizeof(accounts) / sizeof(accounts[0]); ++i) {
        struct passwd *pw = getpwuid(accounts[i].id);
        CHECK(pw && !strcmp(pw->pw_name, accounts[i].name) && pw->pw_uid == accounts[i].id && pw->pw_gid == accounts[i].id);
        pw = getpwnam(accounts[i].name);
        CHECK(pw && pw->pw_uid == accounts[i].id && !strcmp(pw->pw_dir, "/") && !strcmp(pw->pw_shell, "/bin/sh"));
        struct group *gr = getgrgid(accounts[i].id);
        CHECK(gr && gr->gr_gid == accounts[i].id && !strcmp(gr->gr_name, accounts[i].name));
        gr = getgrnam(accounts[i].name);
        CHECK(gr && gr->gr_gid == accounts[i].id && !strcmp(gr->gr_name, accounts[i].name));
        struct passwd owned_pw, *pw_result = NULL;
        errno = EPERM;
        CHECK(getpwuid_r(accounts[i].id, &owned_pw, storage.value, sizeof(storage.value), &pw_result) == 0 &&
              errno == EPERM && pw_result == &owned_pw && owned_pw.pw_uid == accounts[i].id &&
              !strcmp(owned_pw.pw_name, accounts[i].name) && inside(owned_pw.pw_name, storage.value, sizeof(storage.value)));
        (void)getpwnam("root");
        CHECK(!strcmp(owned_pw.pw_name, accounts[i].name));
        struct group owned_gr, *gr_result = NULL;
        CHECK(getgrnam_r(accounts[i].name, &owned_gr, storage.value, sizeof(storage.value), &gr_result) == 0 &&
              gr_result == &owned_gr && owned_gr.gr_gid == accounts[i].id &&
              !strcmp(owned_gr.gr_name, accounts[i].name) && inside(owned_gr.gr_name, storage.value, sizeof(storage.value)) &&
              storage.before == UINT64_C(0xface012389abcdef) && storage.after == UINT64_C(0xface012389abcdef));
    }
    struct passwd pw, *pw_result = &pw;
    struct group gr, *gr_result = &gr;
    errno = EPERM;
    CHECK(getpwuid_r(0, &pw, storage.value, 1, &pw_result) == ERANGE && pw_result == NULL && errno == EPERM);
    CHECK(getgrgid_r(0, &gr, storage.value, 1, &gr_result) == ERANGE && gr_result == NULL && errno == EPERM &&
          storage.before == UINT64_C(0xface012389abcdef) && storage.after == UINT64_C(0xface012389abcdef));
    return cases;
}
