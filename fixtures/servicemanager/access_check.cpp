// Original contract for the Access adapter, not a substitute Binder service. MIT.
#include "artbox_access.h"
#include "Access.h"
#include <unistd.h>
#define CHECK(n, condition) do { if (!(condition)) return -(100+(n)); ++cases; } while (0)
extern "C" int artbox_service_access_prepare(unsigned mutation) {
    if (mutation>2) return -1;
    artbox_service_rule rules[]={
        {mutation==2 ? 99 : 100,1000,ARTBOX_SERVICE_ADD|ARTBOX_SERVICE_FIND,"manager",7},
        {101,1001,ARTBOX_SERVICE_ADD,"artbox.ping",11},
        {102,mutation==1 ? 10001u : 10000u,ARTBOX_SERVICE_FIND,"artbox.ping",11},
        {102,10000,ARTBOX_SERVICE_FIND,"manager",7},
        {102,10000,ARTBOX_SERVICE_LIST,nullptr,0}};
    return artbox_service_access_configure(rules,5);
}
extern "C" int artbox_service_access_check(unsigned mutation) {
    if (mutation>2) return -1;
    int cases=0;
    android::Access access;
    const android::Access::CallingContext manager{100,1000,{}}, provider{101,1001,{}},
        client{102,10000,{}}, root{100,0,{}}, unknown{103,10000,{}};
    CHECK(1,!access.canAdd(manager,"manager"));
    CHECK(2,!access.canFind(manager,"manager"));
    CHECK(3,!access.canList(manager));
    CHECK(4,!access.canAdd(root,"manager"));
    CHECK(5,artbox_service_access_prepare(mutation)==0);
    CHECK(6,artbox_service_access_prepare(mutation)==-114);
    CHECK(7,access.canAdd(manager,"manager"));
    CHECK(8,access.canAdd(provider,"artbox.ping"));
    CHECK(9,access.canFind(client,"artbox.ping"));
    CHECK(10,access.canList(client));
    CHECK(11,!access.canAdd(client,"artbox.ping"));
    CHECK(12,!access.canFind(unknown,"artbox.ping"));
    CHECK(13,!access.canAdd(root,"manager"));
    CHECK(14,!access.canFind(client,"ARTBOX.PING"));
    CHECK(15,!access.canFind(client,"artbox.ping.extra"));
    CHECK(16,!access.canFind(client,std::string("artbox.ping\0extra",17)));
    // This deliberately uses the real IPCThreadState and its actual Bionic
    // identity. The owner supplies PID100/UID1000 and an attached Binder VFS.
    const auto caller=access.getCallingContext();
    CHECK(17,caller.debugPid==100 && getpid()==100);
    CHECK(18,caller.uid==1000 && getuid()==1000 && caller.sid.empty());
    CHECK(19,access.canAdd(caller,"manager"));
    CHECK(20,caller.toDebugString().find("pid=100,uid=1000")!=std::string::npos);
    return cases;
}
#undef CHECK
