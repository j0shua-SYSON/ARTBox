/* SPDX-License-Identifier: MIT */
#define _GNU_SOURCE
#include <arpa/inet.h>
#include <errno.h>
#include <netdb.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>

#define CHECK(value) do { if (!(value)) return -__LINE__; ++cases; } while (0)

int artbox_libcore_frontend_common(void) {
    int cases = 0;
    char text[12] = "abc";
    CHECK(strlcat(text, "def", sizeof(text)) == 6 && !strcmp(text, "abcdef"));
    strcpy(text, "abc");
    CHECK(strlcat(text, "def", 5) == 6 && !strcmp(text, "abcd"));
    CHECK(strlcat(text, "xyz", 0) == 3 && !strcmp(text, "abcd"));
    memcpy(text, "abcd", 4);
    CHECK(strlcat(text, "xy", 3) == 5 && !memcmp(text, "abcd", 4));
    const char *base = "abcxyz";
    CHECK(strcspn(base, "xy") == 3 && strpbrk(base, "xy") == base + 3);
    CHECK(strcspn(base, "q") == 6 && strpbrk(base, "q") == NULL);
    CHECK(strcspn("", "xy") == 0 && strpbrk("", "xy") == NULL);
    CHECK(strcspn(base, "") == 6 && strpbrk(base, "") == NULL);

    static const struct { int family; const char *text; } inputs[] = {
        {AF_INET, "127.0.0.1"}, {AF_INET, "192.0.2.255"},
        {AF_INET6, "::1"}, {AF_INET6, "2001:db8::1234"}
    };
    for (unsigned i = 0; i < sizeof(inputs) / sizeof(inputs[0]); ++i) {
        union { struct in_addr v4; struct in6_addr v6; } address;
        struct { unsigned char before; char value[64]; unsigned char after; } printed;
        memset(&printed, 0xa5, sizeof(printed));
        CHECK(inet_pton(inputs[i].family, inputs[i].text, &address) == 1);
        CHECK(inet_ntop(inputs[i].family, &address, printed.value, sizeof(printed.value)) == printed.value &&
              !strcmp(printed.value, inputs[i].text) && printed.before == 0xa5 && printed.after == 0xa5);
        errno = 0;
        CHECK(inet_ntop(inputs[i].family, &address, printed.value, strlen(inputs[i].text)) == NULL &&
              errno == ENOSPC && printed.before == 0xa5 && printed.after == 0xa5);

        struct addrinfo hints = {0}, *result = NULL;
        hints.ai_family = inputs[i].family;
        hints.ai_socktype = SOCK_STREAM;
        hints.ai_protocol = IPPROTO_TCP;
        hints.ai_flags = AI_NUMERICHOST | AI_NUMERICSERV;
        CHECK(getaddrinfo(inputs[i].text, "443", &hints, &result) == 0 && result != NULL);
        socklen_t expected_length = inputs[i].family == AF_INET ? sizeof(struct sockaddr_in) : sizeof(struct sockaddr_in6);
        CHECK(result->ai_family == inputs[i].family && result->ai_socktype == SOCK_STREAM &&
              result->ai_protocol == IPPROTO_TCP && result->ai_addrlen == expected_length && result->ai_next == NULL);
        const struct sockaddr_in *v4 = (const struct sockaddr_in *)result->ai_addr;
        const struct sockaddr_in6 *v6 = (const struct sockaddr_in6 *)result->ai_addr;
        CHECK(inputs[i].family == AF_INET ?
              v4->sin_port == htons(443) && !memcmp(&v4->sin_addr, &address.v4, sizeof(address.v4)) :
              v6->sin6_port == htons(443) && !memcmp(&v6->sin6_addr, &address.v6, sizeof(address.v6)));
        struct { unsigned char before; char value[8]; unsigned char after; } service;
        memset(&service, 0xa5, sizeof(service));
        CHECK(getnameinfo(result->ai_addr, result->ai_addrlen, printed.value, sizeof(printed.value),
                          service.value, sizeof(service.value), NI_NUMERICHOST | NI_NUMERICSERV) == 0 &&
              !strcmp(printed.value, inputs[i].text) && !strcmp(service.value, "443") &&
              printed.before == 0xa5 && printed.after == 0xa5 && service.before == 0xa5 && service.after == 0xa5);
        /* These libcs use different error constants for a short buffer. */
#ifdef __BIONIC__
        const int short_buffer = EAI_MEMORY;
#else
        const int short_buffer = EAI_OVERFLOW;
#endif
        CHECK(getnameinfo(result->ai_addr, result->ai_addrlen, printed.value, sizeof(printed.value),
                          service.value, 3, NI_NUMERICHOST | NI_NUMERICSERV) == short_buffer &&
              printed.before == 0xa5 && printed.after == 0xa5 && service.before == 0xa5 && service.after == 0xa5);
        freeaddrinfo(result);
    }
    unsigned char address[16] = {0};
    errno = 0;
    CHECK(inet_ntop(AF_UNSPEC, address, text, sizeof(text)) == NULL && errno == EAFNOSUPPORT);
    struct addrinfo hints = {0}, *result = NULL;
    hints.ai_family = AF_INET; hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_NUMERICHOST | AI_NUMERICSERV;
    CHECK(getaddrinfo("invalid.example", "443", &hints, &result) == EAI_NONAME);
    CHECK(getaddrinfo("127.0.0.1", "not-a-port", &hints, &result) == EAI_NONAME);
    hints.ai_family = AF_UNIX;
    CHECK(getaddrinfo("127.0.0.1", "443", &hints, &result) == EAI_FAMILY);
    CHECK(getaddrinfo(NULL, NULL, NULL, &result) == EAI_NONAME);
    return cases;
}
