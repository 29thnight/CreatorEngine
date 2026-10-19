// Executes both production decoders, including the complete surface output.
// Slang 2026.14 retains the HLSL `precise` token in C++ output. The runner
// enforces its arithmetic intent with -fno-fast-math -ffp-contract=off.
#define precise
#include "reac_geometry_position.generated.cpp"
#undef precise

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <stdexcept>

namespace
{
    using vector4 = Vector<float, 4>;
    constexpr unsigned kVertices = 4;
    constexpr unsigned kPacket = 256;
    constexpr unsigned kInner = 1024;
    constexpr unsigned kStride = 96;
    std::uint64_t checks{};
    std::uint64_t fixtures{};

    void require(bool condition, const char* message)
    {
        ++checks;
        if (!condition)
        {
            throw std::runtime_error(message);
        }
    }

    std::array<vector4, kVertices * 6> execute(std::array<vector4, 32>& pose, std::array<std::uint32_t, 2048>& topology)
    {
        std::array<vector4, kVertices * 6> result{};
        GlobalParams_0 parameters{
            {pose.data(), pose.size()}, {topology.data(), sizeof(topology)}, {result.data(), result.size()}};
        ComputeVaryingInput varying{};
        varying.endGroupID = {kVertices, 1, 1};
        PositionOracleCS(&varying, nullptr, &parameters);
        ++fixtures;
        for (unsigned vertex = 0; vertex < kVertices; ++vertex)
        {
            for (unsigned axis = 0; axis < 3; ++axis)
            {
                require(std::bit_cast<std::uint32_t>(result[vertex * 6][axis]) ==
                            std::bit_cast<std::uint32_t>(result[vertex * 6 + 1][axis]),
                        "position helper differs bitwise from full surface decoder");
            }
        }
        return result;
    }

    void check_compatibility()
    {
        for (const unsigned version : {4u, 5u})
        {
            std::array<vector4, 32> pose{};
            std::array<std::uint32_t, 2048> topology{};
            topology[0] = version;
            for (unsigned vertex = 0; vertex < kVertices; ++vertex)
            {
                pose[vertex * 5 + 1] =
                    vector4(float(vertex) - 1.5f, float(vertex) * 0.25f, float(vertex) * -3.0f, 1.0f);
            }
            const auto result = execute(pose, topology);
            for (unsigned vertex = 0; vertex < kVertices; ++vertex)
            {
                for (unsigned axis = 0; axis < 3; ++axis)
                {
                    require(result[vertex * 6][axis] == pose[vertex * 5 + 1][axis],
                            "compatibility path must read point position row");
                }
            }
        }
    }

    void check_direct(unsigned mask, bool constant_packet, bool quantized_position, bool skin, bool missing,
                      unsigned motion)
    {
        std::array<vector4, 32> pose{};
        std::array<std::uint32_t, 2048> topology{};
        std::array<std::array<float, kStride / 4>, kVertices> values{};
        topology[0] = 5;
        topology[80 / 4] = 128;
        topology[(kPacket + 8) / 4] = mask;
        topology[(kPacket + 16) / 4] = kInner - kPacket;
        topology[(kInner + 8) / 4] = constant_packet ? 0u : 1u;
        topology[(kInner + 20) / 4] = kStride;
        pose[0].x = std::bit_cast<float>(skin ? 4u : 0u);
        for (unsigned bone = 0; bone < 4; ++bone)
        {
            for (unsigned axis = 0; axis < 3; ++axis)
            {
                auto& column = pose[2 + bone * 3 + axis];
                column[axis] = bone == 1 ? -2.0f : float(bone + 1) * 0.5f;
                column[(axis + 1) % 3] = (float(bone) - 1.0f) * 0.125f;
                column.w = (float(bone) - 1.5f) * float(axis + 1) + float(motion) * 0.03125f;
            }
        }
        const unsigned tangent = (32 + ((mask & 8u) != 0 ? 8 : 0)) / 4;
        const unsigned indices = tangent + 4 + ((mask & 32u) != 0 ? 4 : 0);
        for (unsigned vertex = 0; vertex < kVertices; ++vertex)
        {
            topology[(128 + vertex * 8) / 4] = missing && vertex == 3 ? 0u : kPacket;
            topology[(128 + vertex * 8 + 4) / 4] = vertex;
            auto& data = values[vertex];
            data[0] = float(vertex) - 1.25f;
            data[1] = float(vertex) * -0.75f;
            data[2] = float(vertex) * 0.5f + 0.125f;
            data[5] = 1.0f;
            data[6] = 0.125f;
            data[7] = 0.75f;
            data[tangent] = 1.0f;
            data[tangent + 3] = 1.0f;
            data[indices] = std::bit_cast<float>(0x03020100u);
            if (vertex == 0)
            {
                data[indices + 1] = 1.0f;
                data[indices + 2] = 2.0f;
                data[indices + 3] = 3.0f;
                data[indices + 4] = 4.0f;
            }
            else if (vertex == 2)
            {
                data[indices + 4] = 1.0f;
            }
            else if (vertex == 3)
            {
                data[indices + 1] = 0.001f;
                data[indices + 2] = 128.0f;
                data[indices + 3] = 7.5f;
                data[indices + 4] = 0.125f;
            }
            for (unsigned word = 0; word < data.size(); ++word)
            {
                topology[(kInner + 32 + vertex * kStride) / 4 + word] = std::bit_cast<std::uint32_t>(data[word]);
            }
        }
        if (constant_packet)
        {
            // CGQ1 constant channels are an independent second decoder path.
            for (unsigned word = 0; word < kStride / 4; ++word)
            {
                const unsigned descriptor = (kInner + 32 + word * 24) / 4;
                for (unsigned item = 0; item < 6; ++item)
                {
                    topology[descriptor + item] = 0;
                }
                topology[descriptor + 2] = std::bit_cast<std::uint32_t>(values[0][word]);
            }
        }
        if (quantized_position)
        {
            for (unsigned axis = 0; axis < 3; ++axis)
            {
                topology[(kPacket + 32 + axis * 8) / 4] = std::bit_cast<std::uint32_t>(float(axis) - 0.25f);
                topology[(kPacket + 36 + axis * 8) / 4] = std::bit_cast<std::uint32_t>(0.125f);
            }
        }
        const auto result = execute(pose, topology);
        for (unsigned vertex = 0; vertex < kVertices; ++vertex)
        {
            const auto& data = values[constant_packet ? 0 : vertex];
            std::array<double, 4> position{data[0], data[1], data[2], 1.0};
            if (quantized_position)
            {
                for (unsigned axis = 0; axis < 3; ++axis)
                {
                    position[axis] = (double(axis) - 0.25) + position[axis] * 0.125;
                }
            }
            std::array<double, 3> expected{position[0], position[1], position[2]};
            double sum{};
            for (unsigned influence = 0; influence < 4; ++influence)
            {
                sum += data[indices + 1 + influence];
            }
            if (skin && sum > 0.0)
            {
                expected = {};
                // Independent double weighted transformed positions, contrasting
                // with the shader's float matrix-blend-then-position sequence.
                for (unsigned influence = 0; influence < 4; ++influence)
                {
                    for (unsigned axis = 0; axis < 3; ++axis)
                    {
                        double transformed{};
                        for (unsigned component = 0; component < 4; ++component)
                        {
                            transformed += position[component] * double(pose[2 + influence * 3 + axis][component]);
                        }
                        expected[axis] += transformed * (double(data[indices + 1 + influence]) / sum);
                    }
                }
            }
            if (missing && vertex == 3)
            {
                expected = {};
            }
            for (unsigned axis = 0; axis < 3; ++axis)
            {
                require(std::abs(double(result[vertex * 6][axis]) - expected[axis]) <=
                            0.00002 * std::max(1.0, std::abs(expected[axis])),
                        "direct decoder position differs from independent packet/skin oracle");
            }
        }
    }
} // namespace

int main()
{
    try
    {
        check_compatibility();
        for (unsigned motion = 0; motion < 8; ++motion)
        {
            for (const unsigned mask : {0u, 8u, 32u, 40u})
            {
                for (unsigned variant = 0; variant < 16; ++variant)
                {
                    check_direct(mask, (variant & 1u) != 0, (variant & 2u) != 0, (variant & 4u) != 0,
                                 (variant & 8u) != 0, motion);
                }
            }
        }
        std::cout << "REAC_GEOMETRY_POSITION_OK checks=" << checks << " fixtures=" << fixtures
                  << " vertices=" << fixtures * kVertices << '\n';
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "REAC_GEOMETRY_POSITION_FAIL after=" << checks << " reason=" << error.what() << '\n';
        return 1;
    }
}
