#include "../../Engine/Utility_Framework/Foundation/EngineStorage.h"
struct Bytes
{
    size_t current=0,peak=0,allocations=0,releases=0;
    void add(size_t n){current+=n;peak=std::max(peak,current);++allocations;}
    void remove(size_t n){current-=n;++releases;}
};
template<class T> struct CountingAllocator
{
    using value_type=T;
    Bytes* bytes=nullptr;
    CountingAllocator()noexcept=default;
    explicit CountingAllocator(Bytes* p)noexcept:bytes(p){}
    template<class U> CountingAllocator(const CountingAllocator<U>& a)noexcept:bytes(a.bytes){}
    T* allocate(size_t n){auto p=std::allocator<T>{}.allocate(n);bytes->add(n*sizeof(T));return p;}
    void deallocate(T* p,size_t n)noexcept{std::allocator<T>{}.deallocate(p,n);bytes->remove(n*sizeof(T));}
    template<class U> bool operator==(const CountingAllocator<U>& a)const noexcept{return bytes==a.bytes;}
};
struct ByteCounter
{
    Bytes* bytes=nullptr;
    void* allocate(size_t n,size_t alignment){auto p=::operator new(n,std::align_val_t(alignment));bytes->add(n);return p;}
    void deallocate(void* p,size_t n,size_t alignment)noexcept{::operator delete(p,std::align_val_t(alignment));bytes->remove(n);}
    bool operator==(const ByteCounter& a)const noexcept{return bytes==a.bytes;}
};
template<class Map> void Inspect(const char* label,Map& values,Bytes& bytes,size_t handle)
{
    values.reserve(4096);for(uint64_t i=0;i<4096;++i)values.try_emplace(i,unsigned(i));
    const auto allocated=bytes.allocations;
    for(uint64_t i=0;i<8192;++i){if(values.contains(i)!=(i<4096))throw std::runtime_error("lookup");}
    if(bytes.allocations!=allocated)throw std::runtime_error("lookup allocation");
    const auto live=bytes.current,peak=bytes.peak,buckets=values.bucket_count();
    const auto released=bytes.releases;values.erase(uint64_t(0));
    if(bytes.releases!=released+1)throw std::runtime_error("node release");
    std::printf("%s,4096,%zu,%zu,%zu,%zu,%zu,%zu\n",label,buckets,live,peak,allocated,released,handle);
}
int main()
{
    using CE=ce::extractable_hash_map<uint64_t,unsigned,ce::engine_integer_hash<uint64_t>,ce::engine_node_equal<uint64_t>,ByteCounter,false>;
    using STL=std::unordered_map<uint64_t,unsigned,std::hash<uint64_t>,std::equal_to<uint64_t>,CountingAllocator<std::pair<const uint64_t,unsigned>>>;
    Bytes a,b;
    std::puts("container,size,buckets,requested_live_bytes,peak_requested_bytes,allocate_calls,free_calls,node_handle_bytes");
    {CE values{ByteCounter{&a}};Inspect("ce",values,a,sizeof(ce::engine_extractable_map<uint64_t,unsigned>::node_type));}
    {STL values{0,std::hash<uint64_t>{},std::equal_to<uint64_t>{},CountingAllocator<std::pair<const uint64_t,unsigned>>{&b}};Inspect("stl",values,b,sizeof(std::unordered_map<uint64_t,unsigned>::node_type));}
    if(a.current||b.current)throw std::runtime_error("leak");
    std::puts("STORAGE_MEMORY_OK lookupAllocations=0 eraseRelease=1 finalLiveBytes=0");
}
