#include "../../tests/modules/prelude.h"
import Creator.NodeMapProbe;
template<class K,class V,class H=ce::hash<K>,class E=ce::equal_to<K>,class A=ce::heap_allocator>
using probe_map=ce::experiment::measured_node_map<K,V,H,E,A,PROBE_REUSE,PROBE_ERASE>;
// Named module supplies API
#include <algorithm>
#include <concepts>
#include <cstdio>
#include <iterator>
#include <memory>
#include <stdexcept>
#include <type_traits>
#include <unordered_map>

static void check(bool condition){if(!condition)throw std::runtime_error("node map regression");}
struct collision_hash {uint64_t operator()(int)const noexcept{return 0;}};
using map_type=probe_map<int,int,collision_hash>;
static_assert(std::forward_iterator<map_type::iterator>);
static_assert(std::forward_iterator<map_type::const_iterator>);
static_assert(!std::is_assignable_v<decltype((std::declval<map_type::iterator>()->first)),int>);
static_assert(!std::is_copy_constructible_v<probe_map<int,std::unique_ptr<int>>>);
struct ledger{size_t allocations=0,live=0;int remaining=-1;};
struct tracked_allocator {
    ledger* state=nullptr;
    void* allocate(size_t n,size_t a){
        if(state && state->remaining==0)throw std::bad_alloc{};
        if(state && state->remaining>0)--state->remaining;
        auto* p=ce::heap_allocator{}.allocate(n,a);if(state){++state->allocations;++state->live;}return p;
    }
    void deallocate(void* p,size_t n,size_t a)noexcept{
        ce::heap_allocator{}.deallocate(p,n,a);if(state)--state->live;
    }
};
struct object {
    static inline int alive=0,copies=-1;
    int value=0;
    explicit object(int n):value(n){++alive;}
    object(const object& other):value(other.value){if(copies==0)throw 1;if(copies>0)--copies;++alive;}
    object(object&& other)noexcept:value(other.value){++alive;}
    ~object(){--alive;}
};
int main(){
    {
        map_type tomb;tomb.try_emplace(1,10);tomb.try_emplace(2,20);tomb.try_emplace(3,30);
        auto* stable=&tomb.at(3);tomb.erase(1);
        auto duplicate=tomb.try_emplace(3,999);
        check(!duplicate.second && tomb.size()==2 && tomb.at(3)==30);
        tomb.try_emplace(4,40);check(tomb.at(4)==40 && stable==&tomb.at(3));
    }
    map_type own{{{1,10},{2,20}}};
    auto* stable=&own.at(2);
    own.reserve(1000);
    for(int i=3;i<500;++i)own.try_emplace(i,i*10);
    check(stable==&own.at(2) && *stable==20);
    auto copy=own;check(copy==own);
    auto moved=std::move(copy);check(moved==own && copy.empty());
    moved.erase(moved.begin(),moved.end());check(moved.empty());
    std::unordered_map<int,int,collision_hash> expected;
    own.clear();uint32_t random=1234;
    for(int step=0;step<10000;++step){
        random=random*1664525u+1013904223u;const auto key=int(random%128);
        if((random>>16)&1){own.insert_or_assign(key,step);expected.insert_or_assign(key,step);}
        else check(own.erase(key)==expected.erase(key));
        check(own.size()==expected.size());
        for(const auto& [k,v]:expected){auto found=own.find(k);check(found!=own.end() && found->second==v);}
    }
    probe_map<ce::string,int> names;
    names.try_emplace("name",7);check(names.contains("name") && names.at("name")==7);
    probe_map<int,std::unique_ptr<int>> owners;
    owners.try_emplace(1,std::make_unique<int>(4));check(*owners.at(1)==4);
    {
        probe_map<int,object> values;values.try_emplace(1,11);values.try_emplace(2,22);
        object::copies=1;bool threw=false;
        try{auto failed=values;(void)failed;}catch(int){threw=true;}
        check(threw && object::alive==2 && values.at(1).value==11);object::copies=-1;
    }
    check(object::alive==0);
    ledger state;
    {
        using tracked_map=probe_map<int,int,collision_hash,ce::equal_to<int>,tracked_allocator>;
        tracked_map tracked(0,{}, {},tracked_allocator{&state});
        for(int i=0;i<6;++i)tracked.try_emplace(i,i);
        const auto buckets=tracked.bucket_count();const auto live=state.live;
        for(int budget:{0,1}){
            state.remaining=budget;bool threw=false;
            try{tracked.try_emplace(100,100);}catch(const std::bad_alloc&){threw=true;}
            state.remaining=-1;
            check(threw && tracked.size()==6 && tracked.bucket_count()==buckets && state.live==live);
        }
        const auto before=state.allocations;
        for(int i=0;i<100;++i){tracked.erase(2);tracked.try_emplace(2,i);}
        check(state.allocations-before==100); // no table rebuild for reuse of one erased bucket.
    }
    check(state.live==0);
    std::puts("NODE_MAP_MIGRATION_CORRECTNESS_PASS");
}
