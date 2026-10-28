#include "../../Engine/Utility_Framework/Foundation/EngineStorage.h"
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
int main(int argc,char** argv)
{
    if (!SetPriorityClass(GetCurrentProcess(), HIGH_PRIORITY_CLASS) || !SetThreadAffinityMask(GetCurrentThread(), DWORD_PTR{1} << 4)) return 2;
    bool reverse=argc>1 && std::string_view(argv[1])=="--reverse";
    std::puts("case,count,sample,ce_ns,stl_ns");
    if(argc>2) goto hash_cases;
    for(size_t count:{128u,4096u})
    {
        Case<ce::segmented_deque<unsigned>,std::deque<unsigned>>("deque_fill",count,reverse,[&]<class C>{
            C c;for(size_t i=0;i<count;++i)c.emplace_back(static_cast<unsigned>(i));return c.back()+c.size();});
        Case<ce::segmented_deque<unsigned>,std::deque<unsigned>>("deque_fifo",count,reverse,[&]<class C>{
            C c;for(size_t i=0;i<count;++i)c.emplace_back(static_cast<unsigned>(i));size_t sum=0;
            for(size_t i=0;i<count;++i){sum+=c.front();c.pop_front();c.emplace_back(static_cast<unsigned>(i));}return sum;});
        Case<ce::segmented_deque<unsigned>,std::deque<unsigned>>("deque_index",count,reverse,[&]<class C>{
            static C c; if(c.size()!=count){c.clear();for(size_t i=0;i<count;++i)c.emplace_back(static_cast<unsigned>(i));}size_t sum=0;
            for(size_t i=0;i<count;++i)sum+=c[(i*17)%count];return sum;});
    }
    Case<ce::storage_queue<unsigned>,std::queue<unsigned>>("queue",4096,reverse,[]<class C>{C c;for(unsigned i=0;i<4096;++i)c.push(i);size_t sum=0;while(!c.empty()){sum+=c.front();c.pop();}return sum;});
    Case<ce::storage_stack<unsigned>,std::stack<unsigned>>("stack",4096,reverse,[]<class C>{C c;for(unsigned i=0;i<4096;++i)c.push(i);size_t sum=0;while(!c.empty()){sum+=c.top();c.pop();}return sum;});
    Case<ce::storage_priority_queue<unsigned>,std::priority_queue<unsigned>>("heap",4096,reverse,[]<class C>{C c;for(unsigned i=0;i<4096;++i)c.push(i*2654435761u);size_t sum=0;while(!c.empty()){sum+=c.top();c.pop();}return sum;});
hash_cases:
    Case<ce::engine_extractable_map<unsigned,unsigned>,std::unordered_map<unsigned,unsigned>>("hash_fill",4096,reverse,[]<class C>{C c;c.reserve(4096);for(unsigned i=0;i<4096;++i)c.try_emplace(i,i);return c.size();});
    Case<ce::engine_extractable_map<unsigned,unsigned>,std::unordered_map<unsigned,unsigned>>("hash_hit",4096,reverse,[]<class C>{static C c;if(c.empty()){c.reserve(4096);for(unsigned i=0;i<4096;++i)c.try_emplace(i,i);}size_t sum=0;for(unsigned i=0;i<4096;++i)sum+=c.find(i)->second;return sum;});
    Case<ce::engine_extractable_map<unsigned,unsigned>,std::unordered_map<unsigned,unsigned>>("hash_rekey",4096,reverse,[]<class C>{static C c;if(c.empty()){c.reserve(4096);for(unsigned i=0;i<4096;++i)c.try_emplace(i,i);}size_t sum=0;for(unsigned i=0;i<128;++i){auto n=c.extract(i);n.key()=i+4096;c.insert(std::move(n));n=c.extract(i+4096);n.key()=i;c.insert(std::move(n));sum+=c.size();}return sum;});
    Case<ce::engine_extractable_map<uint64_t,unsigned>,std::unordered_map<uint64_t,unsigned>>("hash_random64_hit",4096,reverse,[]<class C>{static C c;if(c.empty()){c.reserve(4096);for(unsigned i=0;i<4096;++i)c.try_emplace(ce::mix_hash(i),i);}size_t sum=0;for(unsigned i=0;i<4096;++i)sum+=c.find(ce::mix_hash(i))->second;return sum;});
    Case<ce::engine_extractable_map<uint64_t,unsigned>,std::unordered_map<uint64_t,unsigned>>("hash_random64_miss",4096,reverse,[]<class C>{static C c;if(c.empty()){c.reserve(4096);for(unsigned i=0;i<4096;++i)c.try_emplace(ce::mix_hash(i),i);}size_t sum=0;for(unsigned i=4096;i<8192;++i)sum+=c.contains(ce::mix_hash(i));return sum;});
    Case<ce::engine_extractable_map<uint64_t,unsigned>,std::unordered_map<uint64_t,unsigned>>("hash_aligned64_hit",4096,reverse,[]<class C>{static C c;if(c.empty()){c.reserve(4096);for(unsigned i=0;i<4096;++i)c.try_emplace(0x7ff000000000ULL+uint64_t(i)*128,i);}size_t sum=0;for(unsigned i=0;i<4096;++i)sum+=c.find(0x7ff000000000ULL+uint64_t(i)*128)->second;return sum;});
}
