#include "material_probe_compiler.h"

#include "material_probe_gpu.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <iomanip>
#include <limits>
#include <sstream>

namespace
{
using MaterialProbe::ComputeReadback;
using MaterialProbe::Float4;
using Vector = std::array<double, 3>;
constexpr unsigned kAngleCount = 9;
constexpr unsigned kOutputCount = 12;
constexpr double kPi = 3.14159265358979323846;
constexpr std::array<double, kAngleCount> kCosines{0.0, 1.0 / 7.0, 0.25, 0.5, 0.65, 0.75, 0.9, 0.999, 1.0};

struct ProbeInput
{
    Float4 baseAlpha{0.8f, 0.8f, 0.8f, 1.0f};
    Float4 normalRoughness{0.0f, 0.0f, 1.0f, 0.5f};
    Float4 metalIorLevelAo{0.0f, 1.5f, 0.5f, 1.0f};
    Float4 tint{1.0f, 1.0f, 1.0f, 1.0f};
    Float4 emissionColorStrength{1.0f, 1.0f, 1.0f, 0.0f};
    Float4 options{};
};
static_assert(sizeof(ProbeInput) == 96);
static_assert(sizeof(Float4) == 16);

struct Fixture
{
    std::string name;
    ProbeInput input;
};

std::vector<Fixture> Fixtures()
{
    std::vector<Fixture> fixtures;
    const auto add = [&fixtures](const char* name, ProbeInput input) { fixtures.push_back({name, input}); };
    ProbeInput input;
    input.options.x = 1.0f;
    add("blender_defaults", input);
    input = {};
    add("explicit_defaults", input);
    input.metalIorLevelAo.y = 1.0f;
    add("ior_equal_one", input);
    input.metalIorLevelAo.y = 2.0f;
    add("ior_two", input);
    input.metalIorLevelAo.y = 0.75f;
    add("ior_below_one_tir", input);
    input = {};
    input.metalIorLevelAo.z = 0.0f;
    add("specular_zero", input);
    input.metalIorLevelAo.z = 1.0f;
    add("specular_double", input);
    input.metalIorLevelAo.y = 0.75f;
    add("specular_double_below_one", input);
    input = {};
    input.tint = {0.25f, 0.5f, 1.0f, 1.0f};
    add("dielectric_tint", input);
    input.metalIorLevelAo.x = 1.0f;
    input.baseAlpha = {0.2f, 0.5f, 0.9f, 1.0f};
    add("metal_f82_tint", input);
    input.tint = {1.0f, 1.0f, 1.0f, 1.0f};
    add("metal_white_tint", input);
    input.metalIorLevelAo.z = 0.0f;
    add("metal_ignores_dielectric_level", input);
    input = {};
    input.baseAlpha = {-1.0f, 2.0f, 0.5f, -0.25f};
    input.normalRoughness = {0.0f, 0.0f, 0.0f, -2.0f};
    input.metalIorLevelAo = {2.0f, 0.0f, -2.0f, 2.0f};
    input.tint = {-1.0f, 2.0f, 0.5f, 1.0f};
    add("input_clamps_low", input);
    input.baseAlpha.w = 2.0f;
    input.normalRoughness.w = 2.0f;
    input.metalIorLevelAo = {-1.0f, 3.0f, 100.0f, -1.0f};
    add("input_clamps_high", input);
    input = {};
    input.emissionColorStrength = {2.0f, 0.5f, 0.125f, 8.0f};
    add("hdr_emission", input);
    input.baseAlpha.w = 0.0f;
    add("alpha_zero_preserves_closure", input);
    input.baseAlpha.w = 0.35f;
    add("alpha_partial", input);
    input = {};
    input.normalRoughness = {0.0f, 3.0f, 4.0f, 0.5f};
    add("normal_normalized", input);
    input.normalRoughness.x = std::numeric_limits<float>::quiet_NaN();
    add("normal_nan_fallback", input);
    input.normalRoughness = {1e30f, 1e30f, 1e30f, 0.5f};
    add("normal_overflow_fallback", input);
    input = {};
    input.metalIorLevelAo.x = 0.4f;
    input.tint = {0.1f, 0.6f, 1.0f, 1.0f};
    add("metal_dielectric_mix", input);
    input = {};
    input.options = {1.0f, 1.0f, 0.0f, 0.0f};
    add("default_uses_geometry_normal", input);
    input.options.x = 0.0f;
    input.normalRoughness = {0.0f, 0.0f, 0.0f, 0.5f};
    add("invalid_normal_uses_geometry", input);
    return fixtures;
}

double Clamp(double value)
{
    return std::clamp(value, 0.0, 1.0);
}

struct Reference
{
    Vector base, normal, emission, f0, metalF0, correction;
    double alpha, roughness, metal, ior, level, ao;
};

Reference Evaluate(const ProbeInput& authored)
{
    ProbeInput input = authored.options.x != 0 ? ProbeInput{} : authored;
    const Vector geometricNormal = authored.options.y != 0 ? Vector{0, 0.6, 0.8} : Vector{0, 0, 1};
    if (authored.options.x != 0 && authored.options.y != 0)
    {
        input.normalRoughness = {0, 0.6f, 0.8f, 0.5f};
    }
    Reference result{};
    result.base = {std::max(double(input.baseAlpha.x), 0.0), std::max(double(input.baseAlpha.y), 0.0),
                   std::max(double(input.baseAlpha.z), 0.0)};
    result.alpha = Clamp(input.baseAlpha.w);
    result.roughness = Clamp(input.normalRoughness.w);
    result.metal = Clamp(input.metalIorLevelAo.x);
    result.ior = std::max(double(input.metalIorLevelAo.y), 1e-5);
    result.level = std::max(double(input.metalIorLevelAo.z), 0.0);
    result.ao = Clamp(input.metalIorLevelAo.w);
    result.normal = {input.normalRoughness.x, input.normalRoughness.y, input.normalRoughness.z};
    double lengthSquared = 0;
    for (double value : result.normal)
    {
        lengthSquared += value * value;
    }
    if (!std::isfinite(lengthSquared) || lengthSquared > std::numeric_limits<float>::max() || lengthSquared <= 1e-20)
    {
        result.normal = geometricNormal;
    }
    else
    {
        for (double& value : result.normal)
        {
            value /= std::sqrt(lengthSquared);
        }
    }
    const Vector tint{input.tint.x, input.tint.y, input.tint.z};
    result.emission = {double(input.emissionColorStrength.x) * input.emissionColorStrength.w,
                       double(input.emissionColorStrength.y) * input.emissionColorStrength.w,
                       double(input.emissionColorStrength.z) * input.emissionColorStrength.w};
    const double physicalF0 = std::pow((result.ior - 1) / (result.ior + 1), 2);
    const double adjustedF0 = physicalF0 * 2 * result.level;
    if (result.level != 0.5)
    {
        const double root = std::sqrt(std::clamp(adjustedF0, 0.0, 0.99));
        const double adjusted = (1 + root) / (1 - root);
        result.ior = result.ior < 1 ? 1 / adjusted : adjusted;
    }
    for (size_t channel = 0; channel < 3; ++channel)
    {
        result.f0[channel] = Clamp(adjustedF0 * std::max(tint[channel], 0.0));
        result.metalF0[channel] = Clamp(result.base[channel]);
        const double schlick82 = std::pow(6.0 / 7.0, 5);
        const double reflectance82 = result.metalF0[channel] + (1 - result.metalF0[channel]) * schlick82;
        result.correction[channel] =
            reflectance82 * (1 - Clamp(tint[channel])) / ((1.0 / 7.0) * std::pow(6.0 / 7.0, 6));
    }
    return result;
}

Vector Fresnel(const Reference& material, double cosine)
{
    double dielectricWeight = 0;
    if (material.ior != 1)
    {
        const double transmittedSinSquared = (1 - cosine * cosine) / (material.ior * material.ior);
        double reflectance = 1;
        if (transmittedSinSquared < 1)
        {
            const double transmittedCos = std::sqrt(1 - transmittedSinSquared);
            const double parallel = (material.ior * cosine - transmittedCos) / (material.ior * cosine + transmittedCos);
            const double perpendicular =
                (cosine - material.ior * transmittedCos) / (cosine + material.ior * transmittedCos);
            reflectance = (parallel * parallel + perpendicular * perpendicular) / 2;
        }
        const double normal = std::pow((material.ior - 1) / (material.ior + 1), 2);
        dielectricWeight = Clamp((reflectance - normal) / (1 - normal));
    }
    Vector result{};
    for (size_t channel = 0; channel < 3; ++channel)
    {
        const double metal =
            Clamp(material.metalF0[channel] + (1 - material.metalF0[channel]) * std::pow(1 - cosine, 5) -
                  material.correction[channel] * cosine * std::pow(1 - cosine, 6));
        const double dielectric =
            material.ior == 1 ? 0 : material.f0[channel] + (1 - material.f0[channel]) * dielectricWeight;
        result[channel] = (1 - material.metal) * dielectric + material.metal * metal;
    }
    return result;
}

struct Integral
{
    Vector single{}, average{};
    double albedo{};
};

Integral Integrate(const Reference& material, double ndotv)
{
    // Independent double-precision quadrature of the documented GGX split sum.
    const double cosine = std::clamp(ndotv, 1e-4, 1.0);
    const double viewX = std::sqrt(1 - cosine * cosine);
    const double alphaSquared = std::pow(material.roughness, 4);
    const double k = material.roughness * material.roughness / 2;
    const auto geometry = [k](double c) { return c / (c * (1 - k) + k); };
    Integral result;
    constexpr unsigned samples = 1024;
    for (unsigned index = 0; index < samples; ++index)
    {
        double reversed = 0;
        double bitWeight = 0.5;
        for (unsigned bits = index; bits != 0; bits >>= 1)
        {
            reversed += (bits & 1) * bitWeight;
            bitWeight *= 0.5;
        }
        const double hZ = std::sqrt((1 - reversed) / (1 + (alphaSquared - 1) * reversed));
        const double hX = std::sin(2 * kPi * index / samples) * std::sqrt(1 - hZ * hZ);
        const double vdoth = Clamp(viewX * hX + cosine * hZ);
        const double ndotl = Clamp(2 * vdoth * hZ - cosine);
        if (ndotl > 0)
        {
            const double weight = geometry(cosine) * geometry(ndotl) * vdoth / std::max(hZ * cosine, 1e-6);
            const Vector fresnel = Fresnel(material, vdoth);
            for (size_t channel = 0; channel < 3; ++channel)
            {
                result.single[channel] += fresnel[channel] * weight / samples;
            }
            result.albedo += weight / samples;
        }
    }
    result.albedo = Clamp(result.albedo);
    for (unsigned index = 0; index < 64; ++index)
    {
        const Vector fresnel = Fresnel(material, std::sqrt((index + 0.5) / 64));
        for (size_t channel = 0; channel < 3; ++channel)
        {
            result.average[channel] += fresnel[channel] / 64;
        }
    }
    return result;
}

Vector Direct(const Reference& material)
{
    const double ndotl = Clamp(material.normal[2]);
    const double ndotv = Clamp(0.6 * material.normal[0] + 0.8 * material.normal[2]) + 1e-5;
    const double halfLength = std::sqrt(0.6 * 0.6 + 1.8 * 1.8);
    const double ndoth = Clamp((0.6 * material.normal[0] + 1.8 * material.normal[2]) / halfLength);
    const double vdoth = 1.8 / halfLength;
    const Vector fresnel = Fresnel(material, vdoth);
    const double alpha = std::max(material.roughness * material.roughness, 1e-3);
    const double a2 = alpha * alpha;
    const double denominator = 1 - ndoth * ndoth + ndoth * ndoth * a2;
    const double distribution = a2 / std::max(kPi * denominator * denominator, 1e-20);
    const double visibility = 0.5 / std::max(ndotl * std::sqrt(ndotv * ndotv * (1 - a2) + a2) +
                                                 ndotv * std::sqrt(ndotl * ndotl * (1 - a2) + a2),
                                             1e-6);
    Vector result{};
    for (size_t channel = 0; channel < 3; ++channel)
    {
        result[channel] = ndotl * (fresnel[channel] * distribution * visibility +
                                   (1 - fresnel[channel]) * material.base[channel] * (1 - material.metal) / kPi);
    }
    return result;
}

class Verification final
{
  public:
    void Scalar(double actual, double expected, const std::string& label, double tolerance = 5e-5)
    {
        ++m_checks;
        const double error = std::abs(actual - expected) / std::max(1.0, std::abs(expected));
        m_maxError = std::max(m_maxError, error);
        if (!std::isfinite(actual) || !std::isfinite(expected) || error > tolerance)
        {
            throw std::runtime_error(label + ": actual=" + std::to_string(actual) +
                                     " expected=" + std::to_string(expected) + " error=" + std::to_string(error));
        }
    }

    void Rgb(Float4 actual, Vector expected, const std::string& label, double tolerance = 5e-5)
    {
        Scalar(actual.x, expected[0], label + ".r", tolerance);
        Scalar(actual.y, expected[1], label + ".g", tolerance);
        Scalar(actual.z, expected[2], label + ".b", tolerance);
    }

    void Run(const std::vector<Fixture>& fixtures, const std::vector<Float4>& results)
    {
        for (size_t index = 0; index < fixtures.size(); ++index)
        {
            const Reference material = Evaluate(fixtures[index].input);
            for (unsigned angle = 0; angle < kAngleCount; ++angle)
            {
                const Float4* output = results.data() + (index * kAngleCount + angle) * kOutputCount;
                const std::string label = fixtures[index].name + "/" + std::to_string(angle);
                Rgb(output[0], material.base, label + "/base");
                Scalar(output[0].w, material.alpha, label + "/alpha");
                Rgb(output[1], material.normal, label + "/normal");
                Scalar(output[1].w, material.roughness, label + "/roughness");
                Scalar(output[2].x, material.metal, label + "/metallic");
                Scalar(output[2].y, material.ior, label + "/ior");
                Scalar(output[2].z, material.level, label + "/level");
                Scalar(output[2].w, material.ao, label + "/ao");
                Rgb(output[3], material.emission, label + "/emission");
                Rgb(output[4], material.f0, label + "/dielectricF0");
                Rgb(output[5], material.metalF0, label + "/metalF0");
                Rgb(output[6], Fresnel(material, kCosines[angle]), label + "/fresnel");
                Rgb(output[7], Direct(material), label + "/direct");
                const Integral integral = Integrate(material, kCosines[angle]);
                Vector diffuse{}, specular{};
                const double missing = 1 - integral.albedo;
                const double specularAo = Clamp(
                    std::pow(kCosines[angle] + material.ao, std::exp2(-16 * material.roughness - 1)) - 1 + material.ao);
                for (size_t channel = 0; channel < 3; ++channel)
                {
                    const double multiple = missing * integral.single[channel] * integral.average[channel] /
                                            std::max(1 - integral.average[channel] * missing, 1e-4);
                    const double weight = std::max(1 - integral.single[channel] - multiple, 0.0);
                    diffuse[channel] =
                        (material.base[channel] * (1 - material.metal) * weight + multiple) * material.ao;
                    specular[channel] = integral.single[channel] * specularAo;
                }
                // TIR discontinuities and float GGX sampling make the quadrature
                // tolerance larger than the pointwise Fresnel/ABI tolerance.
                Rgb(output[8], diffuse, label + "/iblDiffuse", 3e-3);
                Rgb(output[9], specular, label + "/iblSpecular", 3e-3);
                Rgb(output[10], integral.single, label + "/integral", 3e-3);
                Scalar(output[10].w, integral.albedo, label + "/albedo", 3e-3);
                Rgb(output[11], integral.average, label + "/average", 3e-3);
            }
            std::cout << "CASE_OK " << fixtures[index].name << '\n';
        }
        std::cout << std::setprecision(9) << "PRINCIPLED_CORE_GPU_OK cases=" << fixtures.size()
                  << " angles=" << kAngleCount << " checks=" << m_checks << " maxNormalizedError=" << m_maxError
                  << '\n';
    }

  private:
    unsigned m_checks{};
    double m_maxError{};
};
} // namespace

int wmain(int argc, wchar_t** argv)
{
    try
    {
        if (argc != 2)
        {
            throw std::runtime_error("Expected repository root");
        }
        const auto root = std::filesystem::absolute(argv[1]);
        const auto source = root / "Tools" / "regression" / "principled_core_probe.slang";
        MaterialProbe::CompilerRuntime compiler;
        compiler.Initialize(root);
        std::vector<uint8_t> dxil;
        std::string error;
        if (!compiler.Compile(source, "CSMain", SLANG_STAGE_COMPUTE, false, {}, error, &dxil) ||
            !compiler.Compile(source, "CSMain", SLANG_STAGE_COMPUTE, true, {}, error))
        {
            throw std::runtime_error("Principled core DXIL/SPIR-V compile: " + error);
        }
        std::cout << "PRINCIPLED_CORE_COMPILE_OK compiled=2\n";
        const auto fixtures = Fixtures();
        ComputeReadback gpu;
        std::vector<ProbeInput> inputs;
        for (const Fixture& fixture : fixtures)
        {
            inputs.push_back(fixture.input);
        }
        const auto results = gpu.Run(dxil, inputs, kAngleCount, kOutputCount);
        const auto artifact = root / "Build" / "Obj" / "PrincipledCoreProbe" / "gpu-readback.csv";
        std::filesystem::create_directories(artifact.parent_path());
        std::ofstream readback(artifact);
        if (!readback)
        {
            throw std::runtime_error("GPU readback artifact could not be opened");
        }
        readback << "case,cosine,field,x,y,z,w\n" << std::setprecision(9);
        const char* fields[kOutputCount]{
            "baseAlpha", "normalRoughness", "metalIorLevelAo", "emission",    "dielectricF0",        "metalF0",
            "fresnel",   "direct",          "iblDiffuse",      "iblSpecular", "singleScatterAlbedo", "averageFresnel"};
        for (size_t index = 0; index < fixtures.size(); ++index)
        {
            for (unsigned angle = 0; angle < kAngleCount; ++angle)
            {
                for (unsigned field = 0; field < kOutputCount; ++field)
                {
                    const Float4 value = results[(index * kAngleCount + angle) * kOutputCount + field];
                    readback << fixtures[index].name << ',' << kCosines[angle] << ',' << fields[field] << ',' << value.x
                             << ',' << value.y << ',' << value.z << ',' << value.w << '\n';
                }
            }
        }
        readback.close();
        Verification verification;
        verification.Run(fixtures, results);
        return 0;
    }
    catch (const std::exception& exception)
    {
        std::cerr << exception.what() << '\n';
        return 1;
    }
}
