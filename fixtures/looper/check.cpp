// SPDX-License-Identifier: MIT
// One caller for the original Linux Looper and the signed Android guest.
#include "check.h"
#include <utils/Looper.h>
#include <android-base/unique_fd.h>
#include <pthread.h>
#include <sys/eventfd.h>
#include <sys/timerfd.h>
#include <time.h>
#include <unistd.h>

using android::Looper;
using android::Message;
using android::MessageHandler;
using android::sp;
using android::base::unique_fd;

namespace {
constexpr int64_t second = 1000000000;
int64_t now() { return systemTime(SYSTEM_TIME_MONOTONIC); }

struct Messages : MessageHandler {
    int values[8] = {};
    unsigned count = 0;
    void handleMessage(const Message& message) override {
        if (count < 8) values[count] = message.what;
        ++count;
    }
};

struct Event {
    int fd;
    int keep;
    unsigned calls = 0;
    bool valid = true;
    uint64_t value = 0;
};
int callback(int fd, int events, void* opaque) {
    auto* event = static_cast<Event*>(opaque);
    ++event->calls;
    uint64_t value = 0;
    event->valid &= fd == event->fd && events == Looper::EVENT_INPUT &&
                    read(fd, &value, sizeof(value)) == sizeof(value);
    event->value += value;
    return event->keep;
}

int pollWithoutWake(const sp<Looper>& looper, int timeoutMillis) {
    const auto end = now() + int64_t(timeoutMillis) * 1000000;
    int result;
    do {
        result = looper->pollOnce(toMillisecondTimeoutDelay(now(), end));
    } while (result == Looper::POLL_WAKE && now() < end);
    return result;
}

struct Wake {
    Looper* owner;
    bool omit;
    bool valid = false;
};
void* wakeWorker(void* opaque) {
    auto* work = static_cast<Wake*>(opaque);
    // The actual pthread TLS owner must be independent of the polling thread.
    if (Looper::getForThread() != nullptr) return nullptr;
    auto own = Looper::prepare(0);
    if (own == nullptr || own.get() == work->owner || own->getAllowNonCallbacks()) return nullptr;
    Looper::setForThread(nullptr);
    if (Looper::getForThread() != nullptr) return nullptr;
    const auto end = now() + second;
    while (!work->owner->isPolling() && now() < end) {
        const timespec pause{0, 100000};
        nanosleep(&pause, nullptr);
    }
    work->valid = work->owner->isPolling();
    if (work->valid && !work->omit) work->owner->wake();
    return nullptr;
}

// Clear thread ownership on every return, including negative controls.
struct ThreadOwner {
    ~ThreadOwner() { Looper::setForThread(nullptr); }
};
} // namespace

extern "C" int artbox_native_looper_check(unsigned mutation, artbox_looper_result* result) {
    if (!result) return -1;
    *result = {};
#define REQUIRE(condition, id) do { if (!(condition)) { result->failure = id; return -(id); } ++result->cases; } while (0)
    REQUIRE(mutation <= 2, 1);
    REQUIRE(Looper::getForThread() == nullptr, 100);
    ThreadOwner owner;
    auto looper = Looper::prepare(Looper::PREPARE_ALLOW_NON_CALLBACKS);
    REQUIRE(looper != nullptr && looper->getAllowNonCallbacks(), 101);
    REQUIRE(Looper::getForThread() == looper && Looper::prepare(Looper::PREPARE_ALLOW_NON_CALLBACKS) == looper, 102);
    int fd = -1, events = -1;
    void* data = result;
    REQUIRE(looper->pollOnce(0, &fd, &events, &data) == Looper::POLL_TIMEOUT, 103);
    REQUIRE(fd == 0 && events == 0 && data == nullptr, 104);
    looper->wake();
    looper->wake();
    REQUIRE(looper->pollOnce(0) == Looper::POLL_WAKE, 105);
    REQUIRE(looper->pollOnce(0) == Looper::POLL_TIMEOUT, 106);

    Wake work{looper.get(), mutation == 2};
    pthread_t thread;
    REQUIRE(pthread_create(&thread, nullptr, wakeWorker, &work) == 0, 107);
    const int woke = looper->pollOnce(1500);
    const int joined = pthread_join(thread, nullptr);
    REQUIRE(joined == 0 && work.valid, 108);
    REQUIRE(woke == Looper::POLL_WAKE, 109);
    result->wake_threads = 1;
    REQUIRE(looper->pollOnce(0) == Looper::POLL_TIMEOUT, 110);

    unique_fd event(eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC));
    REQUIRE(event.get() >= 0, 200);
    Event notice{event.get(), mutation == 1 ? 1 : 0};
    REQUIRE(looper->addFd(event.get(), 0, Looper::EVENT_INPUT, callback, &notice) == 1, 201);
    const uint64_t five = 5;
    REQUIRE(write(event.get(), &five, sizeof(five)) == sizeof(five), 202);
    REQUIRE(looper->pollOnce(0) == Looper::POLL_CALLBACK, 203);
    REQUIRE(notice.calls == 1 && notice.valid && notice.value == five, 204);
    result->fd_callbacks = notice.calls;
    REQUIRE(looper->removeFd(event.get()) == 0, 205);
    REQUIRE(looper->pollOnce(0) == Looper::POLL_TIMEOUT, 206);

    REQUIRE(looper->addFd(event.get(), 7, Looper::EVENT_INPUT, nullptr, &notice) == 1, 210);
    REQUIRE(write(event.get(), &five, sizeof(five)) == sizeof(five), 211);
    REQUIRE(looper->pollOnce(0, &fd, &events, &data) == 7, 212);
    REQUIRE(fd == event.get() && events == Looper::EVENT_INPUT && data == &notice, 213);
    REQUIRE(looper->pollOnce(0) == 7, 214); // Level-triggered until consumed.
    REQUIRE(looper->addFd(event.get(), 9, Looper::EVENT_INPUT, nullptr, result) == 1, 215);
    REQUIRE(looper->pollOnce(0, &fd, &events, &data) == 9 && data == result, 216);
    uint64_t consumed = 0;
    REQUIRE(read(event.get(), &consumed, sizeof(consumed)) == sizeof(consumed) && consumed == five, 217);
    REQUIRE(looper->removeFd(event.get()) == 1 && looper->removeFd(event.get()) == 0, 218);
    REQUIRE(looper->pollOnce(0) == Looper::POLL_TIMEOUT, 219);
    auto callbacksOnly = sp<Looper>::make(false);
    REQUIRE(callbacksOnly->addFd(event.get(), 3, Looper::EVENT_INPUT, nullptr, nullptr) == -1, 220);

    auto messages = sp<Messages>::make();
    const auto past = now() - 1;
    looper->sendMessageAtTime(past, messages, Message(10));
    looper->sendMessageAtTime(past, messages, Message(20));
    looper->sendMessageAtTime(past, messages, Message(30));
    looper->removeMessages(messages, 20);
    REQUIRE(looper->pollOnce(0) == Looper::POLL_CALLBACK, 300);
    REQUIRE(messages->count == 2 && messages->values[0] == 10 && messages->values[1] == 30, 301);
    looper->sendMessageDelayed(2000000, messages, Message(40));
    REQUIRE(pollWithoutWake(looper, 1000) == Looper::POLL_CALLBACK, 302);
    REQUIRE(messages->count == 3 && messages->values[2] == 40, 303);
    looper->sendMessageAtTime(now() + second, messages, Message(50));
    looper->removeMessages(messages);
    REQUIRE(pollWithoutWake(looper, 5) == Looper::POLL_TIMEOUT && messages->count == 3, 304);
    result->message_calls = messages->count;

    unique_fd timer(timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC));
    REQUIRE(timer.get() >= 0, 400);
    Event tick{timer.get(), 1};
    REQUIRE(looper->addFd(timer.get(), 0, Looper::EVENT_INPUT, callback, &tick) == 1, 401);
    itimerspec arm{};
    arm.it_value.tv_nsec = 1000000;
    REQUIRE(timerfd_settime(timer.get(), 0, &arm, nullptr) == 0, 402);
    REQUIRE(pollWithoutWake(looper, 1000) == Looper::POLL_CALLBACK, 403);
    REQUIRE(tick.calls == 1 && tick.valid && tick.value == 1, 404);
    REQUIRE(looper->pollOnce(0) == Looper::POLL_TIMEOUT, 405);
    REQUIRE(looper->removeFd(timer.get()) == 1, 406);
    result->timer_callbacks = tick.calls;
    Looper::setForThread(nullptr);
    REQUIRE(Looper::getForThread() == nullptr, 500);
    return 0;
#undef REQUIRE
}
