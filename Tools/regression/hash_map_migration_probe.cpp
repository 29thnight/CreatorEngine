#include "../../Engine/Utility_Framework/Foundation/EngineNodeMap.h"
#include "../../Engine/Utility_Framework/Reflection/ReflectionContainer.h"
#if defined(_DEBUG)
#include <crtdbg.h>
namespace {
thread_local bool observe=false;
thread_local unsigned allocations=0,frees=0;
int Hook(int kind,void*,size_t,int,long,const unsigned char*,int){
    if(observe && (kind==_HOOK_ALLOC || kind==_HOOK_REALLOC))++allocations;
    if(observe && kind==_HOOK_FREE)++frees;
    return 1;
}
struct Scope {
    _CRT_ALLOC_HOOK old;
    Scope():old(_CrtSetAllocHook(Hook)){allocations=frees=0;observe=true;}
    ~Scope(){observe=false;_CrtSetAllocHook(old);}
};
}
#endif
namespace {
unsigned checks=0;
void Check(bool value,const char* message){++checks;if(!value)throw std::runtime_error(message);}
struct Owner {
    static inline unsigned live=0;
    Owner(){++live;} ~Owner(){--live;}
};
struct CollisionHash { size_t operator()(unsigned)const noexcept{return 3;} };
}
int main() try {
    static_assert(meta::container::KeyedRange<ce::engine_node_map<std::string,int>>);
    static_assert(meta::container::FillableKeyed<ce::engine_node_map<std::string,int>>);
    ce::engine_node_map<std::string,int> names;
    std::string source(192,'x'); source.back()='a';
    names.try_emplace(source,17);
    const auto original=source;
    source.assign(192,'z');
    const std::string envelope="!"+original+"?";
    const std::string_view view(envelope.data()+1,original.size()); // deliberately not NUL-terminated
    const std::string missing(192,'q');
    Check(names.at(view)==17 && !names.contains(std::string_view(missing)),"owned keys do not borrow caller buffers");
    Check(ce::engine_node_hash<std::string>{}(view)==std::hash<std::string>{}(original),"string hash preserves standard key hash");
    const std::wstring wide(192,L'x');
    ce::engine_node_map<std::wstring,int> wideNames;wideNames.try_emplace(wide,9);
    Check(ce::engine_node_hash<std::wstring>{}(std::wstring_view(wide))==std::hash<std::wstring>{}(wide),"wide string hash preserves standard key hash");
    bool found=true;
#if defined(_DEBUG)
    unsigned readAllocations=0;
    {
        Scope scope;
#endif
        for(unsigned i=0;i<8192;++i){
            found &= names.find(view)!=names.end() && names.at(view)==17;
            found &= !names.contains(std::string_view(missing));
            found &= wideNames.at(std::wstring_view(wide))==9;
        }
#if defined(_DEBUG)
        readAllocations=allocations;
    }
    Check(readAllocations==0,"heterogeneous long string hit/miss queries allocate zero");
#endif
    Check(found,"all query results match");
    bool missingRejected=false;
    try { (void)names.at(std::string_view(missing)); }
    catch(const std::out_of_range&) { missingRejected=true; }
    Check(missingRejected,"mutable at preserves standard out_of_range");
    missingRejected=false;
    try { (void)std::as_const(names).at(std::string_view(missing)); }
    catch(const std::out_of_range&) { missingRejected=true; }
    Check(missingRejected,"const at preserves standard out_of_range");
    const auto* address=&*names.find(view);
    names.reserve(4096);
    Check(&*names.find(view)==address,"rehash preserves key/value node address");
    ce::dynamic_array<std::pair<std::string,int>> snapshot;
    snapshot.reserve(names.size());
    for(const auto& entry:names) snapshot.emplace_back(entry.first,entry.second);
    Check(snapshot.size()==1 && snapshot.front().first==original && snapshot.front().second==17,
        "explicit pair snapshot preserves key/value contents");
    const std::string embedded("ab\0cd",5);
    names.try_emplace(embedded,23);
    Check(names.at(std::string_view(embedded.data(),embedded.size()))==23 &&
        !names.contains(std::string_view(embedded.data(),2)),"embedded NUL and view length remain part of key identity");
    ce::engine_node_map<unsigned,unsigned,CollisionHash> actual;
    std::unordered_map<unsigned,unsigned,CollisionHash> expected;
    std::uint32_t random=0x71b984;
    for(unsigned i=0;i<8192;++i){
        random=random*1664525u+1013904223u;const auto key=(random>>8)%127;
        if(random&1){actual.insert_or_assign(key,i);expected.insert_or_assign(key,i);}
        else Check(actual.erase(key)==expected.erase(key),"collision erase agrees with STL");
        Check(actual.size()==expected.size(),"collision size agrees with STL");
        for(const auto& pair:expected)Check(actual.at(pair.first)==pair.second,"collision values agree with STL");
    }
    auto copy=actual;ce::engine_node_map<unsigned,unsigned,CollisionHash> moved=std::move(copy);
    Check(moved==actual && copy.empty(),"copy and move preserve values");
    ce::engine_node_map<unsigned,std::shared_ptr<Owner>> owners;
    owners.try_emplace(1,std::make_shared<Owner>());
    auto retained=owners.at(1);
    owners.erase(1);
    Check(Owner::live==1 && owners.empty(),"erase does not destroy an externally owned object");
    retained.reset();Check(Owner::live==0,"last owner releases object");
    ce::engine_node_map<unsigned,std::unique_ptr<Owner>> unique;
    unique.try_emplace(1,std::make_unique<Owner>());unique.clear();
    Check(Owner::live==0,"clear destroys unique owner values immediately");
    ce::engine_node_map<unsigned,unsigned> nodes;nodes.reserve(64);nodes.try_emplace(7,1);
#if defined(_DEBUG)
    unsigned nodeFrees=0;
    {
        Scope scope;
        nodes.erase(7);
        nodeFrees=frees;
    }
    Check(nodeFrees==1,"erase immediately frees one tracking node");
    for(unsigned i=0;i<32;++i)nodes.try_emplace(i,i);
    const auto buckets=nodes.bucket_count();
    {
        Scope scope;
        nodes.clear();
        nodeFrees=frees;
    }
    Check(nodeFrees==32 && nodes.bucket_count()==buckets,"clear frees all tracking nodes while retaining index capacity");
#else
    nodes.erase(7);
#endif
    std::cout<<"HASH_MAP_MIGRATION_OK checks="<<checks<<" modelOperations=8192 stringReads=8192 ownership=1 stableNodes=1";
#if defined(_DEBUG)
    std::cout<<" allocationChecks=1 nodeFreeChecks=2\n";
#else
    std::cout<<" allocationChecks=0 nodeFreeChecks=0\n";
#endif
    return 0;
}catch(const std::exception& error){std::cerr<<"HASH_MAP_MIGRATION_FAIL "<<error.what()<<'\n';return 1;}
