// Original native Binder acceptance callers. SPDX-License-Identifier: MIT
#include <android/os/IServiceManager.h>
#include <binder/Binder.h>
#include <binder/IPCThreadState.h>
#include <binder/Parcel.h>
#include <binder/ProcessState.h>
#include <utils/Looper.h>
#include <utils/Timers.h>
#include <algorithm>
#include <cstdio>
#include <unistd.h>

using namespace android;
namespace {
constexpr uint32_t ping=IBinder::FIRST_CALL_TRANSACTION, finish=ping+1;
constexpr int rounds=32;
constexpr int64_t second=1000000000;
struct Pump {
    sp<Looper> looper;
    int fd=-1, error=0;
    static int callback(int, int events, void *opaque) {
        auto *self=static_cast<Pump *>(opaque);
        if (events!=Looper::EVENT_INPUT) { self->error=-1; return 0; }
        self->error=IPCThreadState::self()->handlePolledCommands();
        return self->error==0 ? 1 : 0;
    }
    bool setup() {
        ProcessState::self()->setThreadPoolMaxThreadCount(0);
        IPCThreadState::self()->disableBackgroundScheduling(true);
        looper=Looper::prepare(false);
        return IPCThreadState::self()->setupPolling(&fd)==0 && fd>=0 &&
            looper->addFd(fd,Looper::POLL_CALLBACK,Looper::EVENT_INPUT,callback,this)==1;
    }
    ~Pump() { if (looper!=nullptr && fd>=0) looper->removeFd(fd); }
};
struct Ping : BBinder {
    bool done=false;
    int calls=0;
    bool valid=true;
    status_t onTransact(uint32_t code,const Parcel &data,Parcel *reply,uint32_t flags) override {
        if (code!=ping && code!=finish) return BBinder::onTransact(code,data,reply,flags);
        auto *ipc=IPCThreadState::self();
        if (code==finish) {
            // Linux omits the originating PID for one-way transactions.
            valid &= ipc->getCallingPid()==0 && ipc->getCallingUid()==10000 &&
                     (flags & IBinder::FLAG_ONEWAY);
            done=true; return NO_ERROR;
        }
        valid &= ipc->getCallingPid()==102 && ipc->getCallingUid()==10000;
        const int32_t sequence=data.readInt32();
        const int64_t payload=data.readInt64();
        // Payload identity bytes cannot replace transaction credentials.
        const int32_t fakePid=data.readInt32(),fakeUid=data.readInt32();
        valid &= sequence==calls && payload==INT64_C(0x1234567800000000)+sequence &&
                 fakePid==100 && fakeUid==0 && data.dataAvail()==0;
        ++calls;
        if (!reply || !valid) return BAD_VALUE;
        reply->writeInt32(sequence);
        reply->writeInt64(payload ^ INT64_C(0x55aa55aa));
        reply->writeInt32(ipc->getCallingPid());
        reply->writeInt32(static_cast<int32_t>(ipc->getCallingUid()));
        return NO_ERROR;
    }
};
struct Death : IBinder::DeathRecipient {
    unsigned calls=0;
    wp<IBinder> expected;
    bool identity=true;
    void binderDied(const wp<IBinder> &who) override { ++calls; identity &= who==expected; }
};
sp<os::IServiceManager> manager() {
    return interface_cast<os::IServiceManager>(ProcessState::self()->getContextObject(nullptr));
}
}

extern "C" int artbox_service_provider() {
    if (getpid()!=101 || getuid()!=1001) return -301;
    Pump pump;
    if (!pump.setup()) return -302;
    auto sm=manager(); auto object=sp<Ping>::make();
    if (sm==nullptr) return -303;
    std::vector<std::string> names;
    if (sm->listServices(os::IServiceManager::DUMP_FLAG_PRIORITY_ALL,&names).exceptionCode()!=
            binder::Status::EX_SECURITY) return -304;
    if (sm->addService("artbox.denied",object,false,8).exceptionCode()!=binder::Status::EX_SECURITY)
        return -305;
    if (!sm->addService("artbox.ping",object,false,8).isOk()) return -306;
    std::puts("ARTBox provider registered");
    const auto deadline=systemTime(SYSTEM_TIME_MONOTONIC)+15*second;
    while (!object->done && !pump.error && systemTime(SYSTEM_TIME_MONOTONIC)<deadline)
        pump.looper->pollOnce(20);
    if (!object->done || !object->valid || object->calls!=rounds || pump.error) return -307;
    IPCThreadState::self()->flushCommands();
    // The native owner joins this entry before closing descriptors/unmapping.
    // Endpoint death must come from that teardown, never a synthetic callback.
    return rounds;
}

extern "C" int artbox_service_client() {
    if (getpid()!=102 || getuid()!=10000) return -201;
    Pump pump;
    if (!pump.setup()) return -202;
    auto sm=manager();
    if (sm==nullptr) return -203;
    sp<IBinder> service;
    const auto deadline=systemTime(SYSTEM_TIME_MONOTONIC)+15*second;
    do {
        if (!sm->checkService("artbox.ping",&service).isOk()) return -204;
        if (service!=nullptr) break;
        pump.looper->pollOnce(5);
    } while (systemTime(SYSTEM_TIME_MONOTONIC)<deadline);
    if (service==nullptr || service->localBinder()!=nullptr) return -205;
    auto death=sp<Death>::make(); death->expected=service;
    if (service->linkToDeath(death)!=NO_ERROR) return -206;
    for (int32_t i=0;i<rounds;++i) {
        Parcel input,output;
        const int64_t payload=INT64_C(0x1234567800000000)+i;
        input.writeInt32(i); input.writeInt64(payload);
        input.writeInt32(100); input.writeInt32(0);
        if (service->transact(ping,input,&output)!=NO_ERROR || output.readInt32()!=i ||
                output.readInt64()!=(payload ^ INT64_C(0x55aa55aa)) ||
                output.readInt32()!=102 || output.readInt32()!=10000 || output.dataAvail()!=0)
            return -207;
    }
    auto fake=sp<BBinder>::make();
    if (sm->addService("artbox.ping",fake,false,8).exceptionCode()!=binder::Status::EX_SECURITY)
        return -208;
    std::vector<std::string> names;
    if (!sm->listServices(os::IServiceManager::DUMP_FLAG_PRIORITY_ALL,&names).isOk() ||
            std::find(names.begin(),names.end(),"manager")==names.end() ||
            std::find(names.begin(),names.end(),"artbox.ping")==names.end()) return -209;
    sp<IBinder> unknown;
    if (!sm->checkService("artbox.denied",&unknown).isOk() || unknown!=nullptr) return -210;
    Parcel empty;
    if (service->transact(finish,empty,nullptr,IBinder::FLAG_ONEWAY)!=NO_ERROR) return -211;
    while (!death->calls && !pump.error && systemTime(SYSTEM_TIME_MONOTONIC)<deadline)
        pump.looper->pollOnce(20);
    if (death->calls!=1 || !death->identity || pump.error || service->isBinderAlive()) return -212;
    Parcel deadReply;
    if (service->transact(ping,empty,&deadReply)!=DEAD_OBJECT) return -213;
    std::puts("ARTBox Binder ping/pong and death verified");
    IPCThreadState::self()->flushCommands();
    return rounds;
}
