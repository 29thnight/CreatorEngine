// Slang이 생성한 실제 cache 변환/조회 코드를 실행한다. GPU 실행/성능 검사가 아니다.
#define precise
#include "primitive-cache.generated.cpp"
#undef precise

#include <array>
#include <bit>
#include <cstdint>
#include <iostream>
#include <stdexcept>

namespace
{
    using vector4 = Vector<float, 4>;
    constexpr std::uint32_t kCapacity = 16384;
    constexpr std::uint32_t kCount = 522;
    std::size_t checks{}, fixtures{};

    void require(bool condition, const char* message)
    {
        ++checks;
        if (!condition)
        {
            throw std::runtime_error(message);
        }
    }

    std::uint32_t hash_address(std::uint32_t value)
    {
        value ^= value >> 16;
        value *= 0x7feb352du;
        value ^= value >> 15;
        value *= 0x846ca68bu;
        return value ^ (value >> 16);
    }

    struct fixture
    {
        std::array<vector4, 32> current{}, previous{};
        std::array<std::uint32_t, 2048> topology{};
        std::array<std::uint32_t, kCapacity> keys{};
        std::array<std::uint32_t, kCount> addresses{}, expected{};
        std::array<Vector<std::uint32_t, 4>, kCount> results{};
        LXSceneGpuRecord_0 record{};
        SLANG_ParameterGroup_LXPrimitiveCacheConstants_natural_0 constants{};

        fixture()
        {
            for (std::uint32_t index = 0; index < 512; ++index)
            {
                addresses[index] = index * 7919u;
            }
            std::uint32_t collisions = 512;
            for (std::uint32_t value = 1; collisions < kCount; ++value)
            {
                if ((hash_address(value) & (kCapacity - 1)) == kCapacity - 3)
                {
                    addresses[collisions++] = value;
                }
            }
            expected.fill(UINT32_MAX);
            // 충돌 fixture를 먼저 삽입해 마지막 세 slot에서 table 앞부분으로 감싸는
            // 8-probe 경계와 9/10번째 primitive의 miss를 결정적으로 만든다.
            for (std::uint32_t item = 0; item < kCount; ++item)
            {
                const auto index = (item + 512) % kCount;
                const auto address = addresses[index];
                if (address == 0)
                {
                    continue;
                }
                for (std::uint32_t probe = 0; probe < 8; ++probe)
                {
                    const auto slot = (hash_address(address) + probe) & (kCapacity - 1);
                    if (keys[slot] == 0 || keys[slot] == address)
                    {
                        keys[slot] = address;
                        expected[index] = slot;
                        break;
                    }
                }
            }
            require(expected[519] != UINT32_MAX && expected[520] == UINT32_MAX && expected[521] == UINT32_MAX,
                    "Fixture saturates all eight probes and retains a bounded miss fallback");
            record.geometry_0 = {0, 1, 0, 3};
            for (std::uint32_t axis = 0; axis < 4; ++axis)
            {
                constants.lx_cacheProjection_0.data_0[axis][axis] = 1.0f;
            }
            constants.lx_cacheProjection_0.data_0[3].z = 0.25f;
        }

        void compatibility()
        {
            topology.fill(0);
            topology[0] = 4;
            for (std::uint32_t vertex = 0; vertex < 3; ++vertex)
            {
                current[vertex * 5] = vector4(float(vertex) * 0.125f, 0.75f, 2, 1);
                current[vertex * 5 + 1] = vector4(float(vertex) - 1.25f, float(vertex) * -0.5f, 0.75f, 0);
                current[vertex * 5 + 2] = vector4(0, 0, 1, 0);
                current[vertex * 5 + 3] = vector4(1, 0, 0, 0);
                current[vertex * 5 + 4] = vector4(0, 1, 0, 0);
            }
            previous = current;
            for (std::uint32_t vertex = 0; vertex < 3; ++vertex)
            {
                previous[vertex * 5 + 1].x += float(vertex + 1) * 0.5f;
            }
        }

        void direct(bool skin, bool missing)
        {
            constexpr std::uint32_t packet = 256, inner = 1024, stride = 96;
            topology.fill(0);
            current.fill(vector4(0));
            previous.fill(vector4(0));
            topology[0] = 5;
            topology[80 / 4] = 128;
            topology[(packet + 16) / 4] = inner - packet;
            topology[(inner + 8) / 4] = 1;
            topology[(inner + 20) / 4] = stride;
            for (std::uint32_t vertex = 0; vertex < 3; ++vertex)
            {
                topology[(128 + vertex * 8) / 4] = missing && vertex == 2 ? 0 : packet;
                topology[(128 + vertex * 8 + 4) / 4] = vertex;
                std::array<float, stride / 4> data{};
                data[0] = float(vertex) - 1.25f;
                data[1] = float(vertex) * -0.5f;
                data[2] = 0.75f;
                data[5] = 1.0f;
                data[6] = float(vertex) * 0.125f;
                data[7] = 0.75f;
                data[8] = data[11] = 1.0f;
                data[12] = std::bit_cast<float>(0x03020100u);
                data[13] = 1.0f;
                data[14] = 2.0f;
                data[15] = 3.0f;
                data[16] = 4.0f;
                for (std::uint32_t word = 0; word < data.size(); ++word)
                {
                    topology[(inner + 32 + vertex * stride) / 4 + word] = std::bit_cast<std::uint32_t>(data[word]);
                }
            }
            current[0].x = previous[0].x = std::bit_cast<float>(skin ? 4u : 0u);
            for (std::uint32_t bone = 0; bone < 4; ++bone)
            {
                for (std::uint32_t axis = 0; axis < 3; ++axis)
                {
                    auto& column = current[2 + bone * 3 + axis];
                    column[axis] = float(bone + 1) * (bone == 1 ? -0.5f : 0.5f);
                    column[(axis + 1) % 3] = (float(bone) - 1.0f) * 0.125f;
                    column.w = float(bone) - 1.5f;
                    previous[2 + bone * 3 + axis] = column;
                    previous[2 + bone * 3 + axis].w += 0.25f;
                }
            }
        }

        void execute(std::size_t keyCapacity = kCapacity)
        {
            GlobalParams_0 parameters{};
            parameters.LXPrimitiveCacheConstants_0 = &constants;
            parameters.lx_cacheRecords_0 = {&record, 1};
            parameters.lx_cacheVertices_0[0] = {current.data(), current.size()};
            parameters.lx_cacheVertices_0[1] = {previous.data(), previous.size()};
            parameters.lx_cacheTopology_0[0] = {topology.data(), sizeof(topology)};
            parameters.oracleKeys_0 = {keys.data(), keyCapacity};
            parameters.oracleAddresses_0 = {addresses.data(), addresses.size()};
            parameters.oracleResults_0 = {results.data(), results.size()};
            ComputeVaryingInput varying{};
            varying.endGroupID = {kCount, 1, 1};
            PrimitiveCacheOracleCS(&varying, nullptr, &parameters);
            ++fixtures;
            for (std::uint32_t index = 0; index < kCount; ++index)
            {
                require(results[index].x == hash_address(addresses[index]), "Shader hash matches the integer oracle");
                require(results[index].y == (keyCapacity == kCapacity ? expected[index] : UINT32_MAX),
                        "Exact-key lookup, collision wrap, probe overflow and disabled-capacity fallback agree");
                require(results[index].z == 0,
                        "Cached world/clip/previous values differ bitwise from uncached decoding");
                require(results[index].w == kCapacity, "CPU and shader bounded cache capacities agree");
            }
        }
    };

    void verify_cache()
    {
        static_assert(sizeof(LXPrimitiveCacheVertex_0) == 112);
        static_assert(sizeof(LXPrimitiveCacheTriangle_0) == 336);
        static_assert(sizeof(LXSceneGpuRecord_0) == 224);
        static_assert(sizeof(SLANG_ParameterGroup_LXPrimitiveCacheConstants_natural_0) == 80);
        fixture value;
        for (std::uint32_t deformation = 0; deformation < 5; ++deformation)
        {
            if (deformation == 0)
            {
                value.compatibility();
            }
            else
            {
                value.direct(deformation >= 3, (deformation & 1) == 0);
            }
            for (const float scale : {1.0f, -2.0f, 0.0f, 0.00001f, 10000.0f})
            {
                value.record.worldColumns_0[0] = vector4(scale, 0.125f * scale, 0, -2.5f);
                value.record.worldColumns_0[1] = vector4(0, scale * 0.5f, 0, 1.25f);
                value.record.worldColumns_0[2] = vector4(0, 0, scale * 2.0f, 0.5f);
                value.execute();
            }
        }
        value.execute(1);
        value.keys.fill(0);
        value.expected.fill(UINT32_MAX);
        value.execute();
    }
} // namespace

int main()
{
    try
    {
        verify_cache();
        std::cout << "PRIMITIVE_CACHE_SHADER_CPU_OK fixtures=" << fixtures << " checks=" << checks
                  << " native_gpu=false\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "PRIMITIVE_CACHE_SHADER_CPU_FAILED " << error.what() << '\n';
        return 1;
    }
}
