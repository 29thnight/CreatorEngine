#include <algorithm>
#include <array>
#include <chrono>
#include <concepts>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <iterator>
#include <memory>
#include <stdexcept>
#include <string_view>
#include <vector>
#include <ce/dynamic_array.h>
#include <ce/fixed_array.h>
#include <ce/containers.h>

static_assert(std::random_access_iterator<ce::reverse_pointer_iterator<int>>);
static_assert(std::random_access_iterator<ce::reverse_pointer_iterator<const int>>);
static_assert(std::ranges::contiguous_range<ce::dynamic_array<int>>);
static_assert(std::ranges::contiguous_range<ce::fixed_array<int,4>>);
struct no_default { no_default()=delete; };
struct immovable {
    static inline int live=0,budget=-1;
    immovable(){if(budget==0)throw std::runtime_error("immovable construction");if(budget>0)--budget;++live;}
    immovable(const immovable&)=delete;immovable(immovable&&)=delete;
    ~immovable()noexcept{--live;}
};
struct recursive_node {int value=0;ce::recursive_array<recursive_node> children;};
struct recursive_object {ce::recursive_array<std::pair<int,recursive_object>> fields;};
static_assert(std::is_copy_constructible_v<recursive_node>);
static_assert(std::is_copy_constructible_v<recursive_object>);
struct scope_domain{int live=0;};
struct scope_allocator {
    using scoped_construction_allocator=scope_allocator;
    static constexpr bool propagate_on_array_copy_assignment=false;
    static constexpr bool propagate_on_array_move_assignment=false;
    scope_domain* domain=nullptr;
    scope_allocator select_on_container_copy_construction()const noexcept{return {};}
    void* allocate(size_t n,size_t a){auto* p=ce::heap_allocator{}.allocate(n,a);if(domain)++domain->live;return p;}
    void deallocate(void* p,size_t n,size_t a)noexcept{ce::heap_allocator{}.deallocate(p,n,a);if(domain)--domain->live;}
    bool operator==(const scope_allocator& other)const noexcept{return domain==other.domain;}
    template<class T,class... Args>void construct(T* p,Args&&... args){
        std::apply([p](auto&&... values){std::construct_at(p,std::forward<decltype(values)>(values)...);},
            std::uses_allocator_construction_args<T>(*this,std::forward<Args>(args)...));
    }
    template<class T,class... Args>T construct_value(Args&&... args){return std::make_obj_using_allocator<T>(*this,std::forward<Args>(args)...);}
};
using scoped_inner=ce::dynamic_array<int,scope_allocator>;
using scoped_outer=ce::dynamic_array<scoped_inner,scope_allocator>;
static_assert(!std::is_nothrow_move_assignable_v<scoped_outer>);
static_assert(sizeof(ce::fixed_array<no_default,0>)==1);
constexpr ce::fixed_array fixed{1,2,3};
constexpr auto generated_fixed=ce::to_array<int>({3,4,5});
static_assert(generated_fixed[2]==5);
static_assert(fixed[1]==2 && ce::get<2>(fixed)==3);
static_assert((fixed<=>ce::fixed_array{1,2,4})<0);

static void check(bool condition) { if(!condition)throw std::runtime_error("array migration regression"); }
static void correctness() {
    {ce::dynamic_array<immovable> fixed_count(17);check(fixed_count.size()==17 && immovable::live==17);}
    check(immovable::live==0);immovable::budget=3;
    try{ce::dynamic_array<immovable> throws(17);check(false);}catch(const std::runtime_error&){}
    check(immovable::live==0);immovable::budget=-1;
    std::deque<int> segmented{1,2,1,3,1};
    check(ce::erase(segmented,1)==3 && segmented==std::deque<int>({2,3}));
    check(ce::erase_if(segmented,[](int x){return x%2==0;})==1 && segmented.front()==3);
    scoped_outer escaped;
    scope_domain a,b;
    {
        scoped_outer source(scope_allocator{&a});source.emplace_back();source[0].push_back(7);
        check(source[0].get_allocator().domain==&a);
        auto* stable=source[0].data();source.reserve(100);
        check(source[0].data()==stable);
        escaped=source;check(escaped.get_allocator().domain==nullptr && escaped[0].get_allocator().domain==nullptr);
        scoped_outer destination(scope_allocator{&b});destination=source;
        check(destination.get_allocator().domain==&b && destination[0].get_allocator().domain==&b);
        destination=std::move(source);
        check(source.empty() && destination.get_allocator().domain==&b && destination[0].get_allocator().domain==&b && destination[0][0]==7);
    }
    check(a.live==0 && b.live==0 && escaped[0][0]==7);
    recursive_node root;root.children.push_back(recursive_node{7,{}});
    auto copy=root;check(copy.children[0].value==7);
    recursive_object object;object.fields.emplace_back(1,recursive_object{});
    auto objectCopy=object;check(objectCopy.fields.size()==1 && objectCopy.fields[0].first==1);
    ce::fixed_array<int,4> reverse{1,2,3,4};
    std::sort(reverse.rbegin(),reverse.rend());
    check(reverse[0]==4 && reverse[3]==1);
    ce::dynamic_array<int> own;
    std::vector<int> standard;
    uint32_t random=12345;
    for(size_t step=0;step<10000;++step) {
        random=random*1664525u+1013904223u;
        auto value=static_cast<int>(random>>8);
        size_t pos=standard.empty()?0:random%(standard.size()+1);
        switch(random%7) {
        case 0: own.push_back(value);standard.push_back(value);break;
        case 1: own.insert(own.begin()+static_cast<ptrdiff_t>(pos),value);standard.insert(standard.begin()+static_cast<ptrdiff_t>(pos),value);break;
        case 2: if(!standard.empty()){pos%=standard.size();own.erase(own.begin()+static_cast<ptrdiff_t>(pos));standard.erase(standard.begin()+static_cast<ptrdiff_t>(pos));}break;
        case 3: own.resize(random%32,value);standard.resize(random%32,value);break;
        case 4: own.assign(random%16,value);standard.assign(random%16,value);break;
        case 5: own.insert(own.end(),{1,2,3});standard.insert(standard.end(),{1,2,3});break;
        default: own.shrink_to_fit();standard.shrink_to_fit();break;
        }
        check(own.size()==standard.size() && std::equal(own.begin(),own.end(),standard.begin()));
    }
    ce::dynamic_array<std::unique_ptr<int>> moveOnly;
    moveOnly.emplace_back(std::make_unique<int>(1));
    moveOnly.emplace(moveOnly.begin(),std::make_unique<int>(2));
    moveOnly.shrink_to_fit();
    check(*moveOnly[0]==2 && *moveOnly[1]==1);
    std::ranges::sort(own.rbegin(),own.rend());
    check(std::is_sorted(own.rbegin(),own.rend()));
    auto removed=ce::erase_if(own,[](int v){return v==1;});
    check(removed<=standard.size());
    bool bounds=false;try{(void)reverse.at(4);}catch(ce::bounds_error){bounds=true;}check(bounds);
    std::puts("ARRAY_MIGRATION_CORRECTNESS_PASS");
}

static volatile uint64_t checksum=0;
static bool flag(const char* name) {
#if defined(_MSC_VER)
    size_t count=0;return getenv_s(&count,nullptr,0,name)==0 && count!=0;
#else
    return std::getenv(name)!=nullptr;
#endif
}
template<size_t LargeAlignment> struct alignment_control_allocator {
    void* allocate(size_t bytes,size_t alignment) {
        if(bytes>=4096 && alignment<LargeAlignment)alignment=LargeAlignment;
        return ce::detail::heap_allocate(bytes,alignment);
    }
    void deallocate(void* value,size_t bytes,size_t alignment)noexcept {
        if(bytes>=4096 && alignment<LargeAlignment)alignment=LargeAlignment;
        ce::detail::heap_deallocate(value,alignment);
    }
};
using aligned_control_array=ce::dynamic_array<uint64_t,alignment_control_allocator<32>>;
using plain_control_array=ce::dynamic_array<uint64_t,alignment_control_allocator<16>>;
#if defined(_MSC_VER)
__declspec(noinline)
#else
__attribute__((noinline))
#endif
static uint64_t consume(const uint64_t* values,size_t count) {
    uint64_t value=0;for(size_t i=0;i<count;++i)value+=values[i];return value;
}
template<class V> static double measure(std::string_view name,size_t size,size_t rounds) {
    std::vector<uint64_t> input(size);
    for(size_t i=0;i<size;++i)input[i]=i*17+3;
    V reusable;
    reusable.reserve(size*2);
    if(flag("CE_ARRAY_ALIGNMENT_DIAGNOSTICS"))
        std::printf("array_api_alignment,%.*s,%zu,%s,%zu,%zu\n",static_cast<int>(name.size()),name.data(),size,
            std::same_as<V,std::vector<uint64_t>>?"stl":"custom",
            size_t(reinterpret_cast<uintptr_t>(input.data())%64),size_t(reinterpret_cast<uintptr_t>(reusable.data())%64));
    uint64_t sum=0;
    const auto start=std::chrono::steady_clock::now();
    for(size_t round=0;round<rounds;++round) {
        if(name=="append_reserved") {
            reusable.clear();reusable.insert(reusable.end(),input.data(),input.data()+input.size());
            sum+=consume(reusable.data(),reusable.size());
        } else if(name=="range_construct") {
            V value(input.data(),input.data()+input.size());sum+=consume(value.data(),value.size());
        } else if(name=="count_construct") {
            V value(size,uint64_t{7});sum+=consume(value.data(),value.size());
        } else {
            reusable.assign(input.data(),input.data()+input.size());sum+=consume(reusable.data(),reusable.size());
        }
    }
    const auto elapsed=std::chrono::duration<double,std::nano>(std::chrono::steady_clock::now()-start).count();
    checksum=sum;
    return elapsed/static_cast<double>(rounds);
}
template<class V> static size_t calibrate(std::string_view name,size_t size) {
    size_t rounds=64;
    for(;;){auto ns=measure<V>(name,size,rounds);if(ns*static_cast<double>(rounds)>=100000000.0)return rounds;if(rounds>size_t{1}<<28)return rounds;rounds*=2;}
}
static bool benchmark() {
    bool accepted=true;
    for(auto name:{"append_reserved","range_construct","count_construct","assign_reserved"})for(size_t size:{size_t(8),size_t(64),size_t(4096)}) {
        if(flag("CE_ARRAY_API_COUNT_ONLY") && std::string_view(name)!="count_construct")continue;
        if(flag("CE_ARRAY_API_LARGE_ONLY") && size!=4096)continue;
        auto custom=[&](size_t rounds){
            if(flag("CE_ARRAY_ALIGNED_CONTROL"))return measure<aligned_control_array>(name,size,rounds);
            if(flag("CE_ARRAY_PLAIN_CONTROL"))return measure<plain_control_array>(name,size,rounds);
            return measure<ce::dynamic_array<uint64_t>>(name,size,rounds);
        };
        const auto ownRounds=flag("CE_ARRAY_ALIGNED_CONTROL")?calibrate<aligned_control_array>(name,size):
            flag("CE_ARRAY_PLAIN_CONTROL")?calibrate<plain_control_array>(name,size):calibrate<ce::dynamic_array<uint64_t>>(name,size);
        const auto stdRounds=calibrate<std::vector<uint64_t>>(name,size);
        std::vector<double> own,standard;
        for(int sample=0;sample<15;++sample) {
            double a,b;
            if(sample%2){b=measure<std::vector<uint64_t>>(name,size,stdRounds);a=custom(ownRounds);}
            else{a=custom(ownRounds);b=measure<std::vector<uint64_t>>(name,size,stdRounds);}
            own.push_back(a);standard.push_back(b);
            std::printf("array_api_raw,%s,%zu,%d,%.6f,%.6f\n",name,size,sample,a,b);
        }
        std::sort(own.begin(),own.end());std::sort(standard.begin(),standard.end());
        const auto ratio=own[7]/standard[7],ownSpread=own[13]/own[1],stdSpread=standard[13]/standard[1];
        const bool pass=ratio<=1.10 && ownSpread<=1.30 && stdSpread<=1.30;
        accepted&=pass;
        std::printf("array_api_summary,%s,%zu,%.6f,%.6f,%.6f,%s\n",name,size,ratio,ownSpread,stdSpread,pass?"PASS":"FAIL");
        std::fflush(stdout);
    }
    return accepted;
}
int main(int argc,char** argv) {
    if(argc>1 && std::string_view(argv[1])=="--bench")return benchmark()?0:2;
    correctness();
}
