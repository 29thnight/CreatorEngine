#pragma once

#include "principled_layered_reference.h"
#include "MaterialGraphIblBake.h"

namespace MaterialProbe::IblReference
{
using namespace material_graph;
using namespace MaterialProbe::Reference;

inline IblVector Pack4(Vector value, double w = 0)
{
    return {float(value[0]), float(value[1]), float(value[2]), float(w)};
}

inline Vector Rgb4(IblVector value)
{
    return {value[0], value[1], value[2]};
}

inline double Reverse(unsigned value)
{
    double result = 0, weight = .5;
    for (; value; value >>= 1, weight *= .5)
    {
        result += (value & 1) * weight;
    }
    return result;
}

using Environment = std::array<Vector, 6>;
inline Vector Radiance(const Environment& environment, Vector direction)
{
    unsigned axis = 0;
    for (unsigned i = 1; i < 3; ++i)
    {
        if (std::abs(direction[i]) > std::abs(direction[axis]))
        {
            axis = i;
        }
    }
    return environment[axis * 2 + (direction[axis] < 0 ? 1 : 0)];
}

inline Vector Diffuse(const Environment& environment, Vector normal)
{
    const Vector up = std::abs(normal[2]) < .999 ? Vector{0, 0, 1} : Vector{1, 0, 0};
    const Vector tangent = Unit(Cross(up, normal)), bitangent = Cross(normal, tangent);
    Vector result{};
    for (unsigned i = 0; i < 1024; ++i)
    {
        const double y = Reverse(i), angle = 2 * kPi * i / 1024;
        const Vector direction = tangent * (std::sqrt(y) * std::cos(angle)) +
                                 bitangent * (std::sqrt(y) * std::sin(angle)) + normal * std::sqrt(1 - y);
        result = result + Radiance(environment, direction) * (1.0 / 1024);
    }
    return result;
}

struct ReflectionResult
{
    Integral integral;
    Vector filtered{};
};

inline ReflectionResult Reflection(const Material& material, Vector view, bool layered, bool coat,
                                   const Environment& environment)
{
    Frame frame = MakeFrame(material, coat);
    const double nv = Clamp(Dot(frame.normal, view));
    ReflectionResult result;
    if (nv <= 0 || (coat && material.coat == 0))
    {
        return result;
    }
    const double roughness = coat ? material.coatRoughness : material.roughness;
    const auto energy = Energy(material, view);
    if (layered)
    {
        result.integral = Integrate(material, view, coat);
    }
    if (roughness == 0)
    {
        result.integral.single = layered ? CompensatedFresnel(material, energy, nv, coat) : Fresnel(material, nv, coat);
        result.filtered = Radiance(environment, frame.normal * (2 * nv) - view);
    }
    else
    {
        Vector sum{}, denominator{}, coreSum{};
        double coreAlbedo = 0;
        if (!layered)
        {
            frame.tangent = Projected(view, frame.normal);
            frame.bitangent = Cross(frame.normal, frame.tangent);
        }
        for (unsigned i = 0; i < 1024; ++i)
        {
            const double y = Reverse(i), angle = 2 * kPi * i / 1024;
            Vector local;
            if (layered)
            {
                local = Unit({frame.ax * std::sqrt(y) * std::cos(angle), frame.ay * std::sqrt(y) * std::sin(angle),
                              std::sqrt(1 - y)});
            }
            else
            {
                // BuildSampleFrame(+Z) has tangent=-Y, bitangent=+X.
                const double alpha = roughness * roughness;
                const double z = std::sqrt((1 - y) / (1 + (alpha * alpha - 1) * y));
                const double radius = std::sqrt(1 - z * z);
                local = {radius * std::sin(angle), -radius * std::cos(angle), z};
            }
            const Vector half = frame.tangent * local[0] + frame.bitangent * local[1] + frame.normal * local[2];
            const double vh = Clamp(Dot(view, half));
            const Vector light = half * (2 * vh) - view;
            const double nl = Clamp(Dot(frame.normal, light));
            if (nl <= 0)
            {
                continue;
            }
            const double k = roughness * roughness / 2;
            const auto geometry = [k](double cosine) { return cosine / (cosine * (1 - k) + k); };
            const double weight =
                layered ? 4 * nl * Visibility(frame, view, light) * vh / std::max(Dot(frame.normal, half), 1e-6)
                        : geometry(nv) * geometry(nl) * vh / std::max(Dot(frame.normal, half) * nv, 1e-6);
            const Vector response = (layered ? CompensatedFresnel(material, energy, vh, coat) : Fresnel(material, vh, coat)) * weight;
            sum = sum + response * Radiance(environment, light);
            denominator = denominator + response;
            coreSum = coreSum + response * (1.0 / 1024);
            coreAlbedo += weight / 1024;
        }
        for (unsigned c = 0; c < 3; ++c)
        {
            result.filtered[c] = denominator[c] > 0 ? sum[c] / denominator[c] : 0;
        }
        if (!layered)
        {
            result.integral.single = coreSum;
            result.integral.albedo = Clamp(coreAlbedo);
        }
    }
    if (!layered)
    {
        for (unsigned i = 0; i < 64; ++i)
        {
            result.integral.average =
                result.integral.average + Fresnel(material, std::sqrt((i + .5) / 64), false) * (1.0 / 64);
        }
    }
    return result;
}

inline IblBakeSample ExpectedBake(const IblBakePoint& point, const Environment& environment, const SheenTable& table)
{
    auto input = std::bit_cast<ProbeInput>(point);
    input.options = {};
    const bool layered = point.viewTier[3] == 1;
    const Material material = Evaluate(input, layered ? 0x7ff : 0x7f);
    const Vector view = Unit(Rgb4(point.viewTier));
    const auto base = Reflection(material, view, layered, false, environment);
    const auto coat = Reflection(material, view, layered, true, environment);
    IblBakeSample result{};
    result.baseSingleAlbedo = Pack4(base.integral.single, base.integral.albedo);
    result.baseAverage = Pack4(base.integral.average);
    result.coatSingleAlbedo = Pack4(coat.integral.single, coat.integral.albedo);
    result.coatAverage = Pack4(coat.integral.average);
    result.irradiance = Pack4(Diffuse(environment, material.normal));
    result.basePrefiltered = Pack4(base.filtered);
    result.coatPrefiltered = Pack4(coat.filtered);
    if (material.coat > 0)
    {
        result.coatIrradiance = Pack4(Diffuse(environment, material.coatNormal));
    }
    if (layered && material.sheen > 0)
    {
        const Vector normal =
            Unit(material.normal * (1 - Clamp(material.coat)) + material.coatNormal * Clamp(material.coat));
        if (Dot(normal, view) > 0)
        {
            const Vector tangent = Projected(view, normal), bitangent = Cross(normal, tangent);
            const Vector ltc = Sheen(table, Clamp(Dot(normal, view)), material.sheenRoughness, 0x7ff);
            Vector sum{};
            for (unsigned i = 0; i < 1024; ++i)
            {
                const double y = Reverse(i), angle = 2 * kPi * i / 1024;
                const double z = std::sqrt(1 - y);
                const Vector local = Unit({(std::sqrt(y) * std::cos(angle) - ltc[1] * z) / ltc[0],
                                           std::sqrt(y) * std::sin(angle) / ltc[0], z});
                sum = sum + Radiance(environment, tangent * local[0] + bitangent * local[1] + normal * local[2]) *
                                (1.0 / 1024);
            }
            result.sheenIrradiance = Pack4(sum);
        }
    }
    return result;
}

inline Vector Ambient(const IblBakePoint& point, const IblBakeSample& sample, const SheenTable& table)
{
    auto input = std::bit_cast<ProbeInput>(point);
    input.options = {};
    const bool layered = point.viewTier[3] == 1;
    const Material material = Evaluate(input, layered ? 0x7ff : 0x7f);
    const Vector view = Unit(Rgb4(point.viewTier));
    const Integral base{Rgb4(sample.baseSingleAlbedo), Rgb4(sample.baseAverage), sample.baseSingleAlbedo[3]};
    const Integral coat{Rgb4(sample.coatSingleAlbedo), Rgb4(sample.coatAverage), sample.coatSingleAlbedo[3]};
    const auto baseWeights=LayeredBaseWeights(material,view,base);
    const Vector multiple = layered ? baseWeights.multiple : Multiple(base), coatMultiple{};
    const auto ao = [&](Vector normal, double roughness) {
        return Clamp(std::pow(Clamp(Dot(normal, view)) + material.ao, std::exp2(-16 * roughness - 1)) - 1 +
                     material.ao);
    };
    Vector diffuse{};
    if (!layered)
    {
        for (unsigned c = 0; c < 3; ++c)
        {
            diffuse[c] = material.base[c] * (1 - material.metal) * std::max(1 - base.single[c] - multiple[c], 0.0);
        }
        return (diffuse + multiple) * Rgb4(sample.irradiance) * material.ao +
               base.single * Rgb4(sample.basePrefiltered) * ao(material.normal, material.roughness);
    }
    diffuse = baseWeights.diffuse;
    const Vector sheenNormal =
        Unit(material.normal * (1 - Clamp(material.coat)) + material.coatNormal * Clamp(material.coat));
    const Vector ltc = Sheen(table, Clamp(Dot(sheenNormal, view)), material.sheenRoughness, 0x7ff);
    const Vector sheenColor = material.sheenTint * (material.sheen * ltc[2]);
    const double sheenTransmission = std::max(1 - Maximum(sheenColor), 0.0);
    const double coatTransmission = std::max(1 - material.coat * Energy(material, view).coatAlbedo, 0.0);
    Vector transmission;
    const double nv = Clamp(Dot(material.coatNormal, view));
    const double transmitted = std::sqrt(std::max(1 - (1 - nv * nv) / (material.coatIor * material.coatIor), 0.0));
    for (unsigned c = 0; c < 3; ++c)
    {
        const double absorption =
            material.coatTint[c] <= 0
                ? 0
                : std::exp2(std::clamp(std::log2(material.coatTint[c]) / std::max(transmitted, 1e-4), -80.0, 80.0));
        transmission[c] = sheenTransmission * coatTransmission * (1 + Clamp(material.coat) * (absorption - 1));
    }
    return transmission * ((diffuse + multiple) * Rgb4(sample.irradiance) * material.ao +
                           base.single * ReflectionBudget(diffuse, base.single) * Rgb4(sample.basePrefiltered) * ao(material.normal, material.roughness)) +
           (coat.single * Rgb4(sample.coatPrefiltered) * ao(material.coatNormal, material.coatRoughness) +
            coatMultiple * Rgb4(sample.coatIrradiance) * material.ao) *
               (sheenTransmission * material.coat) +
           sheenColor * Rgb4(sample.sheenIrradiance) * material.ao;
}

} // namespace MaterialProbe::IblReference
