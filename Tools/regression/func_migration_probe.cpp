#include "../../Engine/Utility_Framework/Foundation/EngineFunc.h"
#include <cstdio>
#include <array>
#include <thread>
#if defined(_MSC_VER)
#include <crtdbg.h>
#include <cstdlib>
#endif

namespace
{
    unsigned checks=0;
    void Check(bool value,const char* message){++checks;if(!value)throw std::runtime_error(message);}
    struct Resource { size_t allocated=0,freed=0,live=0;bool fail=false; };
    struct Allocator
    {
        Resource* resource=nullptr;
        void* allocate(size_t n,size_t alignment)
        {
            if(resource->fail)throw std::bad_alloc();
            auto memory=ce::heap_allocator{}.allocate(n,alignment);
            ++resource->allocated;++resource->live;return memory;
        }
        void deallocate(void* memory,size_t n,size_t alignment)noexcept
        {if(!memory)std::terminate();++resource->freed;--resource->live;ce::heap_allocator{}.deallocate(memory,n,alignment);}
    };
    struct Owner { static inline int live=0;Owner(){++live;}~Owner(){--live;} };
    struct Large
    {
        std::array<unsigned,128> data{};
        static inline bool throwCopy=false;
        Large(){data[0]=7;}
        Large(const Large& other):data(other.data){if(throwCopy)throw std::runtime_error("copy rejected");}
        Large(Large&&)=default;
        unsigned operator()(unsigned n)const{return n+data[0];}
    };
    struct ThrowingMove
    {
        int value=11;
        ThrowingMove()=default;
        ThrowingMove(const ThrowingMove&)=default;
        ThrowingMove(ThrowingMove&& other)noexcept(false):value(other.value){}
        int operator()()const{return value;}
    };
    struct alignas(256) Aligned
    {
        unsigned value[64]{19};
        unsigned operator()()const{return value[0];}
    };
    struct Object { int n=3;int add(int v)const{return n+v;} };
    int Add(int n){return n+1;}
    int NoexceptAdd(int n)noexcept{return n+2;}
    struct Reference { int value=17;int& operator()()noexcept{return value;} };
    struct HeapOwner
    {
        std::array<unsigned char,512> bytes{};
        std::unique_ptr<Owner> owner=std::make_unique<Owner>();
        HeapOwner()=default;
        HeapOwner(HeapOwner&&) noexcept=default;
        int operator()() const noexcept { return Owner::live; }
    };
    struct ObserveReplacement
    {
        using Function=ce::unique_func<int() noexcept>;
        Function* destination;
        Function* source;
        bool* observed;
        bool active=true;
        ObserveReplacement(Function* d,Function* s,bool* o):destination(d),source(s),observed(o){}
        ObserveReplacement(ObserveReplacement&& other) noexcept
            : destination(other.destination),source(other.source),observed(other.observed),active(std::exchange(other.active,false)) {}
        ~ObserveReplacement()
        { if(active)*observed=!*source && bool(*destination) && (*destination)()==99; }
        int operator()() const noexcept { return 7; }
    };
    struct ReturnsValue { int operator()()const{return 7;} };
    struct VoidTarget { int operator()()const{return 7;} };
}
int RunFuncProbe()
{
    using Lambda=decltype([n=1]{return n;});
    using MoveOnly=decltype([p=std::unique_ptr<int>{}]{return p?*p:0;});
    static_assert(std::is_copy_constructible_v<ce::owned_func<int()>>);
    static_assert(!std::is_copy_constructible_v<ce::unique_func<int()>>);
    static_assert(!std::is_constructible_v<ce::owned_func<int()>,MoveOnly>);
    static_assert(std::is_constructible_v<ce::unique_func<int()>,MoveOnly>);
    static_assert(!std::is_constructible_v<ce::func_ref<int()>,Lambda>);
    static_assert(std::is_constructible_v<ce::func_ref<int()>,Lambda&>);
    static_assert(!std::is_default_constructible_v<ce::func_ref<int()>>);
    static_assert(!std::is_constructible_v<ce::inline_owned_func<unsigned(unsigned),16>,Large>);
    static_assert(!std::is_constructible_v<ce::inline_unique_func<int(),64>,ThrowingMove>);
    static_assert(!std::is_constructible_v<ce::owned_func<const int&()>,ReturnsValue>);
    static_assert(!std::is_constructible_v<ce::unique_func<int() noexcept>,ReturnsValue>);
    static_assert(noexcept(std::declval<ce::unique_func<int() noexcept>&>()()));
    static_assert(std::is_nothrow_move_constructible_v<ce::owned_func<int()>>);
    static_assert(sizeof(ce::func_ref<int()>)==2*sizeof(void*));

    ce::owned_func<int(int)> empty;
    Check(!empty&&empty==nullptr&&nullptr==empty,"empty state");
    bool bad=false;try{empty(1);}catch(const ce::bad_func_call&){bad=true;}Check(bad,"empty call");
    int(*nullFunction)(int)=nullptr;empty=nullFunction;Check(!empty,"null function pointer");
    int(Object::*nullMember)(int)const=nullptr;ce::owned_func<int(const Object&,int)> member=nullMember;Check(!member,"null member pointer");
    member=&Object::add;Object object;Check(member(object,4)==7,"member invoke");
    ce::owned_func<int(const Object&)> data=&Object::n;Check(data(object)==3,"member data invoke");
    ce::owned_func<void()> discard=VoidTarget{};discard();Check(bool(discard),"void discard");
    ce::owned_func<int&()> reference=Reference{};reference()=23;Check(reference()==23,"reference return");
    ce::owned_func<int(int)> function=Add;Check(function(2)==3,"function pointer");
    Check(function.target<int(*)(int)>()&&*function.target<int(*)(int)>()==&Add,"target pointer");
    Check(!function.target<Large>()&&function.target_type()==typeid(int(*)(int)),"target type");
    const auto& constantFunction=function;Check(constantFunction.target<int(*)(int)>()!=nullptr,"const target");
    function={};Check(!function&&function.target_type()==typeid(void),"reset type");

    {
        ce::inline_owned_func<int(),64> emptyInline;
        auto movedEmpty = std::move(emptyInline);
        Check(!emptyInline && !movedEmpty, "inline empty move");
        ce::inline_owned_func<int(),64> liveInline = [] { return 9; };
        liveInline = std::move(movedEmpty);
        Check(!liveInline && !movedEmpty, "inline empty move assignment");
    }
    Resource a,b;
    using Owned=ce::owned_func<int(),Allocator>;
    using Unique=ce::unique_func<int(),Allocator>;
    {
        Owned original(std::allocator_arg,Allocator{&a},[n=0]()mutable{return ++n;});
        auto copy=original;Check(original()==1&&copy()==1,"independent mutable copy");
        Unique copyToUnique=original;Check(copyToUnique()==2&&original()==2,"owned to unique clone");
        Unique transfer=std::move(original);Check(!original&&transfer()==3,"owned to unique transfer");
        Check(a.allocated==0,"SBO conversion must not allocate");
        Owned other(std::allocator_arg,Allocator{&b},[]{return 99;});copy.swap(other);
        Check(copy()==99&&other()==2,"swap captures");
        copy=copy;copy=std::move(copy);Check(copy()==99,"self assignment");
        ce::inline_owned_func<int(),64> inlineCopy=[n=0]()mutable{return ++n;};auto independent=inlineCopy;
        Check(inlineCopy()==1&&independent()==1,"inline copy");
    }
    Check(!a.live&&!b.live,"small lifetimes");
    {
        using BigFunc=ce::owned_func<unsigned(unsigned),Allocator>;
        BigFunc large(std::allocator_arg,Allocator{&a},Large{});
        Check(a.live==1,"large heap");auto* stable=large.target<Large>();auto before=a.allocated;
        BigFunc moved=std::move(large);Check(!large&&moved.target<Large>()==stable&&a.allocated==before,"heap move identity");
        auto copied=moved;Check(copied.target<Large>()!=stable&&copied(3)==10,"heap deep copy");
        BigFunc destination(std::allocator_arg,Allocator{&b},[](unsigned n){return n+100;});
        Large::throwCopy=true;bool rejected=false;auto live=a.live;
        try{destination=moved;}catch(const std::runtime_error&){rejected=true;}
        Large::throwCopy=false;Check(rejected&&destination(3)==103&&a.live==live,"copy rollback");
        a.fail=true;rejected=false;try{auto failed=moved;(void)failed;}catch(const std::bad_alloc&){rejected=true;}
        a.fail=false;Check(rejected&&moved(1)==8&&a.live==live,"allocation rollback");
        destination=std::move(moved);Check(!moved&&destination.target<Large>()==stable,"allocator transfer");
        before=a.freed;destination.reset();Check(a.freed==before+1,"reset release");
    }
    Check(a.live==0&&b.live==0&&a.allocated==a.freed&&b.allocated==b.freed,"allocator domains");
    {
        ce::owned_func<unsigned(),Allocator> aligned(std::allocator_arg,Allocator{&a},Aligned{});
        Check(reinterpret_cast<uintptr_t>(aligned.target<Aligned>())%256==0&&aligned()==19,"heap alignment");
        ce::inline_owned_func<unsigned(),sizeof(Aligned),alignof(Aligned)> inlineAligned=Aligned{};
        Check(reinterpret_cast<uintptr_t>(inlineAligned.target<Aligned>())%256==0&&inlineAligned()==19,"inline alignment");
        ce::owned_func<int(),Allocator> throwingMove(std::allocator_arg,Allocator{&a},ThrowingMove{});
        auto* stable=throwingMove.target<ThrowingMove>();auto moved=std::move(throwingMove);
        Check(moved.target<ThrowingMove>()==stable&&moved()==11,"throwing move heap fallback");
    }
    Check(a.live==0,"aligned release");
    {
        ce::unique_func<int()> one=[p=std::make_unique<Owner>()]{return Owner::live;};
        Check(one()==1,"unique ownership");auto moved=std::move(one);Check(!one&&moved()==1,"unique move");
        moved.reset();Check(Owner::live==0,"unique release");
        ce::inline_unique_func<int(),64> fixed=[p=std::make_unique<Owner>()]{return Owner::live;};
        auto fixedMove=std::move(fixed);Check(!fixed&&fixedMove()==1,"inline unique move");
    }
    Check(Owner::live==0,"inline unique release");
    auto named=[n=0](int amount)mutable{return n+=amount;};
    ce::func_ref<int(int)> borrowed=named;auto borrowedCopy=borrowed;
    Check(borrowed(2)==2&&borrowedCopy(3)==5&&named(1)==6,"borrowed shared target");
    const auto constNamed=[](int n){return n+4;};ce::func_ref<int(int)> constBorrow=constNamed;
    Check(constBorrow(2)==6,"const borrow");ce::func_ref<int(int)> direct=&Add;Check(direct(2)==3,"borrowed function pointer");
    ce::unique_func<int(int) noexcept> safe=&NoexceptAdd;ce::func_ref<int(int) noexcept> safeBorrow=&NoexceptAdd;
    Check(safe(3)==5&&safeBorrow(3)==5,"noexcept invoke");
    bool nullRejected=false;try{ce::func_ref<int(int)> nullRef=nullFunction;(void)nullRef;}catch(const ce::bad_func_call&){nullRejected=true;}
    Check(nullRejected,"null borrow rejected");
    {
        Owned small(std::allocator_arg,Allocator{&a},[]{return 7;});auto before=a.allocated;
        for(unsigned i=0;i<8192;++i){auto clone=small;auto moved=std::move(clone);Check(!clone&&moved()==7,"small copy/move/call");}
        Check(a.allocated==before,"SBO copy/move/call allocations");
    }
    {
        ce::owned_func<int(int)> recursive;recursive=[&](int n){return n?1+recursive(n-1):0;};Check(recursive(128)==128,"recursive lifetime");
        ce::inline_unique_func<int(int),128> fixed;fixed=[&](int n){return n?1+fixed(n-1):0;};Check(fixed(128)==128,"inline recursive lifetime");
    }
    {
        // Identical targets exercise independent copy state and empty transitions.
        std::array<ce::owned_func<unsigned(unsigned)>,8> actual;
        std::array<std::function<unsigned(unsigned)>,8> expected;
        unsigned random=0x12345678;
        for(unsigned step=0;step<10000;++step)
        {
            random=random*1664525u+1013904223u;
            const auto destination=(random>>8)&7u;
            const auto source=(random>>16)&7u;
            switch(random&7u)
            {
            case 0: {
                auto target=[value=step](unsigned n)mutable { return value+=n; };
                actual[destination]=target; expected[destination]=target; break;
            }
            case 1: {
                Large target;
                actual[destination]=target; expected[destination]=target; break;
            }
            case 2: actual[destination]=nullptr;expected[destination]=nullptr;break;
            case 3: actual[destination]=actual[source];expected[destination]=expected[source];break;
            case 4:
                if(destination!=source)
                {
                    actual[destination]=std::move(actual[source]);
                    expected[destination]=std::move(expected[source]);
                    // Standard moved-from state is unspecified; normalize it.
                    actual[source]=nullptr;expected[source]=nullptr;
                }
                break;
            case 5: actual[destination].swap(actual[source]);expected[destination].swap(expected[source]);break;
            case 6: {
                unsigned(*target)(unsigned)=[](unsigned n){return n+7;};
                actual[destination]=target;expected[destination]=target;break;
            }
            case 7: actual[destination]=actual[destination];expected[destination]=expected[destination];break;
            }
            for(size_t i=0;i<actual.size();++i)
            {
                Check(bool(actual[i])==bool(expected[i]),"differential empty state");
                Check(actual[i].target_type()==expected[i].target_type(),"differential target type");
                if(actual[i]) Check(actual[i](step&15u)==expected[i](step&15u),"differential invocation state");
            }
        }
    }
    {
        ce::unique_func<int() noexcept> heap=HeapOwner{};
        Check(Owner::live==1 && heap()==1,"heap resource constructed");
        auto* identity=heap.target<HeapOwner>();
        auto moved=std::move(heap);
        Check(!heap && moved.target<HeapOwner>()==identity && moved()==1,"heap resource move");
        moved.reset();Check(Owner::live==0,"heap resource destructor release");
        bool observed=false;
        ObserveReplacement::Function destination;
        ObserveReplacement::Function source=[]()noexcept{return 99;};
        destination=ObserveReplacement{&destination,&source,&observed};
        destination=std::move(source);
        Check(observed && !source && destination()==99,"replacement destruction observes installed target");
        ce::owned_func<int(int) noexcept> cachedSafe=[](int n)noexcept{return n+3;};
        auto copy=cachedSafe;auto transfer=std::move(copy);
        Check(!copy && cachedSafe(4)==7 && transfer(5)==8,"noexcept cache clone and transfer");
        ce::owned_func<int(int) noexcept> alternate=[](int n)noexcept{return n+9;};
        transfer.swap(alternate);
        Check(transfer(1)==10 && alternate(1)==4,"noexcept cache swap");
        transfer=nullptr;transfer=alternate;
        Check(transfer(2)==5,"noexcept cache reset and assignment");
    }
    std::printf("FUNC_MIGRATION_OK checks=%u owner=%zu unique=%zu ref=%zu\n",checks,sizeof(ce::owned_func<void()>),sizeof(ce::unique_func<void()>),sizeof(ce::func_ref<void()>));
    return 0;
}

int main()
{
#if defined(_MSC_VER)
    _set_error_mode(_OUT_TO_STDERR);
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#if defined(_DEBUG)
    _CrtSetReportMode(_CRT_WARN, _CRTDBG_MODE_FILE);
    _CrtSetReportMode(_CRT_ERROR, _CRTDBG_MODE_FILE);
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_WARN, _CRTDBG_FILE_STDERR);
    _CrtSetReportFile(_CRT_ERROR, _CRTDBG_FILE_STDERR);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
#endif
#endif
    try { return RunFuncProbe(); }
    catch (const std::exception& error)
    { std::fprintf(stderr, "FUNC_MIGRATION_FAILED checks=%u reason=%s\n", checks, error.what()); return 1; }
    catch (...)
    { std::fprintf(stderr, "FUNC_MIGRATION_FAILED checks=%u reason=unknown exception\n", checks); return 1; }
}
