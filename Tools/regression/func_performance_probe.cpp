#include "../../Engine/Utility_Framework/Foundation/EngineFunc.h"
#include <Windows.h>

namespace
{
    using Clock = std::chrono::steady_clock;
    volatile size_t sink = 0;

    template<class Function>
    std::array<double, 15> Measure(Function function)
    {
        size_t checksum = 0;
        const auto warmEnd = Clock::now() + std::chrono::seconds(1);
        do { checksum += function(); } while (Clock::now() < warmEnd);
        std::array<double, 15> samples{};
        for (auto& value : samples)
        {
            size_t iterations = 0;
            const auto start = Clock::now();
            auto now = start;
            do { checksum += function(); ++iterations; now = Clock::now(); }
            while (now - start < std::chrono::milliseconds(100));
            value = std::chrono::duration<double, std::nano>(now - start).count() / static_cast<double>(iterations);
        }
        sink = checksum;
        return samples;
    }

    template<class CE, class STL, class Function>
    void Case(const char* name, size_t count, bool reverse, Function operation)
    {
        std::array<double, 15> custom{}, reference{};
        if (reverse) { reference = Measure([&] { return operation.template operator()<STL>(); }); custom = Measure([&] { return operation.template operator()<CE>(); }); }
        else { custom = Measure([&] { return operation.template operator()<CE>(); }); reference = Measure([&] { return operation.template operator()<STL>(); }); }
        for (size_t i = 0; i != custom.size(); ++i)
            std::printf("%s,%zu,%zu,%.9f,%.9f\n", name, count, i, custom[i], reference[i]);
        std::fflush(stdout);
    }

}
namespace
{
    template<size_t Bytes> struct Payload
    {
        unsigned values[Bytes/sizeof(unsigned)]{};
        explicit Payload(unsigned seed){for(size_t i=0;i<std::size(values);++i)values[i]=seed+static_cast<unsigned>(i);}
        unsigned operator()(unsigned n)const{return values[n%(Bytes/sizeof(unsigned))]+n;}
    };
    template<class C> __declspec(noinline) C MakeSmall(unsigned seed)
    { return [seed](unsigned n){return n+seed;}; }
    template<class C> __declspec(noinline) C MakeLarge(unsigned seed)
    { return Payload<256>{seed}; }
    template<class C> __declspec(noinline) C MakeMoveOnly(unsigned seed)
    { return [p=std::make_unique<unsigned>(seed)](unsigned n){return n+*p;}; }
    template<class C> __declspec(noinline) C MakeNoexcept(unsigned seed)
    { return [seed](unsigned n)noexcept{return n+seed;}; }
    unsigned Add(unsigned n){return n+7;}
    unsigned Subtract(unsigned n){return n-7;}
    template<class C> __declspec(noinline) C MakePointer(unsigned seed)
    { return seed%2?&Add:&Subtract; }
    struct alignas(64) Aligned
    {
        unsigned values[16]{};
        explicit Aligned(unsigned seed){values[0]=seed;}
        unsigned operator()(unsigned n)const{return n+values[0];}
    };
    template<class C> __declspec(noinline) C MakeAligned(unsigned seed){return Aligned{seed};}
}
int main(int argc,char** argv)
{
    if (!SetPriorityClass(GetCurrentProcess(), HIGH_PRIORITY_CLASS) || !SetThreadAffinityMask(GetCurrentThread(), DWORD_PTR{1} << 4)) return 2;
    bool reverse=false;std::string_view filter;
    for(int i=1;i<argc;++i){std::string_view arg=argv[i];if(arg=="--reverse")reverse=true;else if(arg.starts_with("--filter="))filter=arg.substr(9);}
    auto enabled=[&](std::string_view name){return filter.empty()||name.find(filter)!=std::string_view::npos;};
    std::puts("case,count,sample,ce_ns,stl_ns");
    if(enabled("owned_sbo_call"))Case<ce::owned_func<unsigned(unsigned)>,std::function<unsigned(unsigned)>>("owned_sbo_call",128,reverse,[]<class C>{static C f=MakeSmall<C>(7);size_t sum=0;for(unsigned i=0;i<128;++i)sum+=f(i);return sum;});
    if(enabled("owned_ptr_call"))Case<ce::owned_func<unsigned(unsigned)>,std::function<unsigned(unsigned)>>("owned_ptr_call",128,reverse,[]<class C>{static C f=MakePointer<C>(7);size_t sum=0;for(unsigned i=0;i<128;++i)sum+=f(i);return sum;});
    if(enabled("owned_heap_call"))Case<ce::owned_func<unsigned(unsigned)>,std::function<unsigned(unsigned)>>("owned_heap_call",128,reverse,[]<class C>{static C f=MakeLarge<C>(7);size_t sum=0;for(unsigned i=0;i<128;++i)sum+=f(i);return sum;});
    if(enabled("owned_sbo_construct"))Case<ce::owned_func<unsigned(unsigned)>,std::function<unsigned(unsigned)>>("owned_sbo_construct",64,reverse,[]<class C>{size_t sum=0;for(unsigned i=0;i<64;++i){C f=MakeSmall<C>(i);sum+=f(i);}return sum;});
    if(enabled("owned_heap_construct"))Case<ce::owned_func<unsigned(unsigned)>,std::function<unsigned(unsigned)>>("owned_heap_construct",64,reverse,[]<class C>{size_t sum=0;for(unsigned i=0;i<64;++i){C f=MakeLarge<C>(i);sum+=f(i);}return sum;});
    if(enabled("owned_sbo_copy"))Case<ce::owned_func<unsigned(unsigned)>,std::function<unsigned(unsigned)>>("owned_sbo_copy",64,reverse,[]<class C>{static C source=MakeSmall<C>(7);size_t sum=0;for(unsigned i=0;i<64;++i){C f=source;sum+=f(i);}return sum;});
    if(enabled("owned_heap_copy"))Case<ce::owned_func<unsigned(unsigned)>,std::function<unsigned(unsigned)>>("owned_heap_copy",64,reverse,[]<class C>{static C source=MakeLarge<C>(7);size_t sum=0;for(unsigned i=0;i<64;++i){C f=source;sum+=f(i);}return sum;});
    if(enabled("owned_sbo_move"))Case<ce::owned_func<unsigned(unsigned)>,std::function<unsigned(unsigned)>>("owned_sbo_move",128,reverse,[]<class C>{static C source=MakeSmall<C>(7);size_t sum=0;for(unsigned i=0;i<128;++i){C f=std::move(source);source=std::move(f);sum+=source(i);}return sum;});
    if(enabled("owned_heap_move"))Case<ce::owned_func<unsigned(unsigned)>,std::function<unsigned(unsigned)>>("owned_heap_move",128,reverse,[]<class C>{static C source=MakeLarge<C>(7);size_t sum=0;for(unsigned i=0;i<128;++i){C f=std::move(source);source=std::move(f);sum+=source(i);}return sum;});
    if(enabled("unique_capture_call"))Case<ce::unique_func<unsigned(unsigned)>,std::move_only_function<unsigned(unsigned)>>("unique_capture_call",128,reverse,[]<class C>{static C f=MakeMoveOnly<C>(7);size_t sum=0;for(unsigned i=0;i<128;++i)sum+=f(i);return sum;});
    if(enabled("inline_owned_copy"))Case<ce::inline_owned_func<unsigned(unsigned),64>,std::function<unsigned(unsigned)>>("inline_owned_copy",64,reverse,[]<class C>{static C source=MakeSmall<C>(7);size_t sum=0;for(unsigned i=0;i<64;++i){C f=source;sum+=f(i);}return sum;});
    if(enabled("inline_unique_move"))Case<ce::inline_unique_func<unsigned(unsigned),64>,std::move_only_function<unsigned(unsigned)>>("inline_unique_move",128,reverse,[]<class C>{static C source=MakeSmall<C>(7);size_t sum=0;for(unsigned i=0;i<128;++i){C f=std::move(source);source=std::move(f);sum+=source(i);}return sum;});
    if(enabled("ref_call"))Case<ce::func_ref<unsigned(unsigned)>,std::function<unsigned(unsigned)>>("ref_call",128,reverse,[]<class C>{static Payload<16> target(7);static C f=target;size_t sum=0;for(unsigned i=0;i<128;++i)sum+=f(i);return sum;});
    if(enabled("noexcept_unique_call"))Case<ce::unique_func<unsigned(unsigned) noexcept>,std::move_only_function<unsigned(unsigned) noexcept>>("noexcept_unique_call",128,reverse,[]<class C>{static C f=MakeNoexcept<C>(7);size_t sum=0;for(unsigned i=0;i<128;++i)sum+=f(i);return sum;});
    if(enabled("aligned_construct"))Case<ce::owned_func<unsigned(unsigned)>,std::function<unsigned(unsigned)>>("aligned_construct",64,reverse,[]<class C>{size_t sum=0;for(unsigned i=0;i<64;++i){C f=MakeAligned<C>(i);sum+=f(i);}return sum;});
}
