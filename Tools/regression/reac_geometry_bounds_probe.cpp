// Executes Slang-generated production helpers. The double corner/plane oracle
// does not reproduce the implementation's interval arithmetic or its margins.
#include "reac_geometry_bounds.generated.cpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <span>
#include <stdexcept>
#include <vector>

namespace
{
    using input = BoundsOracleInput_0;
    using output = BoundsOracleOutput_0;
    using vector4 = Vector<float, 4>;

    std::uint64_t checks{};
    std::uint64_t rejected{};
    std::uint64_t skin_samples{};

    void require(bool condition, const char* message)
    {
        ++checks;
        if (!condition)
        {
            throw std::runtime_error(message);
        }
    }

    input identity_input()
    {
        input result{};
        result.minimum_0 = vector4(-0.25f, -0.25f, 0.25f, 0.0f);
        result.maximum_0 = vector4(0.25f, 0.25f, 0.75f, 0.0f);
        for (std::size_t axis = 0; axis < 3; ++axis)
        {
            result.transform_0[axis][axis] = 1.0f;
        }
        for (std::size_t axis = 0; axis < 4; ++axis)
        {
            result.projection_0[axis][axis] = 1.0f;
        }
        result.control_0.x = 1;
        return result;
    }

    void execute(std::span<input> inputs, std::span<output> outputs)
    {
        require(inputs.size() == outputs.size() && inputs.size() <= UINT32_MAX, "fixture storage size");
        GlobalParams_0 parameters{{inputs.data(), inputs.size()}, {outputs.data(), outputs.size()}};
        ComputeVaryingInput varying{};
        varying.endGroupID = {static_cast<std::uint32_t>(inputs.size()), 1, 1};
        BoundsOracleCS(&varying, nullptr, &parameters);
    }

    output execute(input value)
    {
        output result{};
        execute(std::span(&value, 1), std::span(&result, 1));
        return result;
    }

    std::array<double, 4> corner(const input& value, unsigned index)
    {
        std::array<double, 4> result{0, 0, 0, 1};
        for (unsigned axis = 0; axis < 3; ++axis)
        {
            result[axis] = (index & (1u << axis)) != 0 ? value.maximum_0[axis] : value.minimum_0[axis];
        }
        return result;
    }

    double dot_double(const std::array<double, 4>& point, const vector4& column)
    {
        double result{};
        for (std::size_t axis = 0; axis < 4; ++axis)
        {
            result += point[axis] * static_cast<double>(column[axis]);
        }
        return result;
    }

    void verify_finite(const input& value, const output& result)
    {
        require(result.classification_0.x == 1, "finite representable transform must be usable");
        std::array<bool, 6> all_outside{true, true, true, true, true, true};
        for (unsigned index = 0; index < 8; ++index)
        {
            const auto point = corner(value, index);
            for (std::size_t axis = 0; axis < 3; ++axis)
            {
                const double expected = dot_double(point, value.transform_0[axis]);
                require(expected >= static_cast<double>(result.minimum_1[axis]) &&
                            expected <= static_cast<double>(result.maximum_1[axis]),
                        "double transformed corner escaped the conservative box");
            }
            std::array<double, 4> clip{};
            for (std::size_t axis = 0; axis < 4; ++axis)
            {
                clip[axis] = dot_double(point, value.projection_0[axis]);
            }
            const std::array planes{clip[0] + clip[3], clip[3] - clip[0], clip[1] + clip[3],
                                    clip[3] - clip[1], clip[2],           clip[3] - clip[2]};
            for (std::size_t plane = 0; plane < planes.size(); ++plane)
            {
                all_outside[plane] = all_outside[plane] && planes[plane] < 0.0;
            }
        }
        if (result.classification_0.y != 0)
        {
            ++rejected;
            require(value.control_0.x != 0 &&
                        std::any_of(all_outside.begin(), all_outside.end(), [](bool outside) { return outside; }),
                    "false instance rejection against independent homogeneous planes");
        }
        if (value.control_0.x == 0)
        {
            require(result.classification_0.y == 0, "unknown projection must pass through");
        }
    }

    struct generator
    {
        std::uint64_t state{0x5e19d48a2b7036cfull};

        std::uint32_t next()
        {
            state ^= state << 13;
            state ^= state >> 7;
            state ^= state << 17;
            return static_cast<std::uint32_t>(state >> 16);
        }

        float unit() { return static_cast<float>(next() & 0x00ffffffu) / 16777216.0f; }

        float signed_value(float scale) { return (unit() * 2.0f - 1.0f) * scale; }
    };

    void check_affine_and_planes()
    {
        std::vector<input> inputs;
        inputs.reserve(24064);
        const auto identity = identity_input();
        inputs.push_back(identity);
        for (unsigned plane = 0; plane < 6; ++plane)
        {
            input outside = identity;
            const unsigned axis = plane / 2;
            const bool positive = (plane & 1u) != 0;
            outside.minimum_0[axis] = positive ? 2.0f : -3.0f;
            outside.maximum_0[axis] = positive ? 3.0f : -2.0f;
            require(execute(outside).classification_0.y == 1, "separated box must be culled on every plane");
            inputs.push_back(outside);
            input touching = identity;
            const float boundary = axis == 2 && !positive ? 0.0f : positive ? 1.0f : -1.0f;
            touching.minimum_0[axis] = boundary;
            touching.maximum_0[axis] = boundary;
            require(execute(touching).classification_0.y == 0, "plane contact must remain visible");
            inputs.push_back(touching);
        }
        input crossing = identity;
        crossing.projection_0[3] = vector4(0.0f, 0.0f, 1.0f, 0.0f);
        crossing.minimum_0.z = -1.0f;
        crossing.maximum_0.z = 1.0f;
        require(execute(crossing).classification_0.y == 0, "eye-plane crossing must remain visible");
        inputs.push_back(crossing);

        generator random;
        for (unsigned index = 0; index < 24000; ++index)
        {
            input value = identity;
            const float box_scale = std::ldexp(1.0f, static_cast<int>(random.next() % 51) - 25);
            const float transform_scale = std::ldexp(1.0f, static_cast<int>(random.next() % 41) - 20);
            for (std::size_t axis = 0; axis < 3; ++axis)
            {
                const float a = random.signed_value(box_scale);
                const float b = random.signed_value(box_scale);
                value.minimum_0[axis] = std::min(a, b);
                value.maximum_0[axis] = std::max(a, b);
                for (std::size_t component = 0; component < 4; ++component)
                {
                    value.transform_0[axis][component] = random.signed_value(transform_scale);
                }
            }
            // Full homogeneous matrices exercise shear, reflection, cancellation,
            // perspective W crossings and camera motion rather than one projection.
            for (std::size_t axis = 0; axis < 4; ++axis)
            {
                for (std::size_t component = 0; component < 4; ++component)
                {
                    value.projection_0[axis][component] = random.signed_value(4.0f);
                }
            }
            value.control_0.x = index % 13 == 0 ? 0 : 1;
            inputs.push_back(value);
        }
        std::vector<output> outputs(inputs.size());
        execute(inputs, outputs);
        for (std::size_t index = 0; index < inputs.size(); ++index)
        {
            verify_finite(inputs[index], outputs[index]);
        }
        require(rejected > 100, "frustum oracle must exercise actual rejection");
    }

    void check_unknown_and_overflow()
    {
        for (const float invalid : {std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity(),
                                    -std::numeric_limits<float>::infinity()})
        {
            for (unsigned axis = 0; axis < 3; ++axis)
            {
                input value = identity_input();
                value.minimum_0[axis] = invalid;
                const auto result = execute(value);
                require(result.classification_0.x == 0 && result.classification_0.y == 0,
                        "nonfinite source bounds must fail open");
            }
            for (unsigned axis = 0; axis < 4; ++axis)
            {
                input value = identity_input();
                value.projection_0[axis].w = invalid;
                require(execute(value).classification_0.y == 0, "nonfinite projection must fail open");
            }
            input value = identity_input();
            value.transform_0[0].w = invalid;
            require(execute(value).classification_0.x == 0, "nonfinite transform must be unusable");
        }
        input inverted = identity_input();
        inverted.minimum_0.x = inverted.maximum_0.x + 1.0f;
        const auto invalid = execute(inverted);
        require(invalid.classification_0.x == 0 && invalid.classification_0.y == 0, "inverted box must fail open");
        input overflow = identity_input();
        overflow.minimum_0 = vector4(1.0e30f);
        overflow.maximum_0 = vector4(2.0e30f);
        overflow.transform_0[0] = vector4(1.0e20f);
        overflow.projection_0[0] = vector4(1.0e20f);
        const auto huge = execute(overflow);
        require(huge.classification_0.x == 0 && huge.classification_0.y == 0, "floating overflow must fail open");
        input tiny = identity_input();
        tiny.minimum_0 = vector4(-std::numeric_limits<float>::denorm_min());
        tiny.maximum_0 = vector4(std::numeric_limits<float>::denorm_min());
        verify_finite(tiny, execute(tiny));
    }

    void check_skinning()
    {
        generator random;
        for (unsigned group = 0; group < 3000; ++group)
        {
            std::array<input, 4> bones;
            std::array<output, 4> bounds{};
            input world = identity_input();
            const float extent = std::ldexp(1.0f, static_cast<int>(group % 25) - 12);
            world.minimum_0 = vector4(-extent, -extent * 0.75f, -extent * 0.5f, 0.0f);
            world.maximum_0 = vector4(extent * 1.5f, extent, extent * 0.5f, 0.0f);
            const auto source_minimum = world.minimum_0;
            const auto source_maximum = world.maximum_0;
            for (auto& bone : bones)
            {
                bone = world;
                const float scale = std::ldexp(1.0f, static_cast<int>(random.next() % 33) - 16);
                for (unsigned axis = 0; axis < 3; ++axis)
                {
                    for (unsigned component = 0; component < 4; ++component)
                    {
                        bone.transform_0[axis][component] = random.signed_value(scale);
                    }
                }
            }
            execute(bones, bounds);
            // Bind pose remains part of the union for vertices with four zero weights.
            for (const auto& bound : bounds)
            {
                require(bound.classification_0.x == 1, "finite bone box must be usable");
                for (unsigned axis = 0; axis < 3; ++axis)
                {
                    world.minimum_0[axis] = std::min(world.minimum_0[axis], bound.minimum_1[axis]);
                    world.maximum_0[axis] = std::max(world.maximum_0[axis], bound.maximum_1[axis]);
                }
            }
            for (unsigned axis = 0; axis < 3; ++axis)
            {
                for (unsigned component = 0; component < 4; ++component)
                {
                    world.transform_0[axis][component] = random.signed_value(32.0f);
                }
            }
            const auto world_bounds = execute(world);
            require(world_bounds.classification_0.x == 1, "finite world box must be usable");
            for (unsigned sample = 0; sample < 24; ++sample)
            {
                std::array<float, 4> point{0, 0, 0, 1};
                for (unsigned axis = 0; axis < 3; ++axis)
                {
                    point[axis] = source_minimum[axis] + random.unit() * (source_maximum[axis] - source_minimum[axis]);
                }
                std::array<float, 4> weights{};
                float sum{};
                for (unsigned influence = 0; influence < 4; ++influence)
                {
                    weights[influence] = sample == 0 ? 0.0f
                                         : sample < 5
                                             ? static_cast<float>(sample - 1 == influence)
                                             : std::ldexp(random.unit(), static_cast<int>(random.next() % 25) - 12);
                    sum += weights[influence];
                }
                if (sum > 0.0f)
                {
                    for (auto& weight : weights)
                    {
                        weight /= sum;
                    }
                }
                std::array<float, 4> posed = point;
                if (sum > 0.0f)
                {
                    // Float matrix blending then vertex multiplication matches the
                    // decoder's order, independently of the bounds implementation.
                    for (unsigned axis = 0; axis < 3; ++axis)
                    {
                        std::array<float, 4> column{};
                        for (unsigned component = 0; component < 4; ++component)
                        {
                            for (unsigned influence = 0; influence < 4; ++influence)
                            {
                                column[component] += weights[influence] * bones[influence].transform_0[axis][component];
                            }
                        }
                        posed[axis] =
                            ((point[0] * column[0] + point[1] * column[1]) + point[2] * column[2]) + column[3];
                    }
                }
                for (unsigned axis = 0; axis < 3; ++axis)
                {
                    require(posed[axis] >= world.minimum_0[axis] && posed[axis] <= world.maximum_0[axis],
                            "normalized weighted matrix result escaped bone union");
                    const auto& column = world.transform_0[axis];
                    const float transformed =
                        ((posed[0] * column[0] + posed[1] * column[1]) + posed[2] * column[2]) + column[3];
                    require(transformed >= world_bounds.minimum_1[axis] && transformed <= world_bounds.maximum_1[axis],
                            "posed world vertex escaped world interval");
                }
                ++skin_samples;
            }
        }
    }
} // namespace

int main()
{
    try
    {
        check_affine_and_planes();
        check_unknown_and_overflow();
        check_skinning();
        std::cout << "REAC_GEOMETRY_BOUNDS_OK checks=" << checks << " rejected=" << rejected
                  << " normalized_skin_samples=" << skin_samples << '\n';
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "REAC_GEOMETRY_BOUNDS_FAIL after=" << checks << " reason=" << error.what() << '\n';
        return 1;
    }
}
