#include "../../Engine/Utility_Framework/Foundation/EngineStorage.h"
#include <cstdio>
#include <random>
struct Owner { static inline int live=0; Owner(){++live;} ~Owner(){--live;} };
struct Domain { int allocated=0,freed=0,live=0,pointers=0,rejectAt=-1; Domain* copy=nullptr; };
template<class T> struct DomainAllocator
{
    using value_type=T;
    Domain* domain=nullptr;
    DomainAllocator() noexcept=default;
    explicit DomainAllocator(Domain* d) noexcept:domain(d){}
    template<class U> DomainAllocator(const DomainAllocator<U>& a) noexcept:domain(a.domain){}
    T* allocate(size_t n){auto p=static_cast<T*>(::operator new(n*sizeof(T),std::align_val_t(alignof(T))));++domain->allocated;++domain->live;return p;}
    void deallocate(T* p,size_t) noexcept {++domain->freed;--domain->live;::operator delete(p,std::align_val_t(alignof(T)));}
    template<class U> bool operator==(const DomainAllocator<U>& a)const noexcept{return domain==a.domain;}
    DomainAllocator select_on_container_copy_construction()const noexcept{return DomainAllocator(domain->copy?domain->copy:domain);}
    template<class U,class... Args> void construct(U* p,Args&&... args)
    {
        if constexpr(std::is_pointer_v<U>)
        {
            if(domain->pointers==domain->rejectAt)throw std::runtime_error("directory construction");
            ++domain->pointers;
        }
        std::construct_at(p,std::forward<Args>(args)...);
    }
    template<class U> void destroy(U* p)noexcept
    {
        if constexpr(std::is_pointer_v<U>)--domain->pointers;
        std::destroy_at(p);
    }
};
struct ByteAllocator
{
    Domain* domain=nullptr;
    void* allocate(size_t bytes,size_t alignment){auto p=::operator new(bytes,std::align_val_t(alignment));++domain->allocated;++domain->live;return p;}
    void deallocate(void* p,size_t,size_t alignment)noexcept{++domain->freed;--domain->live;::operator delete(p,std::align_val_t(alignment));}
    bool operator==(const ByteAllocator& a)const noexcept{return domain==a.domain;}
};
struct Immovable
{
    static inline bool fail=false;
    static inline int live=0;
    Immovable(){if(fail)throw std::runtime_error("expected constructor failure");++live;}
    Immovable(const Immovable&)=delete;
    ~Immovable(){--live;}
};
struct Deferred
{
    bool* locked;
    bool* destroyed;
    ~Deferred(){if(*locked)std::terminate();*destroyed=true;}
};
struct alignas(256) Aligned { unsigned value=7; };
int main()
{
    static_assert(std::random_access_iterator<ce::segmented_deque<int>::iterator>);
    static_assert(std::ranges::forward_range<ce::engine_extractable_map<int,int>>);
    size_t checks=0;
    auto check=[&](bool ok){++checks; if(!ok) throw std::runtime_error("storage mismatch");};
    ce::segmented_deque<int> actual;
    std::deque<int> expected;
    std::mt19937 random(0xCE51);
    for(int i=0;i<20000;++i)
    {
        switch(random()%6)
        {
        case 0: actual.push_back(i);expected.push_back(i);break;
        case 1: actual.push_front(i);expected.push_front(i);break;
        case 2: if(!expected.empty()){actual.pop_front();expected.pop_front();}break;
        case 3: if(!expected.empty()){actual.pop_back();expected.pop_back();}break;
        case 4: if(!expected.empty()){auto n=random()%expected.size();actual.erase(actual.begin()+n);expected.erase(expected.begin()+n);}break;
        default: actual.emplace_back(i);expected.emplace_back(i);break;
        }
        check(actual.size()==expected.size());
        check(std::equal(actual.begin(),actual.end(),expected.begin()));
    }
    ce::segmented_deque<Owner> owners;
    owners.emplace_back(); auto first=&owners.front();
    for(int i=0;i<8192;++i) owners.emplace_back();
    check(&owners.front()==first); check(Owner::live==8193);
    owners.pop_front();check(Owner::live==8192);owners.clear();check(Owner::live==0);
    auto copied=actual;check(copied==actual); auto moved=std::move(copied);check(moved==actual);check(copied.empty());
    ce::storage_queue<int> queue;queue.push(1);queue.emplace(2);check(queue.front()==1);queue.pop();check(queue.front()==2);
    ce::storage_stack<std::unique_ptr<int>> stack;stack.push(std::make_unique<int>(7));check(*stack.top()==7);stack.pop();check(stack.empty());
    ce::storage_priority_queue<int> heap;for(int i:{4,8,2})heap.push(i);check(heap.top()==8);heap.pop();check(heap.top()==4);
    ce::engine_extractable_map<int,std::unique_ptr<int>> map;
    map.reserve(32);map.try_emplace(1,std::make_unique<int>(7));map.try_emplace(2,std::make_unique<int>(9));
    auto address=&map.at(1);auto node=map.extract(1);check(map.count(1)==0);node.key()=3;
    auto result=map.insert(std::move(node));check(result.inserted);check(&map.at(3)==address);check(*map.at(3)==7);
    node=map.extract(3);node.key()=2;auto rejected=map.insert(std::move(node));check(!rejected.inserted);check(bool(rejected.node));
    rejected.node.key()=1;check(map.insert(std::move(rejected.node)).inserted);check(&map.at(1)==address);
    Domain a,b;a.copy=&b;
    {
        ce::segmented_deque<int,DomainAllocator<int>> source{DomainAllocator<int>(&a)};
        for(int i=0;i<1024;++i)source.push_back(i);
        const auto allocations=a.allocated;
        size_t sum=0;for(int i=0;i<8192;++i)sum+=source[size_t(i)%source.size()];
        check(sum>0&&a.allocated==allocations);
        auto copy=source;check(copy.get_allocator().domain==&b);check(copy==source);
        ce::segmented_deque<int,DomainAllocator<int>> target{DomainAllocator<int>(&b)};
        target=std::move(source);check(target.get_allocator().domain==&b);check(source.empty());check(target==copy);
        source.push_back(7);bool rejectedSwap=false;
        try{source.swap(target);}catch(const std::invalid_argument&){rejectedSwap=true;}
        check(rejectedSwap&&source.front()==7&&target.size()==1024);
    }
    check(a.live==0&&b.live==0&&a.allocated==a.freed&&b.allocated==b.freed);
    check(a.pointers==0&&b.pointers==0);
    Domain rejectedDirectory;rejectedDirectory.rejectAt=3;
    {
        ce::segmented_deque<int,DomainAllocator<int>> values{DomainAllocator<int>(&rejectedDirectory)};
        bool threw=false;try{values.emplace_back(7);}catch(const std::runtime_error&){threw=true;}
        check(threw&&values.empty()&&rejectedDirectory.live==0&&rejectedDirectory.pointers==0);
        rejectedDirectory.rejectAt=-1;values.emplace_back(9);check(values.front()==9);
    }
    check(rejectedDirectory.live==0&&rejectedDirectory.pointers==0);
    {
        ce::segmented_deque<Immovable> values;values.emplace_back();auto old=&values.front();
        Immovable::fail=true;try{values.emplace_back();check(false);}catch(const std::runtime_error&){}
        check(values.size()==1&&&values.front()==old&&Immovable::live==1);Immovable::fail=false;
    }
    check(Immovable::live==0);
    {
        using Map=ce::extractable_hash_map<int,int,ce::hash<int>,ce::equal_to<int>,ByteAllocator,false>;
        Map source{ByteAllocator{&a}},target{ByteAllocator{&b}};
        source.reserve(32);source.try_emplace(1,7);auto value=*source.begin();
        const auto allocations=a.allocated;
        for(int i=0;i<8192;++i)check(source.at(1)==7&&!source.contains(9));
        auto domainNode=source.extract(1);domainNode.key()=3;
        bool rejectedDomain=false;try{target.insert(std::move(domainNode));}catch(const std::invalid_argument&){rejectedDomain=true;}
        check(rejectedDomain&&domainNode&&domainNode.key()==3&&target.empty());
        check(source.insert(std::move(domainNode)).inserted&&a.allocated==allocations);
        source.clear();check(value.first==1&&value.second==7);
    }
    check(a.live==0&&b.live==0);
    bool locked=false,destroyed=false;
    {
        ce::engine_extractable_map<int,std::unique_ptr<Deferred>> values;
        values.try_emplace(1,std::make_unique<Deferred>(&locked,&destroyed));
        decltype(values)::node_type retired;
        locked=true;retired=values.extract(1);check(!destroyed);locked=false;
    }
    check(destroyed);
    ce::engine_extractable_map<int,int> ranges;ranges.emplace(1,7);ranges.emplace(2,9);
    int valuesSum=0;for(auto& [key,value]:ranges){check(key==1||key==2);valuesSum+=value;}
    check(valuesSum==16);
    valuesSum=0;for(auto& value:ranges|std::views::transform([](auto& entry)->auto&{return entry.second;}))valuesSum+=value;
    check(valuesSum==16);
    ce::segmented_deque<int> erased{1,2,3,4};check(ce::erase_if(erased,[](int v){return v%2==0;})==2);check(erased==ce::segmented_deque<int>{1,3});
    ce::segmented_deque<int> frontGrowth;std::deque<int> frontModel;
    frontGrowth.push_back(-1);frontModel.push_back(-1);const auto* oldest=&frontGrowth.back();
    for(int i=0;i<8192;++i){frontGrowth.push_front(i);frontModel.push_front(i);}
    check(&frontGrowth.back()==oldest&&std::equal(frontGrowth.begin(),frontGrowth.end(),frontModel.begin()));
    ce::segmented_deque<Aligned> aligned;aligned.emplace_back();
    check(reinterpret_cast<uintptr_t>(&aligned.front())%256==0&&aligned.front().value==7);
    Domain growing;
    {
        ce::segmented_deque<int,DomainAllocator<int>> values{DomainAllocator<int>(&growing)};
        for(int i=0;i<200;++i)values.push_back(i);
        const auto* original=&values.front();const auto pointers=growing.pointers;growing.rejectAt=pointers+3;
        bool threw=false;try{for(int i=200;i<400;++i)values.push_back(i);}catch(const std::runtime_error&){threw=true;}
        check(threw&&growing.pointers==pointers&&&values.front()==original&&values.back()==int(values.size()-1));
        growing.rejectAt=-1;values.push_back(999);check(&values.front()==original&&values.back()==999);
    }
    check(growing.live==0&&growing.pointers==0);
    std::printf("STORAGE_MIGRATION_OK checks=%zu\n",checks);
}
