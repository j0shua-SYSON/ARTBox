/* Original threaded handle-zero ping/reply fixture. SPDX-License-Identifier: MIT */
#include "check.h"
#include "artbox/binder_wire.h"
#include <stdio.h>
#include <string.h>
#define REQUIRE(x) do { if (!(x)) { fprintf(stderr, "Binder transaction line %d: %s\n", __LINE__, #x); return -1; } } while (0)
static const unsigned char ping[] = "Binder ping", pong[] = "Binder pong";
enum { transaction_code = 0x41525442, object_pointer = 0x1000, object_cookie = 0x2000 };
struct endpoint {
    void *context;
    const artbox_binder_transaction_ops *ops;
    artbox_binder_transaction_scratch *scratch;
    int fd;
    int32_t tid;
    uint64_t mapping;
    size_t length;
};
static uint64_t pointer(const void *p) { return (uint64_t)(uintptr_t)p; }
static void put32(unsigned char *p, uint32_t value) {
    for (unsigned i = 0; i < 4; ++i) p[i] = (unsigned char)(value >> (i * 8));
}
static void put64(unsigned char *p, uint64_t value) {
    for (unsigned i = 0; i < 8; ++i) p[i] = (unsigned char)(value >> (i * 8));
}
static int call(struct endpoint *e, uint32_t command, uint64_t argument) {
    return (int)e->ops->ioctl(e->context, e->fd, e->tid, command, argument);
}
static int send(struct endpoint *e, size_t size) {
    uint64_t *t = e->scratch->transfer;
    memset(t, 0, 6 * sizeof(*t));
    t[0] = size; t[2] = pointer(e->scratch->write);
    REQUIRE(call(e, ARTBOX_BINDER_WRITE_READ, pointer(t)) == 0 && t[1] == size);
    return 0;
}
static int receive(struct endpoint *e, size_t *size) {
    uint64_t *t = e->scratch->transfer;
    memset(t, 0, 6 * sizeof(*t));
    t[3] = sizeof(e->scratch->read); t[5] = pointer(e->scratch->read);
    int result = call(e, ARTBOX_BINDER_WRITE_READ, pointer(t));
    if (result == -11) { *size = 0; return 0; }
    REQUIRE(result == 0 && t[4] <= sizeof(e->scratch->read));
    *size = (size_t)t[4];
    return 0;
}
static size_t transaction(struct endpoint *e, uint32_t command, uint32_t code,
                          const unsigned char *data, size_t size) {
    unsigned char *w = e->scratch->write;
    memset(w, 0, 68);
    put32(w, command); put32(w + 20, code);
    memcpy(e->scratch->data, data, size);
    put64(w + 36, size); put64(w + 52, pointer(e->scratch->data));
    return 68;
}
static void free_buffer(unsigned char *write, uint64_t address) {
    put32(write, ARTBOX_BC_FREE_BUFFER); put64(write + 4, address);
}
static int ref_command(struct endpoint *e, const artbox_binder_frame *frame) {
    uint32_t reply = frame->command == ARTBOX_BR_INCREFS ? ARTBOX_BC_INCREFS_DONE :
                     frame->command == ARTBOX_BR_ACQUIRE ? ARTBOX_BC_ACQUIRE_DONE : 0;
    if (reply) {
        put32(e->scratch->write, reply);
        memcpy(e->scratch->write + 4, frame->payload, 16);
        return send(e, 20);
    }
    if (frame->command != ARTBOX_BR_NOOP && frame->command != ARTBOX_BR_RELEASE &&
            frame->command != ARTBOX_BR_DECREFS) {
        fprintf(stderr, "Binder role %d unexpected return 0x%08x\n", e->tid, frame->command);
        return -1;
    }
    return 0;
}
static int payload(struct endpoint *e, const artbox_binder_transaction *t,
                   const unsigned char *expected, size_t size) {
    unsigned char captured[32];
    REQUIRE(t->data_size == size && !t->offsets_size);
    REQUIRE(t->data_buffer >= e->mapping && t->data_buffer - e->mapping <= e->length &&
            size <= e->length - (size_t)(t->data_buffer - e->mapping));
    REQUIRE(e->ops->read(e->context, t->data_buffer, captured, size) == 0);
    REQUIRE(!memcmp(captured, expected, size));
    return 0;
}
static int server_loop(void *opaque) {
    struct endpoint *e = opaque;
    put32(e->scratch->write, ARTBOX_BC_ENTER_LOOPER);
    REQUIRE(send(e, 4) == 0);
    unsigned received = 0, completed = 0;
    for (unsigned attempt = 0; attempt < 5000 && (!received || !completed); ++attempt) {
        size_t size = 0, cursor = 0;
        REQUIRE(receive(e, &size) == 0);
        while (cursor < size) {
            artbox_binder_frame frame;
            REQUIRE(artbox_binder_next(ARTBOX_BINDER_READ, e->scratch->read, size, &cursor, &frame) == ARTBOX_BINDER_OK);
            if (frame.command == ARTBOX_BR_TRANSACTION) {
                artbox_binder_transaction t;
                REQUIRE(!received++ && artbox_binder_decode_transaction(&frame, &t) == ARTBOX_BINDER_OK);
                REQUIRE(t.target == object_pointer && t.cookie == object_cookie && t.code == transaction_code);
                REQUIRE(t.sender_pid == e->ops->pid(e->context, 2) && t.sender_euid == e->ops->uid);
                REQUIRE(payload(e, &t, ping, sizeof(ping)) == 0);
                size_t bytes = transaction(e, ARTBOX_BC_REPLY, 0, pong, sizeof(pong));
                free_buffer(e->scratch->write + bytes, t.data_buffer);
                REQUIRE(send(e, bytes + 12) == 0);
            } else if (frame.command == ARTBOX_BR_TRANSACTION_COMPLETE) ++completed;
            else REQUIRE(ref_command(e, &frame) == 0);
        }
        if (!size) e->ops->pause(e->context);
    }
    REQUIRE(received == 1 && completed == 1);
    put32(e->scratch->write, ARTBOX_BC_EXIT_LOOPER);
    REQUIRE(send(e, 4) == 0);
    return 0;
}
static int client_loop(void *opaque) {
    struct endpoint *e = opaque;
    REQUIRE(send(e, transaction(e, ARTBOX_BC_TRANSACTION, transaction_code, ping, sizeof(ping))) == 0);
    unsigned replies = 0, completed = 0;
    for (unsigned attempt = 0; attempt < 5000 && (!replies || !completed); ++attempt) {
        size_t size = 0, cursor = 0;
        REQUIRE(receive(e, &size) == 0);
        while (cursor < size) {
            artbox_binder_frame frame;
            REQUIRE(artbox_binder_next(ARTBOX_BINDER_READ, e->scratch->read, size, &cursor, &frame) == ARTBOX_BINDER_OK);
            if (frame.command == ARTBOX_BR_REPLY) {
                artbox_binder_transaction t;
                REQUIRE(!replies++ && artbox_binder_decode_transaction(&frame, &t) == ARTBOX_BINDER_OK);
                REQUIRE(payload(e, &t, pong, sizeof(pong)) == 0);
                free_buffer(e->scratch->write, t.data_buffer);
                REQUIRE(send(e, 12) == 0);
            } else if (frame.command == ARTBOX_BR_TRANSACTION_COMPLETE) ++completed;
            else REQUIRE(ref_command(e, &frame) == 0);
        }
        if (!size) e->ops->pause(e->context);
    }
    REQUIRE(replies == 1 && completed == 1);
    return 0;
}
static int prepare(struct endpoint *e) {
    e->fd = e->ops->open(e->context, e->tid);
    REQUIRE(e->fd >= 0);
    int64_t mapped = e->ops->map(e->context, e->fd, e->length);
    REQUIRE(mapped > 0);
    e->mapping = (uint64_t)mapped;
    return 0;
}
static int cleanup(struct endpoint *e, int result) {
    if (e->mapping && e->ops->unmap(e->context, e->mapping, e->length)) result = -1;
    if (e->fd >= 0 && e->ops->close(e->context, e->fd)) result = -1;
    e->mapping = 0; e->fd = -1;
    return result;
}
static int become_manager(struct endpoint *e) {
    unsigned char *write = e->scratch->write;
    memset(write, 0, 24);
    put32(write, ARTBOX_BINDER_TYPE_BINDER);
    put64(write + 8, object_pointer); put64(write + 16, object_cookie);
    int manager = -16;
    for (unsigned attempt = 0; attempt < 2000 && manager == -16; ++attempt) {
        manager = call(e, ARTBOX_BINDER_SET_CONTEXT_MGR_EXT, pointer(write));
        if (manager == -16) e->ops->pause(e->context);
    }
    REQUIRE(manager == 0);
    memset(write, 0, 4);
    REQUIRE(call(e, ARTBOX_BINDER_SET_MAX_THREADS, pointer(write)) == 0);
    return 0;
}
static int client_run(void *opaque) {
    struct endpoint *e = opaque;
    int result = prepare(e);
    if (!result) result = client_loop(e);
    return cleanup(e, result);
}
int artbox_binder_transaction_check(void *context, const artbox_binder_transaction_ops *ops,
    artbox_binder_transaction_scratch *server, artbox_binder_transaction_scratch *client) {
    const size_t size = ops->page_size * 16;
    struct endpoint a = {context, ops, server, -1, 1, 0, size};
    struct endpoint b = {context, ops, client, -1, 2, 0, size};
    int result = prepare(&a);
    if (result || become_manager(&a)) return cleanup(&a, -1);
    int results[2] = {-1, -1};
    result = ops->parallel(context, server_loop, &a, client_run, &b, results);
    if (results[0] || results[1]) result = -1;
    return cleanup(&a, result);
}
static int expect_self_rejection(struct endpoint *e) {
    REQUIRE(send(e, transaction(e, ARTBOX_BC_TRANSACTION, transaction_code, ping, sizeof(ping))) == 0);
    for (unsigned attempt = 0; attempt < 2000; ++attempt) {
        size_t size = 0, cursor = 0;
        REQUIRE(receive(e, &size) == 0);
        while (cursor < size) {
            artbox_binder_frame frame;
            REQUIRE(artbox_binder_next(ARTBOX_BINDER_READ, e->scratch->read, size, &cursor, &frame) == ARTBOX_BINDER_OK);
            if (frame.command == ARTBOX_BR_FAILED_REPLY) return 0;
            REQUIRE(frame.command == ARTBOX_BR_NOOP);
        }
        e->ops->pause(e->context);
    }
    REQUIRE(0 && "Same-PID context-manager call was not rejected");
}
int artbox_binder_transaction_same_pid_check(void *context, const artbox_binder_transaction_ops *ops,
    artbox_binder_transaction_scratch *server, artbox_binder_transaction_scratch *client) {
    const size_t size = ops->page_size * 16;
    struct endpoint a = {context, ops, server, -1, 1, 0, size};
    struct endpoint b = {context, ops, client, -1, 2, 0, size};
    REQUIRE(ops->pid(context, 1) == ops->pid(context, 2));
    int result = prepare(&a);
    if (!result) result = prepare(&b);
    if (!result) result = become_manager(&a);
    if (!result) result = expect_self_rejection(&b);
    result = cleanup(&b, result);
    return cleanup(&a, result);
}
