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
    if (!result) {
        // Acquiring the owner endpoint's own manager node is an ioctl error,
        // distinct from a same-PID transaction's BR_FAILED_REPLY response.
        put32(server->write, ARTBOX_BC_ACQUIRE); put32(server->write + 4, 0);
        memset(server->transfer, 0, sizeof(server->transfer));
        server->transfer[0] = 8; server->transfer[2] = pointer(server->write);
        if (call(&a, ARTBOX_BINDER_WRITE_READ, pointer(server->transfer)) != -22 || server->transfer[1]) result = -1;
    }
    if (!result) result = expect_self_rejection(&b);
    result = cleanup(&b, result);
    return cleanup(&a, result);
}

static const uint64_t death_cookie = UINT64_C(0x1234567887654321);
static uint64_t get64(const unsigned char *p) {
    uint64_t value = 0;
    for (unsigned i = 0; i < 8; ++i) value |= (uint64_t)p[i] << (i * 8);
    return value;
}
static int death_command_handle(struct endpoint *e, uint32_t command, uint32_t handle, uint64_t cookie) {
    put32(e->scratch->write, command);
    if (command == ARTBOX_BC_DEAD_BINDER_DONE) {
        put64(e->scratch->write + 4, cookie);
        return send(e, 12);
    }
    put32(e->scratch->write + 4, handle);
    put64(e->scratch->write + 8, cookie);
    return send(e, 16);
}
static int death_command(struct endpoint *e, uint32_t command, uint64_t cookie) {
    return death_command_handle(e, command, 0, cookie);
}
static int no_event(struct endpoint *e) {
    size_t size = 0;
    REQUIRE(receive(e, &size) == 0 && !size);
    return 0;
}
static int cookie_event(struct endpoint *e, uint32_t expected) {
    for (unsigned attempt = 0; attempt < 5000; ++attempt) {
        size_t size = 0, cursor = 0;
        unsigned events = 0;
        REQUIRE(receive(e, &size) == 0);
        while (cursor < size) {
            artbox_binder_frame frame;
            REQUIRE(artbox_binder_next(ARTBOX_BINDER_READ, e->scratch->read, size, &cursor, &frame) == ARTBOX_BINDER_OK);
            if (frame.command == ARTBOX_BR_NOOP) continue;
            REQUIRE(frame.command == expected && get64(frame.payload) == death_cookie && !events++);
        }
        if (events) return 0;
        e->ops->pause(e->context);
    }
    REQUIRE(0 && "Missing Binder cookie event");
}
static int departing_server(void *opaque) {
    struct endpoint *e = opaque;
    put32(e->scratch->write, ARTBOX_BC_ENTER_LOOPER);
    REQUIRE(send(e, 4) == 0);
    for (unsigned attempt = 0; attempt < 5000; ++attempt) {
        size_t size = 0, cursor = 0;
        REQUIRE(receive(e, &size) == 0);
        while (cursor < size) {
            artbox_binder_frame frame;
            REQUIRE(artbox_binder_next(ARTBOX_BINDER_READ, e->scratch->read, size, &cursor, &frame) == ARTBOX_BINDER_OK);
            if (frame.command == ARTBOX_BR_TRANSACTION) {
                artbox_binder_transaction t;
                REQUIRE(artbox_binder_decode_transaction(&frame, &t) == ARTBOX_BINDER_OK);
                REQUIRE(t.target == object_pointer && t.cookie == object_cookie && t.code == transaction_code);
                REQUIRE(t.sender_pid == e->ops->pid(e->context, 2) && payload(e, &t, ping, sizeof(ping)) == 0);
                free_buffer(e->scratch->write, t.data_buffer);
                REQUIRE(send(e, 12) == 0);
                // Abandon this actual synchronous call. The caller must observe
                // both owner death and a dead reply; no test-only death hook.
                return cleanup(e, 0);
            }
            REQUIRE(ref_command(e, &frame) == 0);
        }
        if (!size) e->ops->pause(e->context);
    }
    REQUIRE(0 && "Manager never received the pending death-test call");
}
struct death_client { struct endpoint endpoint; unsigned mode; };
static int death_client_loop(struct death_client *client) {
    struct endpoint *e = &client->endpoint;
    memset(e->scratch->write, 0, 4);
    REQUIRE(call(e, ARTBOX_BINDER_SET_MAX_THREADS, pointer(e->scratch->write)) == 0);
    put32(e->scratch->write, ARTBOX_BC_ENTER_LOOPER);
    REQUIRE(send(e, 4) == 0);
    // An unowned handle and unmatched acknowledgement are consumed without
    // creating a subscription. The live reference is acquired afterward.
    REQUIRE(death_command(e, ARTBOX_BC_REQUEST_DEATH_NOTIFICATION, death_cookie + 1) == 0);
    REQUIRE(death_command(e, ARTBOX_BC_DEAD_BINDER_DONE, death_cookie + 1) == 0);
    REQUIRE(no_event(e) == 0);
    put32(e->scratch->write, ARTBOX_BC_INCREFS); put32(e->scratch->write + 4, 0);
    put32(e->scratch->write + 8, ARTBOX_BC_ACQUIRE); put32(e->scratch->write + 12, 0);
    REQUIRE(send(e, 16) == 0);
    if (client->mode != 2) {
        REQUIRE(death_command(e, ARTBOX_BC_REQUEST_DEATH_NOTIFICATION, death_cookie) == 0);
        REQUIRE(death_command(e, ARTBOX_BC_REQUEST_DEATH_NOTIFICATION, death_cookie + 1) == 0);
        REQUIRE(death_command(e, ARTBOX_BC_CLEAR_DEATH_NOTIFICATION, death_cookie + 1) == 0);
        REQUIRE(no_event(e) == 0);
    }
    if (!client->mode) {
        REQUIRE(death_command(e, ARTBOX_BC_CLEAR_DEATH_NOTIFICATION, death_cookie) == 0);
        REQUIRE(cookie_event(e, ARTBOX_BR_CLEAR_DEATH_NOTIFICATION_DONE) == 0 && no_event(e) == 0);
    }
    REQUIRE(send(e, transaction(e, ARTBOX_BC_TRANSACTION, transaction_code, ping, sizeof(ping))) == 0);
    unsigned completed = 0, failed = 0, died = 0;
    for (unsigned attempt = 0; attempt < 5000 && (!completed || !failed); ++attempt) {
        size_t size = 0, cursor = 0;
        REQUIRE(receive(e, &size) == 0);
        while (cursor < size) {
            artbox_binder_frame frame;
            REQUIRE(artbox_binder_next(ARTBOX_BINDER_READ, e->scratch->read, size, &cursor, &frame) == ARTBOX_BINDER_OK);
            if (frame.command == ARTBOX_BR_NOOP) continue;
            if (frame.command == ARTBOX_BR_TRANSACTION_COMPLETE) REQUIRE(!completed++);
            else if (frame.command == ARTBOX_BR_DEAD_REPLY) REQUIRE(!failed++);
            else {
                REQUIRE(client->mode == 1 && frame.command == ARTBOX_BR_DEAD_BINDER);
                REQUIRE(get64(frame.payload) == death_cookie && !died++);
            }
        }
        if (!size) e->ops->pause(e->context);
    }
    REQUIRE(completed == 1 && failed == 1);
    if (client->mode == 2) REQUIRE(death_command(e, ARTBOX_BC_REQUEST_DEATH_NOTIFICATION, death_cookie) == 0);
    if (client->mode) {
        if (!died) REQUIRE(cookie_event(e, ARTBOX_BR_DEAD_BINDER) == 0);
        REQUIRE(death_command(e, ARTBOX_BC_DEAD_BINDER_DONE, death_cookie + 1) == 0);
        if (client->mode == 1) {
            REQUIRE(death_command(e, ARTBOX_BC_CLEAR_DEATH_NOTIFICATION, death_cookie) == 0);
            REQUIRE(no_event(e) == 0); // Clear completion waits for this death's ack.
            REQUIRE(death_command(e, ARTBOX_BC_DEAD_BINDER_DONE, death_cookie) == 0);
        } else {
            REQUIRE(death_command(e, ARTBOX_BC_DEAD_BINDER_DONE, death_cookie) == 0 && no_event(e) == 0);
            REQUIRE(death_command(e, ARTBOX_BC_CLEAR_DEATH_NOTIFICATION, death_cookie) == 0);
        }
        REQUIRE(cookie_event(e, ARTBOX_BR_CLEAR_DEATH_NOTIFICATION_DONE) == 0);
    }
    REQUIRE(death_command(e, ARTBOX_BC_DEAD_BINDER_DONE, death_cookie) == 0);
    REQUIRE(death_command(e, ARTBOX_BC_CLEAR_DEATH_NOTIFICATION, death_cookie) == 0);
    REQUIRE(no_event(e) == 0);
    put32(e->scratch->write, ARTBOX_BC_RELEASE); put32(e->scratch->write + 4, 0);
    put32(e->scratch->write + 8, ARTBOX_BC_DECREFS); put32(e->scratch->write + 12, 0);
    REQUIRE(send(e, 16) == 0 && no_event(e) == 0);
    put32(e->scratch->write, ARTBOX_BC_EXIT_LOOPER);
    REQUIRE(send(e, 4) == 0);
    return 0;
}
static int death_client_run(void *opaque) {
    struct death_client *client = opaque;
    int result = prepare(&client->endpoint);
    if (!result) result = death_client_loop(client);
    return cleanup(&client->endpoint, result);
}
int artbox_binder_death_check(void *context, const artbox_binder_transaction_ops *ops,
    artbox_binder_transaction_scratch *server, artbox_binder_transaction_scratch *client) {
    for (unsigned mode = 0; mode < 3; ++mode) {
        struct endpoint a = {context, ops, server, -1, 1, 0, ops->page_size * 16};
        struct death_client b = {{context, ops, client, -1, 2, 0, ops->page_size * 16}, mode};
        int result = prepare(&a);
        if (result || become_manager(&a)) return cleanup(&a, -1);
        int results[2] = {-1, -1};
        result = ops->parallel(context, departing_server, &a, death_client_run, &b, results);
        if (results[0] || results[1]) result = -1;
        if (cleanup(&a, result)) return -1;
    }
    return 3;
}

enum { service_pointer = 0x10001000, service_cookie = 0x20002000,
       register_code = transaction_code + 1, service_code = transaction_code + 2,
       depart_code = transaction_code + 3 };
static uint32_t get32(const unsigned char *p) {
    uint32_t value = 0;
    for (unsigned i = 0; i < 4; ++i) value |= (uint32_t)p[i] << (i * 8);
    return value;
}
static int configure_looper(struct endpoint *e) {
    memset(e->scratch->write, 0, 4);
    REQUIRE(call(e, ARTBOX_BINDER_SET_MAX_THREADS, pointer(e->scratch->write)) == 0);
    put32(e->scratch->write, ARTBOX_BC_ENTER_LOOPER);
    return send(e, 4);
}
static size_t object_packet(struct endpoint *e, uint32_t command, uint32_t type, uint32_t handle) {
    unsigned char data[64];
    memset(data, 0xa5, sizeof(data));
    for (unsigned i = 0; i < 2; ++i) {
        unsigned char *object = data + 8 + i * 24;
        put32(object, type); put32(object + 4, 0x100); // FLAT_BINDER_FLAG_ACCEPTS_FDS
        put64(object + 8, type == ARTBOX_BINDER_TYPE_BINDER ? service_pointer : handle);
        put64(object + 16, type == ARTBOX_BINDER_TYPE_BINDER ? service_cookie : 0);
        put64(e->scratch->offsets + i * 8, 8 + i * 24);
    }
    size_t size = transaction(e, command, register_code, data, sizeof(data));
    put64(e->scratch->write + 44, 16);
    put64(e->scratch->write + 60, pointer(e->scratch->offsets));
    return size;
}
static int send_object(struct endpoint *e, uint32_t command, uint32_t type, uint32_t handle) {
    return send(e, object_packet(e, command, type, handle));
}
static int object_input_rejections(struct endpoint *e) {
    for (unsigned kind = 0; kind < 3; ++kind) {
        size_t bytes = object_packet(e, ARTBOX_BC_TRANSACTION, ARTBOX_BINDER_TYPE_BINDER, 0);
        if (kind == 0) put64(e->scratch->data + 48, service_cookie + 1);
        else if (kind == 1) put64(e->scratch->offsets + 8, 60); // Truncated second object.
        else {
            put32(e->scratch->data + 32, ARTBOX_BINDER_TYPE_HANDLE);
            put64(e->scratch->data + 40, 0x7ffffffe); // First object valid, second handle unowned.
            put64(e->scratch->data + 48, 0);
        }
        put32(e->scratch->write + bytes, ARTBOX_BC_ENTER_LOOPER);
        memset(e->scratch->transfer, 0, sizeof(e->scratch->transfer));
        e->scratch->transfer[0] = bytes + 4;
        e->scratch->transfer[2] = pointer(e->scratch->write);
        REQUIRE(call(e, ARTBOX_BINDER_WRITE_READ, pointer(e->scratch->transfer)) == 0);
        REQUIRE(e->scratch->transfer[1] == bytes); // Stop this batch at the failed transaction.
        unsigned rejected = 0;
        for (unsigned attempt = 0; attempt < 5000 && !rejected; ++attempt) {
            size_t size = 0, cursor = 0;
            REQUIRE(receive(e, &size) == 0);
            while (cursor < size) {
                artbox_binder_frame frame;
                REQUIRE(artbox_binder_next(ARTBOX_BINDER_READ, e->scratch->read, size, &cursor, &frame) == ARTBOX_BINDER_OK);
                if (frame.command == ARTBOX_BR_NOOP) continue;
                REQUIRE(frame.command == ARTBOX_BR_FAILED_REPLY && !rejected++);
            }
            if (!size) e->ops->pause(e->context);
        }
        REQUIRE(rejected == 1 && no_event(e) == 0);
    }
    return 0;
}
static int mapped_range(struct endpoint *e, uint64_t address, size_t length) {
    return address >= e->mapping && address - e->mapping <= e->length &&
           length <= e->length - (size_t)(address - e->mapping);
}
static int object_payload(struct endpoint *e, const artbox_binder_transaction *t,
                          uint32_t type, uint32_t *handle) {
    unsigned char data[64], offsets[16];
    REQUIRE(t->data_size == sizeof(data) && t->offsets_size == sizeof(offsets));
    REQUIRE(mapped_range(e, t->data_buffer, sizeof(data)) && mapped_range(e, t->offsets_buffer, sizeof(offsets)));
    REQUIRE(e->ops->read(e->context, t->data_buffer, data, sizeof(data)) == 0);
    REQUIRE(e->ops->read(e->context, t->offsets_buffer, offsets, sizeof(offsets)) == 0);
    REQUIRE(get64(offsets) == 8 && get64(offsets + 8) == 32);
    for (unsigned i = 0; i < 8; ++i) REQUIRE(data[i] == 0xa5 && data[56 + i] == 0xa5);
    for (unsigned i = 0; i < 2; ++i) {
        const unsigned char *object = data + 8 + i * 24;
        REQUIRE(get32(object) == type && get32(object + 4) == 0x100);
        if (type == ARTBOX_BINDER_TYPE_HANDLE) {
            REQUIRE(get64(object + 8) > 0 && get64(object + 8) <= UINT32_MAX && !get64(object + 16));
            if (!i) *handle = get32(object + 8);
            else REQUIRE(get32(object + 8) == *handle); // Repeated node, same receiver handle.
        } else REQUIRE(get64(object + 8) == service_pointer && get64(object + 16) == service_cookie);
    }
    return 0;
}
static int send_to_handle(struct endpoint *e, uint32_t handle, uint32_t code) {
    size_t size = transaction(e, ARTBOX_BC_TRANSACTION, code, ping, sizeof(ping));
    put32(e->scratch->write + 4, handle);
    return send(e, size);
}
static int object_manager_loop(void *opaque) {
    struct endpoint *e = opaque;
    REQUIRE(configure_looper(e) == 0);
    uint32_t handle = 0;
    unsigned registered = 0, replied = 0, completed = 0, failed = 0, died = 0;
    for (unsigned attempt = 0; attempt < 5000 && (!registered || !replied || completed != 3 || !failed || !died); ++attempt) {
        size_t size = 0, cursor = 0;
        REQUIRE(receive(e, &size) == 0);
        while (cursor < size) {
            artbox_binder_frame frame;
            REQUIRE(artbox_binder_next(ARTBOX_BINDER_READ, e->scratch->read, size, &cursor, &frame) == ARTBOX_BINDER_OK);
            if (frame.command == ARTBOX_BR_TRANSACTION) {
                artbox_binder_transaction t;
                REQUIRE(!registered++ && artbox_binder_decode_transaction(&frame, &t) == ARTBOX_BINDER_OK);
                REQUIRE(t.target == object_pointer && t.cookie == object_cookie && t.code == register_code);
                REQUIRE(t.sender_pid == e->ops->pid(e->context, 2) && t.sender_euid == e->ops->uid);
                REQUIRE(object_payload(e, &t, ARTBOX_BINDER_TYPE_HANDLE, &handle) == 0);
                // Retain the imported service before its parcel is freed.
                put32(e->scratch->write, ARTBOX_BC_INCREFS); put32(e->scratch->write + 4, handle);
                put32(e->scratch->write + 8, ARTBOX_BC_ACQUIRE); put32(e->scratch->write + 12, handle);
                REQUIRE(send(e, 16) == 0);
                REQUIRE(send_object(e, ARTBOX_BC_REPLY, ARTBOX_BINDER_TYPE_HANDLE, handle) == 0);
                free_buffer(e->scratch->write, t.data_buffer);
                REQUIRE(send(e, 12) == 0);
                REQUIRE(send_to_handle(e, handle, service_code) == 0);
            } else if (frame.command == ARTBOX_BR_REPLY) {
                artbox_binder_transaction t;
                REQUIRE(registered == 1 && !replied++ && artbox_binder_decode_transaction(&frame, &t) == ARTBOX_BINDER_OK);
                REQUIRE(payload(e, &t, pong, sizeof(pong)) == 0);
                free_buffer(e->scratch->write, t.data_buffer);
                REQUIRE(send(e, 12) == 0);
                REQUIRE(death_command_handle(e, ARTBOX_BC_REQUEST_DEATH_NOTIFICATION, handle, death_cookie) == 0);
                REQUIRE(send_to_handle(e, handle, depart_code) == 0);
            } else if (frame.command == ARTBOX_BR_TRANSACTION_COMPLETE) REQUIRE(++completed <= 3);
            else if (frame.command == ARTBOX_BR_DEAD_REPLY) REQUIRE(replied == 1 && !failed++);
            else if (frame.command == ARTBOX_BR_DEAD_BINDER) {
                REQUIRE(replied == 1 && get64(frame.payload) == death_cookie && !died++);
            } else REQUIRE(ref_command(e, &frame) == 0);
        }
        if (!size) e->ops->pause(e->context);
    }
    REQUIRE(registered == 1 && replied == 1 && completed == 3 && failed == 1 && died == 1);
    REQUIRE(death_command_handle(e, ARTBOX_BC_CLEAR_DEATH_NOTIFICATION, handle, death_cookie) == 0);
    REQUIRE(death_command(e, ARTBOX_BC_DEAD_BINDER_DONE, death_cookie) == 0);
    REQUIRE(cookie_event(e, ARTBOX_BR_CLEAR_DEATH_NOTIFICATION_DONE) == 0);
    put32(e->scratch->write, ARTBOX_BC_RELEASE); put32(e->scratch->write + 4, handle);
    put32(e->scratch->write + 8, ARTBOX_BC_DECREFS); put32(e->scratch->write + 12, handle);
    REQUIRE(send(e, 16) == 0 && no_event(e) == 0);
    return 0;
}
static int object_owner_loop(struct endpoint *e) {
    REQUIRE(configure_looper(e) == 0);
    REQUIRE(object_input_rejections(e) == 0);
    REQUIRE(send_object(e, ARTBOX_BC_TRANSACTION, ARTBOX_BINDER_TYPE_BINDER, 0) == 0);
    unsigned echoed = 0, called = 0, departing = 0, completed = 0, acquired = 0, increfs = 0;
    for (unsigned attempt = 0; attempt < 5000 && (!departing || completed != 2); ++attempt) {
        size_t size = 0, cursor = 0;
        REQUIRE(receive(e, &size) == 0);
        while (cursor < size) {
            artbox_binder_frame frame;
            REQUIRE(artbox_binder_next(ARTBOX_BINDER_READ, e->scratch->read, size, &cursor, &frame) == ARTBOX_BINDER_OK);
            if (frame.command == ARTBOX_BR_REPLY) {
                artbox_binder_transaction t;
                uint32_t unused = 0;
                REQUIRE(!echoed++ && artbox_binder_decode_transaction(&frame, &t) == ARTBOX_BINDER_OK);
                REQUIRE(object_payload(e, &t, ARTBOX_BINDER_TYPE_BINDER, &unused) == 0);
                free_buffer(e->scratch->write, t.data_buffer);
                REQUIRE(send(e, 12) == 0);
            } else if (frame.command == ARTBOX_BR_TRANSACTION) {
                artbox_binder_transaction t;
                REQUIRE(artbox_binder_decode_transaction(&frame, &t) == ARTBOX_BINDER_OK);
                REQUIRE(t.target == service_pointer && t.cookie == service_cookie);
                REQUIRE(t.sender_pid == e->ops->pid(e->context, 1) && t.sender_euid == e->ops->uid);
                REQUIRE(payload(e, &t, ping, sizeof(ping)) == 0);
                if (t.code == service_code) {
                    REQUIRE(echoed == 1 && !called++);
                    size_t bytes = transaction(e, ARTBOX_BC_REPLY, 0, pong, sizeof(pong));
                    free_buffer(e->scratch->write + bytes, t.data_buffer);
                    REQUIRE(send(e, bytes + 12) == 0);
                } else {
                    REQUIRE(t.code == depart_code && called == 1 && !departing++);
                    free_buffer(e->scratch->write, t.data_buffer);
                    REQUIRE(send(e, 12) == 0); // Close below without replying to this final call.
                }
            } else if (frame.command == ARTBOX_BR_TRANSACTION_COMPLETE) REQUIRE(++completed <= 2);
            else if (frame.command == ARTBOX_BR_INCREFS || frame.command == ARTBOX_BR_ACQUIRE) {
                REQUIRE(get64(frame.payload) == service_pointer && get64(frame.payload + 8) == service_cookie);
                if (frame.command == ARTBOX_BR_INCREFS) REQUIRE(!increfs++);
                else REQUIRE(!acquired++);
                REQUIRE(ref_command(e, &frame) == 0);
            } else REQUIRE(frame.command == ARTBOX_BR_NOOP);
        }
        if (!size) e->ops->pause(e->context);
    }
    REQUIRE(echoed == 1 && called == 1 && departing == 1 && completed == 2 && increfs == 1 && acquired == 1);
    return 0;
}
static int object_owner_run(void *opaque) {
    struct endpoint *e = opaque;
    int result = prepare(e);
    if (!result) result = object_owner_loop(e);
    return cleanup(e, result);
}
int artbox_binder_object_check(void *context, const artbox_binder_transaction_ops *ops,
    artbox_binder_transaction_scratch *server, artbox_binder_transaction_scratch *client) {
    struct endpoint a = {context, ops, server, -1, 1, 0, ops->page_size * 16};
    struct endpoint b = {context, ops, client, -1, 2, 0, ops->page_size * 16};
    int result = prepare(&a);
    if (result || become_manager(&a)) return cleanup(&a, -1);
    int results[2] = {-1, -1};
    result = ops->parallel(context, object_manager_loop, &a, object_owner_run, &b, results);
    if (results[0] || results[1]) result = -1;
    return cleanup(&a, result);
}
