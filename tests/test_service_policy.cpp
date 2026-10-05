// SPDX-License-Identifier: MIT
#include "artbox/service_policy.h"
#include <atomic>
#include <cstdio>
#include <cstring>
#include <thread>
#include <vector>
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr,"policy line %d: %s\n",__LINE__,#x); return 1; } } while (0)
static int allowed(const artbox_service_policy *p,int32_t pid,uint32_t uid,unsigned op,const char *name) {
    return artbox_service_policy_allows(p,pid,uid,op,name,name ? std::strlen(name) : 0);
}
int main() {
    artbox_service_policy *policy=nullptr;
    CHECK(artbox_service_policy_create(nullptr,0,&policy)==0 && policy);
    CHECK(!allowed(policy,1,0,ARTBOX_SERVICE_FIND,"manager"));
    CHECK(!allowed(policy,1,0,ARTBOX_SERVICE_LIST,nullptr));
    artbox_service_policy_destroy(policy); policy=nullptr;
    char service[]="artbox.ping";
    artbox_service_rule rules[]={
        {100,1000,ARTBOX_SERVICE_ADD|ARTBOX_SERVICE_FIND,"manager",7},
        {101,1001,ARTBOX_SERVICE_ADD,service,11},
        {102,10000,ARTBOX_SERVICE_FIND,service,11},
        {102,10000,ARTBOX_SERVICE_LIST,nullptr,0}};
    CHECK(artbox_service_policy_create(rules,4,&policy)==0 && policy);
    service[0]='X'; rules[2].uid=0; // Both names and identities are owned snapshots.
    CHECK(allowed(policy,100,1000,ARTBOX_SERVICE_ADD,"manager"));
    CHECK(allowed(policy,100,1000,ARTBOX_SERVICE_FIND,"manager"));
    CHECK(allowed(policy,101,1001,ARTBOX_SERVICE_ADD,"artbox.ping"));
    CHECK(allowed(policy,102,10000,ARTBOX_SERVICE_FIND,"artbox.ping"));
    CHECK(allowed(policy,102,10000,ARTBOX_SERVICE_LIST,nullptr));
    CHECK(!allowed(policy,102,0,ARTBOX_SERVICE_FIND,"artbox.ping"));
    CHECK(!allowed(policy,101,10000,ARTBOX_SERVICE_FIND,"artbox.ping"));
    CHECK(!allowed(policy,102,10000,ARTBOX_SERVICE_ADD,"artbox.ping"));
    CHECK(!allowed(policy,100,1000,ARTBOX_SERVICE_FIND,"artbox.ping"));
    CHECK(!allowed(policy,102,10000,ARTBOX_SERVICE_FIND,"artbox.ping.extra"));
    CHECK(!allowed(policy,102,10000,ARTBOX_SERVICE_FIND,"ARTBOX.PING"));
    CHECK(!allowed(policy,102,10000,ARTBOX_SERVICE_FIND,"*"));
    CHECK(!allowed(policy,102,10000,ARTBOX_SERVICE_LIST,"artbox.ping"));
    CHECK(!allowed(policy,102,10000,ARTBOX_SERVICE_FIND,nullptr));
    CHECK(!allowed(policy,102,10000,3,"artbox.ping"));
    CHECK(!allowed(policy,102,10000,0,"artbox.ping"));
    CHECK(!allowed(policy,102,10000,8,"artbox.ping"));
    CHECK(!allowed(policy,0,10000,ARTBOX_SERVICE_FIND,"artbox.ping"));
    CHECK(!allowed(policy,102,UINT32_MAX,ARTBOX_SERVICE_FIND,"artbox.ping"));
    CHECK(!allowed(nullptr,102,10000,ARTBOX_SERVICE_FIND,"artbox.ping"));
    const char embedded[]="artbox.ping\0.extra";
    CHECK(!artbox_service_policy_allows(policy,102,10000,ARTBOX_SERVICE_FIND,embedded,sizeof(embedded)-1));
    CHECK(!artbox_service_policy_allows(policy,102,10000,ARTBOX_SERVICE_FIND,"artbox.ping",SIZE_MAX));
    std::atomic<unsigned> wrong{0};
    std::thread workers[4];
    for(auto &worker:workers) worker=std::thread([&] {
        for(unsigned i=0;i<2000;++i) {
            if (!allowed(policy,102,10000,ARTBOX_SERVICE_FIND,"artbox.ping")) ++wrong;
            if (allowed(policy,102,10001,ARTBOX_SERVICE_FIND,"artbox.ping")) ++wrong;
        }
    });
    for(auto &worker:workers) worker.join();
    CHECK(wrong==0);
    artbox_service_policy_destroy(policy); policy=nullptr;
    const artbox_service_rule invalid[]={
        {0,1000,1,"manager",7},{-1,1000,1,"manager",7},{1,UINT32_MAX,1,"manager",7},
        {1,1000,0,"manager",7},{1,1000,8,"manager",7},{1,1000,5,"manager",7},
        {1,1000,4,"manager",7},{1,1000,1,nullptr,7},{1,1000,1,"manager",0},
        {1,1000,1,"manager",SIZE_MAX},{1,1000,1,"*",1},{1,1000,1,"a\0b",3},
        {1,1000,1,"space name",10},{1,1000,1,"\xff",1}};
    for(const auto &rule:invalid) {
        CHECK(artbox_service_policy_create(&rule,1,&policy)==-22 && !policy);
    }
    artbox_service_rule duplicate[]={rules[0],rules[0]};
    duplicate[1].operations=ARTBOX_SERVICE_FIND;
    CHECK(artbox_service_policy_create(duplicate,2,&policy)==-22 && !policy);
    CHECK(artbox_service_policy_create(nullptr,1,&policy)==-22 && !policy);
    CHECK(artbox_service_policy_create(nullptr,SIZE_MAX,&policy)==-22 && !policy);
    CHECK(artbox_service_policy_create(nullptr,0,nullptr)==-22);
    char maximum[128]; std::memset(maximum,'a',127);maximum[127]='\0';
    artbox_service_rule boundary={INT32_MAX,UINT32_MAX-1,1,maximum,127};
    CHECK(artbox_service_policy_create(&boundary,1,&policy)==0);
    CHECK(allowed(policy,INT32_MAX,UINT32_MAX-1,1,maximum));
    artbox_service_policy_destroy(policy); policy=nullptr;
    boundary.name_length=128;
    CHECK(artbox_service_policy_create(&boundary,1,&policy)==-22 && !policy);
    std::vector<artbox_service_rule> full(1024);
    for (size_t i=0;i<full.size();++i)
        full[i]={(int32_t)i+1,1000,ARTBOX_SERVICE_FIND,"manager",7};
    CHECK(artbox_service_policy_create(full.data(),full.size(),&policy)==0);
    CHECK(allowed(policy,1024,1000,ARTBOX_SERVICE_FIND,"manager"));
    CHECK(!allowed(policy,1025,1000,ARTBOX_SERVICE_FIND,"manager"));
    artbox_service_policy_destroy(policy); policy=nullptr;
    CHECK(artbox_service_policy_create(full.data(),1025,&policy)==-22 && !policy);
    artbox_service_policy_destroy(nullptr);
    std::puts("Service policy: exact roles/names, default deny, owned configuration and concurrent queries passed");
    return 0;
}
