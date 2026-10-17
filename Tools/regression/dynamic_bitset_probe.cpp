#define CE_CONTRACT_STANDALONE
#include "../../Engine/Utility_Framework/DynamicBitset.h"

int main()
{
    ce::dynamic_bitset bits;
    std::deque<bool> expected;
    const auto verify = [&]
    {
        if (bits.size() != expected.size() || bits.empty() != expected.empty())
        {
            throw std::runtime_error("bitset size mismatch");
        }
        std::size_t count = 0;
        for (std::size_t i = 0; i < expected.size(); ++i)
        {
            if (bits.test(i) != expected[i])
            {
                throw std::runtime_error("bitset value mismatch");
            }
            count += expected[i];
        }
        if (bits.count() != count)
        {
            throw std::runtime_error("bitset padding/count mismatch");
        }
    };
    for (const auto size : {0u, 1u, 63u, 64u, 65u, 127u, 128u, 129u})
    {
        for (const bool value : {false, true})
        {
            bits.assign(size, value);
            expected.assign(size, value);
            verify();
            bits.resize(size + 65, !value);
            expected.resize(size + 65, !value);
            verify();
            bits.resize(size / 2);
            expected.resize(size / 2);
            verify();
            bits.resize(size + 1, value);
            expected.resize(size + 1, value);
            verify();
            bits.push_back(!value);
            expected.push_back(!value);
            verify();
            bits.set(size, !bits.test(size));
            expected[size] = !expected[size];
            verify();
            auto copy = bits;
            bits.clear();
            bits = std::move(copy);
            verify();
        }
    }
    bool rejected = false;
    try
    {
        bits.set(bits.size());
    }
    catch (const std::out_of_range&)
    {
        rejected = true;
    }
    if (!rejected)
    {
        throw std::runtime_error("bitset accepted invalid index");
    }
    std::puts("DYNAMIC_BITSET_OK boundaries=8 values=2");
}
