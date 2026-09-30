#pragma once

#include "material_probe_gpu.h"
#include "principled_thin_film_sensitivity.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <fstream>
#include <limits>

namespace MaterialProbe::Reference
{
using MaterialProbe::Float4;
using Vector = std::array<double, 3>;
constexpr unsigned kAngles = 8;
constexpr unsigned kFields = 26;
constexpr double kPi = 3.14159265358979323846;
constexpr std::array<double, 4> kCosines{0.1, 0.25, 0.5, 0.9};

struct ProbeInput
{
    Float4 baseAlpha{0.8f, 0.8f, 0.8f, 1.0f};
    Float4 normalRoughness{0.0f, 0.0f, 1.0f, 0.5f};
    Float4 metalIorLevelAo{0.0f, 1.5f, 0.5f, 1.0f};
    Float4 tintAnisotropy{1.0f, 1.0f, 1.0f, 0.0f};
    Float4 emissionColorStrength{1.0f, 1.0f, 1.0f, 0.0f};
    Float4 coatWeightRoughIorFilmThickness{0.0f, 0.03f, 1.5f, 0.0f};
    Float4 coatTintFilmIor{1.0f, 1.0f, 1.0f, 1.33f};
    Float4 coatNormalSheenWeight{0.0f, 0.0f, 1.0f, 0.0f};
    Float4 sheenTintRoughness{1.0f, 1.0f, 1.0f, 0.5f};
    Float4 tangentRotation{1.0f, 0.0f, 0.0f, 0.0f};
    Float4 options{};
};
static_assert(sizeof(ProbeInput) == 176);

struct Fixture
{
    std::string name;
    ProbeInput input;
    bool bounded = true;
};

inline std::vector<Fixture> Fixtures()
{
    std::vector<Fixture> fixtures;
    const auto add = [&](const char* name, ProbeInput input, bool bounded = true) {
        fixtures.push_back({name, input, bounded});
    };
    ProbeInput input;
    input.options.x = 1.0f;
    add("blender_defaults", input);
    input = {};
    add("explicit_defaults", input);
    input.coatWeightRoughIorFilmThickness.x = 1.0f;
    add("coat_one", input);
    input.coatWeightRoughIorFilmThickness.y = 0.4f;
    add("coat_rough", input);
    input.coatWeightRoughIorFilmThickness.z = 1.0f;
    add("coat_ior_one", input);
    input.coatWeightRoughIorFilmThickness.z = 2.0f;
    input.coatTintFilmIor = {0.2f, 0.6f, 1.0f, 1.33f};
    input.emissionColorStrength = {2.0f, 0.5f, 1.0f, 4.0f};
    add("coat_tinted_emission", input);
    input.coatNormalSheenWeight = {0.3f, -0.2f, 0.9f, 0.0f};
    add("coat_normal", input);
    input = {};
    input.coatNormalSheenWeight.w = 1.0f;
    add("sheen_one", input);
    input.sheenTintRoughness = {0.15f, 0.4f, 0.8f, 0.2f};
    add("sheen_color", input);
    input.sheenTintRoughness.w = 0.0f;
    add("sheen_roughness_zero", input);
    input.sheenTintRoughness.w = 1.0f;
    add("sheen_roughness_one", input);
    input = {};
    input.coatWeightRoughIorFilmThickness = {1.0f, 0.35f, 1.5f, 0.0f};
    input.coatNormalSheenWeight.w = 0.8f;
    add("coat_and_sheen", input);
    input.metalIorLevelAo.x = 1.0f;
    add("coated_metal_sheen", input);
    input = {};
    input.tintAnisotropy.w = 0.8f;
    add("anisotropic", input);
    input.tangentRotation.w = 0.25f;
    add("anisotropic_quarter_turn", input);
    input.tangentRotation.w = 1.0f;
    add("anisotropic_whole_turn", input);
    input.tangentRotation = {0.0f, 0.0f, 0.0f, 0.0f};
    add("zero_tangent_fallback", input);
    input.tangentRotation = {0.0f, 0.0f, 1.0f, 0.0f};
    add("parallel_tangent_fallback", input);
    input.tangentRotation.x = std::numeric_limits<float>::quiet_NaN();
    add("nan_tangent_fallback", input);
    input = {};
    input.coatWeightRoughIorFilmThickness.w = 200.0f;
    add("film_200nm", input);
    input.coatWeightRoughIorFilmThickness.w = 400.0f;
    add("film_400nm", input);
    input.coatWeightRoughIorFilmThickness.w = 800.0f;
    add("film_800nm", input);
    input.coatTintFilmIor.w = 1.0f;
    add("film_ior_one", input);
    input.coatTintFilmIor.w = 1.5f;
    add("film_matches_substrate", input);
    input.coatTintFilmIor.w = 0.75f;
    add("film_top_tir", input);
    input = {};
    input.baseAlpha = {0.2f, 0.5f, 0.9f, 1.0f};
    input.metalIorLevelAo.x = 1.0f;
    input.tintAnisotropy = {0.5f, 0.7f, 0.9f, 0.0f};
    input.coatWeightRoughIorFilmThickness.w = 400.0f;
    add("metal_film", input);
    input.metalIorLevelAo.x = 0.4f;
    input.tintAnisotropy.w = 0.7f;
    input.tangentRotation.w = 0.17f;
    input.coatWeightRoughIorFilmThickness = {0.7f, 0.2f, 1.6f, 250.0f};
    input.coatTintFilmIor = {0.7f, 0.8f, 0.95f, 1.33f};
    input.coatNormalSheenWeight.w = 0.5f;
    input.sheenTintRoughness = {0.2f, 0.4f, 0.8f, 0.6f};
    add("all_layers", input);
    input.metalIorLevelAo.w = 0.0f;
    add("all_layers_ao_zero", input);
    input.metalIorLevelAo.w = 1.0f;
    input.baseAlpha.w = 0.0f;
    add("all_layers_alpha_zero", input);
    input = {};
    input.normalRoughness.w = 0.0f;
    input.coatWeightRoughIorFilmThickness = {1.0f, 0.0f, 1.5f, 0.0f};
    add("mirror_layers", input);
    input = {};
    input.baseAlpha = {1.0f, 1.0f, 1.0f, 1.0f};
    input.coatWeightRoughIorFilmThickness = {1.0f, 1.0f, 1.5f, 0.0f};
    input.coatNormalSheenWeight.w = 1.0f;
    input.tintAnisotropy.w = 1.0f;
    add("white_furnace", input);
    input = {};
    input.coatWeightRoughIorFilmThickness = {-2.0f, -1.0f, 0.0f, -100.0f};
    input.coatTintFilmIor = {-1.0f, 1.0f, 0.0f, 0.0f};
    input.coatNormalSheenWeight = {0.0f, 0.0f, 0.0f, -1.0f};
    input.sheenTintRoughness = {-1.0f, 0.0f, 1.0f, -1.0f};
    input.tintAnisotropy.w = -1.0f;
    add("clamps_low", input);
    input.coatWeightRoughIorFilmThickness = {2.0f, 2.0f, 4.0f, 1000.0f};
    input.coatTintFilmIor = {2.0f, 0.5f, 0.0f, 2.0f};
    input.coatNormalSheenWeight.w = 2.0f;
    input.sheenTintRoughness = {0.5f, 2.0f, 0.0f, 2.0f};
    input.tintAnisotropy.w = 2.0f;
    input.tangentRotation.w = -1.25f;
    add("linked_gain_and_clamps", input, false);
    input = {};
    input.coatWeightRoughIorFilmThickness.w = 0.1f;
    add("film_sub_nm_transition", input);
    input.metalIorLevelAo.y = 1.0f;
    add("film_no_substrate_interface", input);
    return fixtures;
}

inline double Clamp(double value)
{
    return std::clamp(value, 0.0, 1.0);
}

inline Vector operator+(Vector a, Vector b)
{
    return {a[0] + b[0], a[1] + b[1], a[2] + b[2]};
}

inline Vector operator-(Vector a, Vector b)
{
    return {a[0] - b[0], a[1] - b[1], a[2] - b[2]};
}

inline Vector operator*(Vector a, Vector b)
{
    return {a[0] * b[0], a[1] * b[1], a[2] * b[2]};
}

inline Vector operator*(Vector a, double scale)
{
    return {a[0] * scale, a[1] * scale, a[2] * scale};
}

inline double Dot(Vector a, Vector b)
{
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

inline Vector Cross(Vector a, Vector b)
{
    return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
}

inline Vector Unit(Vector value, Vector fallback = {0.0, 0.0, 1.0})
{
    const double lengthSquared = Dot(value, value);
    return !std::isfinite(lengthSquared) || lengthSquared <= 1e-20 || lengthSquared > std::numeric_limits<float>::max()
               ? fallback
               : value * (1.0 / std::sqrt(lengthSquared));
}

inline Vector Rgb(Float4 value)
{
    return {value.x, value.y, value.z};
}

inline Vector Nonnegative(Vector value)
{
    for (double& component : value)
    {
        component = std::max(component, 0.0);
    }
    return value;
}

inline Vector Projected(Vector tangent, Vector normal)
{
    const Vector axis = std::abs(normal[2]) < 0.999 ? Vector{0, 0, 1} : Vector{0, 1, 0};
    return Unit(tangent - normal * Dot(tangent, normal), Unit(Cross(axis, normal)));
}

inline double Maximum(Vector value)
{
    return *std::max_element(value.begin(), value.end());
}

struct Material
{
    Vector base, normal, tangent, tint, emission, coatTint, coatNormal, sheenTint;
    double roughness, metal, ior, level, ao, alpha;
    double coat, coatRoughness, coatIor, sheen, sheenRoughness, aniso, rotation, thickness, filmIor;
};

inline Material Evaluate(ProbeInput input, unsigned mask)
{
    if (input.options.x != 0.0f)
    {
        input = {};
    }
    Material result{};
    result.base = Nonnegative(Rgb(input.baseAlpha));
    result.alpha = Clamp(input.baseAlpha.w);
    result.normal = Unit(Rgb(input.normalRoughness));
    result.roughness = Clamp(input.normalRoughness.w);
    result.metal = Clamp(input.metalIorLevelAo.x);
    result.ior = std::max(double(input.metalIorLevelAo.y), 1e-5);
    result.level = std::max(double(input.metalIorLevelAo.z), 0.0);
    result.ao = Clamp(input.metalIorLevelAo.w);
    result.tint = Nonnegative(Rgb(input.tintAnisotropy));
    result.emission = Rgb(input.emissionColorStrength) * input.emissionColorStrength.w;
    result.coat = (mask & 0x80) ? std::max(double(input.coatWeightRoughIorFilmThickness.x), 0.0) : 0.0;
    result.coatRoughness = Clamp(input.coatWeightRoughIorFilmThickness.y);
    result.coatIor = std::max(double(input.coatWeightRoughIorFilmThickness.z), 1.0);
    result.coatTint = Nonnegative(Rgb(input.coatTintFilmIor));
    result.coatNormal = Unit(Rgb(input.coatNormalSheenWeight));
    result.sheen = (mask & 0x100) ? std::max(double(input.coatNormalSheenWeight.w), 0.0) : 0.0;
    result.sheenTint = Nonnegative(Rgb(input.sheenTintRoughness));
    result.sheenRoughness = std::clamp(double(input.sheenTintRoughness.w), 1e-3, 1.0);
    result.aniso = (mask & 0x200) ? Clamp(input.tintAnisotropy.w) : 0.0;
    result.rotation = input.tangentRotation.w - std::floor(input.tangentRotation.w);
    result.thickness = (mask & 0x400) ? std::max(double(input.coatWeightRoughIorFilmThickness.w), 0.0) : 0.0;
    result.filmIor = std::max(double(input.coatTintFilmIor.w), 1e-5);
    const Vector tangent = Unit(Rgb(input.tangentRotation), Vector{1, 0, 0});
    const Vector projected = tangent - result.normal * Dot(tangent, result.normal);
    const Vector fallback = Projected(Vector{1, 0, 0}, result.normal);
    const Vector original = Unit(projected, fallback);
    result.tangent = original * std::cos(2 * kPi * result.rotation) +
                     Cross(result.normal, original) * std::sin(2 * kPi * result.rotation);
    return result;
}

inline double Dielectric(double cosine, double ior)
{
    if (ior == 1)
    {
        return 0;
    }
    const double sineSquared = (1 - cosine * cosine) / (ior * ior);
    if (sineSquared >= 1)
    {
        return 1;
    }
    const double transmitted = std::sqrt(1 - sineSquared);
    const double s = (cosine - ior * transmitted) / (cosine + ior * transmitted);
    const double p = (ior * cosine - transmitted) / (ior * cosine + transmitted);
    return (s * s + p * p) / 2;
}

inline double Film(double cosine, double filmIor, double thickness,
                   std::complex<double> substrate, size_t channel, double f82 = -1)
{
    const double sinSquared = 1 - cosine * cosine;
    if (cosine == 0 || sinSquared >= filmIor * filmIor) return 1;
    const double cosFilm = std::sqrt(1 - sinSquared / (filmIor * filmIor));
    const double r12[2]{(cosine - filmIor * cosFilm) / (cosine + filmIor * cosFilm),
                        (filmIor * cosine - cosFilm) / (filmIor * cosine + cosFilm)};
    const auto q = std::sqrt(substrate * substrate - sinSquared);
    const std::complex<double> r23[2]{(filmIor * cosFilm - q) / (filmIor * cosFilm + q),
        (substrate * substrate * cosFilm - filmIor * q) / (substrate * substrate * cosFilm + filmIor * q)};
    double sum = 0;
    for (size_t polarization = 0; polarization < 2; ++polarization)
    {
        const double top = r12[polarization] * r12[polarization];
        double bottom = std::norm(r23[polarization]);
        if (f82 >= 0)
        {
            const double f0 = std::norm((substrate - filmIor) / (substrate + filmIor));
            const double correction = (f0 + (1 - f0) * std::pow(6.0 / 7.0, 5) - f82)
                                      * 7 / std::pow(6.0 / 7.0, 6);
            bottom = Clamp(f0 + (1 - f0) * std::pow(1 - cosFilm, 5)
                           - correction * cosFilm * std::pow(1 - cosFilm, 6));
        }
        const double magnitude = std::abs(r23[polarization]);
        const auto phase = (magnitude > 0 ? r23[polarization] / magnitude : std::complex<double>{1, 0})
                           * (r12[polarization] >= 0 ? -1.0 : 1.0);
        const double transmission = 1 - top;
        const double multiple = transmission * transmission * bottom / std::max(1 - top * bottom, 1e-20);
        double value = top + multiple;
        for (unsigned order = 1; order <= 3; ++order)
        {
            const double position = Clamp(2 * kPi * (2 * filmIor * thickness * cosFilm * order) / 60000) * 511;
            const size_t index = size_t(position), next = std::min(index + 1, size_t(511));
            const double t = position - index;
            const auto sensitivity = std::complex<double>{
                kFilmSensitivity[index][channel] * (1 - t) + kFilmSensitivity[next][channel] * t,
                kFilmSensitivity[index][channel + 3] * (1 - t) + kFilmSensitivity[next][channel + 3] * t};
            value += 2 * (multiple - transmission) * std::pow(std::sqrt(top * bottom), order)
                     * std::real(std::pow(phase, order) * std::conj(sensitivity));
        }
        sum += value;
    }
    return Clamp(sum / 2);
}
inline Vector Fresnel(const Material& material, double cosine, bool coat = false)
{
    cosine = Clamp(cosine);
    if (coat)
    {
        const double value = Dielectric(cosine, material.coatIor);
        return {value, value, value};
    }
    const double physicalF0 = std::pow((material.ior - 1) / (material.ior + 1), 2);
    const double adjustedF0 = physicalF0 * 2 * material.level;
    const double root = std::sqrt(std::clamp(adjustedF0, 0.0, 0.99));
    const double adjustedIor = (1 + root) / (1 - root);
    const double ior = material.level == 0.5 ? material.ior : (material.ior < 1 ? 1 / adjustedIor : adjustedIor);
    const double normal = std::pow((ior - 1) / (ior + 1), 2);
    const double weight = ior == 1 ? 0 : Clamp((Dielectric(cosine, ior) - normal) / (1 - normal));
    const double transition =
        Clamp(material.thickness) * Clamp(material.thickness) * (3 - 2 * Clamp(material.thickness));
    const double filmIor = 1 + (material.filmIor - 1) * transition;
    Vector result{};
    for (size_t channel = 0; channel < 3; ++channel)
    {
        const double f0 = Clamp(adjustedF0 * material.tint[channel]);
        double dielectric = ior == 1 ? 0 : f0 + (1 - f0) * weight;
        const double metalF0 = Clamp(material.base[channel]);
        const double f82 = metalF0 + (1 - metalF0) * std::pow(6.0 / 7.0, 5);
        const double correction = f82 * (1 - Clamp(material.tint[channel])) / ((1.0 / 7.0) * std::pow(6.0 / 7.0, 6));
        double metal =
            Clamp(metalF0 + (1 - metalF0) * std::pow(1 - cosine, 5) - correction * cosine * std::pow(1 - cosine, 6));
        if (material.thickness > 0 && material.filmIor != 1)
        {
            double film = Film(cosine, filmIor, material.thickness, {ior, 0}, channel);
            if (normal > 1e-5)
            {
                film *= 1 + (f0 / normal - 1) * Clamp((1 - film) / (1 - normal));
            }
            dielectric += transition * (Clamp(film) - dielectric);
            const double boundedF0 = std::clamp(metalF0, 0.0, 0.999);
            const double squareRoot = std::sqrt(boundedF0);
            const double nMin = (1 - boundedF0) / (1 + boundedF0);
            const double nMax = (1 + squareRoot) / std::max(1 - squareRoot, 1e-4);
            const double f82Tinted = Clamp(f82 * Clamp(material.tint[channel]));
            const double n = nMax + (nMin - nMax) * f82Tinted;
            const double kSquared = (boundedF0 * (n + 1) * (n + 1) - (n - 1) * (n - 1)) / (1 - boundedF0);
            film = Film(cosine, filmIor, material.thickness, {n, std::sqrt(std::max(kSquared, 0.0))},
                        channel, f82Tinted);
            metal += transition * (film - metal);
        }
        result[channel] = (1 - material.metal) * dielectric + material.metal * metal;
    }
    return result;
}

struct Frame
{
    Vector normal, tangent, bitangent;
    double ax, ay;
};

inline Frame MakeFrame(const Material& material, bool coat)
{
    Frame frame;
    frame.normal = coat ? material.coatNormal : material.normal;
    frame.tangent = Projected(material.tangent, frame.normal);
    frame.bitangent = Cross(frame.normal, frame.tangent);
    const double roughness = coat ? material.coatRoughness : material.roughness;
    const double alpha = std::max(roughness * roughness, 1e-3);
    const double aspect = coat ? 1 : std::sqrt(1 - 0.9 * material.aniso);
    frame.ax = alpha / aspect;
    frame.ay = alpha * aspect;
    return frame;
}

inline double Distribution(const Frame& frame, Vector halfVector)
{
    const double x = Dot(halfVector, frame.tangent) / frame.ax;
    const double y = Dot(halfVector, frame.bitangent) / frame.ay;
    const double z = Dot(halfVector, frame.normal);
    return 1 / std::max(kPi * frame.ax * frame.ay * std::pow(x * x + y * y + z * z, 2), 1e-20);
}

inline double Visibility(const Frame& frame, Vector view, Vector light)
{
    const double nv = Clamp(Dot(frame.normal, view));
    const double nl = Clamp(Dot(frame.normal, light));
    const auto projectedLength = [&](Vector direction, double cosine) {
        return std::sqrt(std::pow(frame.ax * Dot(frame.tangent, direction), 2) +
                         std::pow(frame.ay * Dot(frame.bitangent, direction), 2) + cosine * cosine);
    };
    return 0.5 / std::max(nl * projectedLength(view, nv) + nv * projectedLength(light, nl), 1e-6);
}

struct Integral
{
    Vector single{}, average{};
    double albedo = 1;
};

inline Integral Integrate(const Material& material, Vector view, bool coat)
{
    const Frame frame = MakeFrame(material, coat);
    const double cosine = Clamp(Dot(frame.normal, view));
    Integral result;
    if (cosine <= 0 || (coat && material.coat == 0))
    {
        return result;
    }
    const double roughness = coat ? material.coatRoughness : material.roughness;
    if (roughness == 0)
    {
        result.single = Fresnel(material, cosine, coat);
    }
    else
    {
        result.albedo = 0;
        for (unsigned index = 0; index < 1024; ++index)
        {
            double reversed = 0, weight = 0.5;
            for (unsigned bits = index; bits; bits >>= 1)
            {
                reversed += (bits & 1) * weight;
                weight *= 0.5;
            }
            const double azimuth = 2 * kPi * index / 1024;
            const Vector local = Unit({frame.ax * std::sqrt(reversed) * std::cos(azimuth),
                                       frame.ay * std::sqrt(reversed) * std::sin(azimuth), std::sqrt(1 - reversed)});
            const Vector half = frame.tangent * local[0] + frame.bitangent * local[1] + frame.normal * local[2];
            const double vh = Clamp(Dot(view, half));
            const Vector light = half * (2 * vh) - view;
            const double nl = Clamp(Dot(frame.normal, light));
            if (nl > 0)
            {
                const double factor =
                    4 * nl * Visibility(frame, view, light) * vh / std::max(Dot(frame.normal, half), 1e-6) / 1024;
                result.single = result.single + Fresnel(material, vh, coat) * factor;
                result.albedo += factor;
            }
        }
        result.albedo = Clamp(result.albedo);
        for (double& component : result.single)
        {
            component = std::min(component, result.albedo);
        }
    }
    for (unsigned index = 0; index < 64; ++index)
    {
        result.average = result.average + Fresnel(material, std::sqrt((index + 0.5) / 64), coat) * (1.0 / 64);
    }
    return result;
}

inline Vector Multiple(const Integral& integral)
{
    const double missing = Clamp(1 - integral.albedo);
    Vector result{};
    for (size_t channel = 0; channel < 3; ++channel)
    {
        result[channel] = missing * integral.single[channel] * integral.average[channel] /
                          std::max(1 - missing * integral.average[channel], 1e-4);
    }
    return result;
}

using SheenTable = std::array<Vector, 1024>;

inline SheenTable LoadSheenTable(const std::filesystem::path& path)
{
    std::ifstream input(path);
    if (!input)
    {
        throw std::runtime_error("Pinned Sheen LTC fixture could not be opened");
    }
    SheenTable table{};
    std::string line;
    std::getline(input, line);
    for (Vector& row : table)
    {
        if (!std::getline(input, line))
        {
            throw std::runtime_error("Sheen fixture must contain 1024 rows");
        }
        std::replace(line.begin(), line.end(), ',', ' ');
        std::istringstream values(line);
        if (!(values >> row[0] >> row[1] >> row[2]))
        {
            throw std::runtime_error("Invalid Sheen coefficient row");
        }
    }
    if (std::getline(input, line) && !line.empty())
    {
        throw std::runtime_error("Unexpected extra Sheen coefficient row");
    }
    return table;
}

inline Vector Sheen(const SheenTable& table, double cosine, double roughness, unsigned mask)
{
    if (!(mask & 0x100))
    {
        return {1, 0, 0};
    }
    const double x = Clamp(cosine) * 31, y = Clamp(roughness) * 31;
    const unsigned x0 = static_cast<unsigned>(x), y0 = static_cast<unsigned>(y);
    const unsigned x1 = std::min(x0 + 1, 31u), y1 = std::min(y0 + 1, 31u);
    Vector result = table[y0 * 32 + x0] * ((1 - (x - x0)) * (1 - (y - y0))) +
                    table[y0 * 32 + x1] * ((x - x0) * (1 - (y - y0))) +
                    table[y1 * 32 + x0] * ((1 - (x - x0)) * (y - y0)) + table[y1 * 32 + x1] * ((x - x0) * (y - y0));
    return std::abs(result[0]) > 1e-5 && result[2] > 1e-5 ? result : Vector{1, 0, 0};
}

struct Expected
{
    std::array<Float4, kFields> fields{};
};

inline Float4 Pack(Vector vector, double w = 0)
{
    return {float(vector[0]), float(vector[1]), float(vector[2]), float(w)};
}

struct BaseWeights { Vector multiple, diffuse; };
inline BaseWeights LayeredBaseWeights(const Material& material, Vector view, const Integral& base)
{
    Vector baseMultiple = Multiple(base);
    Vector diffuse =
        material.base * ((1 - material.metal) * std::max(1 - Maximum(base.single + baseMultiple), 0.0));
    if (material.thickness > 0 && material.filmIor != 1 && (material.metal > 0 || material.filmIor != material.ior))
    {
        const double authoredF0=std::pow((material.ior-1)/(material.ior+1),2)*2*material.level;
        const double f0Root=std::sqrt(std::clamp(authoredF0,0.0,0.99));
        const double adjusted=(1+f0Root)/(1-f0Root);
        const double ior=material.level==0.5?material.ior:material.ior<1?1/adjusted:adjusted;
        const double f0=std::pow((ior-1)/(ior+1),2);
        const double fss=ior>=1?(ior-1)/(4.08567+1.00071*ior)
            :1-ior*ior*(1-(1/ior-1)/(4.08567+1.00071/ior));
        Integral compensation=base;
        for (size_t channel=0;channel<3;++channel)
        {
            const double tintedF0=Clamp(authoredF0*material.tint[channel]);
            const double dielectricFss=tintedF0+(1-tintedF0)*Clamp((fss-f0)/(1-f0));
            const double metalF0=Clamp(material.base[channel]);
            const double schlick82=metalF0+(1-metalF0)*std::pow(6.0/7.0,5);
            const double correction=schlick82*(1-Clamp(material.tint[channel]))*7/std::pow(6.0/7.0,6);
            const double metalFss=metalF0+(1-metalF0)/21-correction/126;
            compensation.average[channel]=Clamp(dielectricFss*(1-material.metal)+metalFss*material.metal);
        }
        baseMultiple=Multiple(compensation);
        Material dielectric = material;
        dielectric.metal = 0;
        diffuse = material.base * ((1 - material.metal) * std::max(1 - Maximum(Fresnel(dielectric, Dot(material.normal, view))), 0.0));
    }
    return {baseMultiple,diffuse};
}

inline Expected Reference(const Material& material, Vector view, unsigned mask, const SheenTable& table)
{
    const Vector light = Unit({0.35, -0.2, 0.8});
    const Vector half = Unit(view + light);
    const Frame frame = MakeFrame(material, false), coatFrame = MakeFrame(material, true);
    const Integral base = Integrate(material, view, false), coat = Integrate(material, view, true);
    const auto terms = LayeredBaseWeights(material,view,base);
    const Vector baseMultiple=terms.multiple, diffuse=terms.diffuse;
    const Vector coatMultiple=Multiple(coat);
    const Vector sheenNormal =
        Unit(material.normal * (1 - Clamp(material.coat)) + material.coatNormal * Clamp(material.coat));
    const Vector sheenCoefficients = Sheen(table, Clamp(Dot(sheenNormal, view)), material.sheenRoughness, mask);
    const Vector sheenColor = material.sheenTint * (material.sheen * sheenCoefficients[2]);
    const double sheenTransmission = std::max(1 - Maximum(sheenColor), 0.0);
    const double coatTransmission = std::max(1 - material.coat * (coat.single[0] + coatMultiple[0]), 0.0);
    Vector transmission{1, 1, 1};
    if (material.coat > 0)
    {
        const double nv = Clamp(Dot(material.coatNormal, view));
        const double path = std::max(std::sqrt(std::max(1 - (1 - nv * nv) / std::pow(material.coatIor, 2), 0.0)), 1e-4);
        for (size_t channel = 0; channel < 3; ++channel)
        {
            const double absorption =
                material.coatTint[channel] == 0
                    ? 0
                    : std::exp2(std::clamp(std::log2(std::max(material.coatTint[channel], 1e-30)) / path, -80.0, 80.0));
            transmission[channel] += Clamp(material.coat) * (absorption - 1);
        }
    }
    transmission = transmission * (sheenTransmission * coatTransmission);
    Vector direct{};
    const double nl = Clamp(Dot(material.normal, light));
    if (nl > 0 && Dot(material.normal, view) > 0)
    {
        direct = transmission * (Fresnel(material, Dot(view, half)) *
                                     (nl * Distribution(frame, half) * Visibility(frame, view, light)) +
                                 (baseMultiple + diffuse) * (nl / kPi));
    }
    const double coatNl = Clamp(Dot(material.coatNormal, light));
    if (material.coat > 0 && coatNl > 0 && Dot(material.coatNormal, view) > 0)
    {
        direct = direct + (Fresnel(material, Dot(view, half), true) *
                               (Distribution(coatFrame, half) * Visibility(coatFrame, view, light)) +
                           coatMultiple * (1 / kPi)) *
                              (sheenTransmission * material.coat * coatNl);
    }
    const double sheenNl = Clamp(Dot(sheenNormal, light));
    if (sheenNl > 0 && Dot(sheenNormal, view) > 0)
    {
        const Vector tangent = Projected(view, sheenNormal);
        const double a = sheenCoefficients[0], b = sheenCoefficients[1];
        const Vector transformed{a * Dot(tangent, light) + b * sheenNl, a * Dot(Cross(sheenNormal, tangent), light),
                                 sheenNl};
        direct = direct +
                 sheenColor * (sheenNl * a * a / (kPi * std::max(std::pow(Dot(transformed, transformed), 2), 1e-20)));
    }
    const auto specularAo = [&](Vector normal, double roughness) {
        return Clamp(std::pow(Clamp(Dot(normal, view)) + material.ao, std::exp2(-16 * roughness - 1)) - 1 +
                     material.ao);
    };
    const Vector ambientDiffuse = transmission * (diffuse + baseMultiple) * material.ao;
    const Vector ambientSpecular = transmission * base.single * specularAo(material.normal, material.roughness);
    const Vector ambientCoat =
        (coat.single * specularAo(material.coatNormal, material.coatRoughness) + coatMultiple * material.ao) *
        (sheenTransmission * material.coat);
    const Vector ambientSheen = sheenColor * material.ao;
    Expected expected;
    auto& fields = expected.fields;
    fields[0] = {float(material.coat), float(material.coatRoughness), float(material.coatIor),
                 float(material.thickness)};
    fields[1] = Pack(material.coatTint, material.filmIor);
    fields[2] = Pack(material.coatNormal, material.sheen);
    fields[3] = Pack(material.sheenTint, material.sheenRoughness);
    fields[4] = Pack(material.tangent, material.aniso);
    fields[5] = {float(frame.ax), float(frame.ay), float(material.rotation), float(material.alpha)};
    fields[6] = Pack(base.single, base.albedo);
    fields[7] = Pack(base.average);
    fields[8] = Pack(coat.single, coat.albedo);
    fields[9] = Pack(coat.average);
    fields[10] = Pack(sheenCoefficients, sheenTransmission);
    fields[11] = Pack(transmission);
    fields[12] = Pack(baseMultiple);
    fields[13] = Pack(coatMultiple);
    fields[14] = Pack(direct);
    fields[15] = Pack(ambientDiffuse);
    fields[16] = Pack(ambientSpecular);
    fields[17] = Pack(ambientCoat);
    fields[18] = Pack(ambientSheen);
    fields[19] = Pack(transmission * material.emission);
    fields[20] = Pack(Fresnel(material, view[2]));
    fields[21] = Pack(ambientDiffuse + ambientSpecular + ambientCoat + ambientSheen);
    fields[22] = {float(Distribution(frame, half)), float(Visibility(frame, view, light)), 0, 0};
    fields[23] = Pack(direct * 2);
    fields[24] = Pack(Fresnel(material, 0));
    fields[25] = Pack(Fresnel(material, 1));
    return expected;
}

} // namespace MaterialProbe::Reference
