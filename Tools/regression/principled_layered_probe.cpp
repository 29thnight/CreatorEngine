#include "material_probe_compiler.h"
#include "principled_layered_reference.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <iomanip>
#include <limits>

namespace
{
using namespace MaterialProbe::Reference;

class Verification final
{
  public:
    void Scalar(double actual, double expected, const std::string& label, double tolerance)
    {
        ++checks;
        const double error = std::abs(actual - expected) / std::max(1.0, std::abs(expected));
        if (error > maximumError)
        {
            maximumError = error;
            maximumLabel = label;
        }
        if (!std::isfinite(actual) || !std::isfinite(expected) || error > tolerance)
        {
            throw std::runtime_error(label + ": actual=" + std::to_string(actual) +
                                     " expected=" + std::to_string(expected) + " error=" + std::to_string(error));
        }
    }

    unsigned checks{};
    unsigned furnaceChecks{};
    double maximumError{};
    std::string maximumLabel;
};

void VerifyGgxClosureContract()
{
    Verification verification;
    for (double roughness : {0.2, 0.5, 0.88})
        for (double cosine : {0.1, 0.5, 0.9})
        {
            ProbeInput input;
            input.normalRoughness.w = float(roughness);
            input.baseAlpha = {1, 1, 1, 1};
            Material white = Evaluate(input, 0x7ff);
            const Vector view{std::sqrt(1 - cosine * cosine), 0, cosine};
            const auto energy = Energy(white, view);
            const double e = SampleTable(kGgxEnergy, 32, 32, roughness, cosine);
            for (unsigned c = 0; c < 3; ++c)
                verification.Scalar(energy.metal[c] * e, 1, "unit-Fss energy conservation", 1e-7);
            white.ior = 1;
            const auto absent = Energy(white, view);
            for (unsigned c = 0; c < 3; ++c)
            {
                verification.Scalar(absent.dielectric[c], 1, "absent interface compensation", 1e-12);
                verification.Scalar(absent.dielectricAlbedo[c], 0, "absent interface attenuation", 1e-12);
            }
            for (double thickness : {0.0, 550.0})
            {
                Material mixed = Evaluate(input, 0x7ff);
                mixed.base = {0.15, 0.4, 0.8};
                mixed.tint = {0.7, 0.9, 1};
                mixed.metal = 0.6;
                mixed.thickness = thickness;
                mixed.filmIor = 1.4;
                Material dielectric = mixed, metal = mixed;
                dielectric.metal = 0;
                metal.metal = 1;
                const Vector a = CompensatedFresnel(mixed, Energy(mixed, view), 0.65);
                const Vector b = CompensatedFresnel(dielectric, Energy(dielectric, view), 0.65) * 0.4 +
                                 CompensatedFresnel(metal, Energy(metal, view), 0.65) * 0.6;
                for (unsigned c = 0; c < 3; ++c)
                    verification.Scalar(a[c], b[c], "independent closure metallic mix", 1e-12);
            }
        }
    std::cout << "PRINCIPLED_GGX_CLOSURE_INVARIANTS_OK checks=" << verification.checks << '\n';
}
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
        VerifyGgxClosureContract();
        const auto source = root / "Tools" / "regression" / "principled_layered_probe.slang";
        const auto table =
            LoadSheenTable(root / "Tools" / "blender" / "fixtures" / "principled-layered-5.1.1" / "sheen-ltc.csv");
        const auto fixtures = Fixtures();
        std::vector<ProbeInput> inputs;
        for (const Fixture& fixture : fixtures)
        {
            inputs.push_back(fixture.input);
        }
        const auto artifact = root / "Build" / "Obj" / "PrincipledLayeredProbe" / "gpu-readback.csv";
        std::filesystem::create_directories(artifact.parent_path());
        std::ofstream readback(artifact);
        std::ofstream referenceArtifact(artifact.parent_path() / "cpu-reference.csv");
        std::ofstream costs(artifact.parent_path() / "compile-cost.csv");
        readback << "mask,case,angle,field,x,y,z,w\n" << std::setprecision(9);
        referenceArtifact << "mask,case,angle,field,x,y,z,w\n" << std::setprecision(9);
        costs << "mask,backend,bytecode_bytes\n";
        if (!readback || !referenceArtifact || !costs)
        {
            throw std::runtime_error("GPU readback artifact could not be opened");
        }
        MaterialProbe::CompilerRuntime compiler;
        compiler.Initialize(root);
        Verification verification;
        unsigned compiled = 0, rejected = 0, gpuVariants = 0;
        std::string error;
        for (unsigned mask : {0x7Fu, 0xFFu, 0x17Fu, 0x27Fu, 0x47Fu, 0x1FFu, 0x7FFu})
        {
            std::vector<uint8_t> dxil;
            const std::vector<std::string> defines{"LX_MATERIAL_FEATURE_MASK=" + std::to_string(mask)};
            for (bool spirv : {false, true})
            {
                std::vector<uint8_t> code;
                if (!compiler.Compile(source, "CSMain", SLANG_STAGE_COMPUTE, spirv, defines, error, &code))
                {
                    throw std::runtime_error("Layered compile mask=" + std::to_string(mask) +
                                             (spirv ? " SPIR-V\n" : " DXIL\n") + error);
                }
                costs << mask << ',' << (spirv ? "spirv" : "dxil") << ',' << code.size() << '\n';
                if (!spirv)
                {
                    dxil = std::move(code);
                }
                ++compiled;
            }
            MaterialProbe::ComputeReadback gpu;
            const auto results = gpu.Run(dxil, inputs, kAngles, kFields);
            ++gpuVariants;
            for (size_t index = 0; index < fixtures.size(); ++index)
            {
                const auto& fixture = fixtures[index];
                const Material material = Evaluate(fixture.input, mask);
                for (unsigned angle = 0; angle < kAngles; ++angle)
                {
                    const double cosine = kCosines[angle % 4];
                    const double sine = std::sqrt(1 - cosine * cosine);
                    const Vector view = angle < 4 ? Vector{sine, 0, cosine} : Vector{0, sine, cosine};
                    const Expected expected = Reference(material, view, mask, table);
                    const Float4* output = results.data() + (index * kAngles + angle) * kFields;
                    const std::string label = std::to_string(mask) + "/" + fixture.name + "/" + std::to_string(angle);
                    for (unsigned field = 0; field < kFields; ++field)
                    {
                        const Float4 actual = output[field], reference = expected.fields[field];
                        const double tolerance = 5e-5;
                        verification.Scalar(actual.x, reference.x, label + "/" + std::to_string(field) + ".x",
                                            tolerance);
                        verification.Scalar(actual.y, reference.y, label + "/" + std::to_string(field) + ".y",
                                            tolerance);
                        verification.Scalar(actual.z, reference.z, label + "/" + std::to_string(field) + ".z",
                                            tolerance);
                        verification.Scalar(actual.w, reference.w, label + "/" + std::to_string(field) + ".w",
                                            tolerance);
                        readback << mask << ',' << fixture.name << ',' << angle << ',' << field << ',' << actual.x
                                 << ',' << actual.y << ',' << actual.z << ',' << actual.w << '\n';
                        referenceArtifact << mask << ',' << fixture.name << ',' << angle << ',' << field << ','
                                          << reference.x << ',' << reference.y << ',' << reference.z << ','
                                          << reference.w << '\n';
                    }
                    for (size_t channel = 0; channel < 3; ++channel)
                    {
                        const Vector direct = Rgb(output[14]), doubled = Rgb(output[23]), furnace = Rgb(output[21]);
                        verification.Scalar(doubled[channel], 2 * direct[channel], label + "/linearLight", 1e-5);
                        if (fixture.bounded)
                        {
                            ++verification.furnaceChecks;
                            if (furnace[channel] < -1e-5 || furnace[channel] > 1.005)
                            {
                                throw std::runtime_error(
                                    label + "/whiteFurnace out of bounds: " + std::to_string(furnace[channel]));
                            }
                        }
                    }
                }
            }
            const auto findCase = [&](const char* name) {
                const auto found = std::find_if(fixtures.begin(), fixtures.end(),
                                                [&](const Fixture& fixture) { return fixture.name == name; });
                if (found == fixtures.end())
                {
                    throw std::runtime_error("Missing invariant fixture");
                }
                return size_t(found - fixtures.begin());
            };
            const auto compare = [&](const char* first, const char* second, unsigned field) {
                const size_t a = findCase(first), b = findCase(second);
                for (unsigned angle = 0; angle < kAngles; ++angle)
                {
                    const Vector actual = Rgb(results[(a * kAngles + angle) * kFields + field]);
                    const Vector expected = Rgb(results[(b * kAngles + angle) * kFields + field]);
                    for (size_t channel = 0; channel < 3; ++channel)
                    {
                        verification.Scalar(actual[channel], expected[channel],
                                            std::string(first) + "/" + second + "/invariant/" + std::to_string(field),
                                            1e-4);
                    }
                }
            };
            for (unsigned field : {14u, 15u, 16u, 17u, 18u, 19u, 20u, 21u})
            {
                compare("blender_defaults", "explicit_defaults", field);
                compare("explicit_defaults", "film_ior_one", field);
                compare("explicit_defaults", "film_matches_substrate", field);
                compare("anisotropic", "anisotropic_whole_turn", field);
                compare("all_layers", "all_layers_alpha_zero", field);
            }
            compare("all_layers", "all_layers_ao_zero", 14);
            compare("all_layers", "all_layers_ao_zero", 23);
            if (mask == 0x7ff)
            {
                std::vector<ProbeInput> whiteMetal;
                for (const auto [roughness, anisotropy] : {std::pair{.5f, 0.f}, {.5f, 1.f}, {.88f, .9f}, {1.f, 1.f}})
                {
                    ProbeInput input;
                    input.baseAlpha = {1, 1, 1, 1};
                    input.metalIorLevelAo.x = 1;
                    input.normalRoughness.w = roughness;
                    input.tintAnisotropy.w = anisotropy;
                    whiteMetal.push_back(input);
                }
                const auto budget = gpu.Run(dxil, whiteMetal, kAngles, kFields);
                Verification invariant;
                unsigned rawExcessViews = 0;
                for (size_t index = 0; index < whiteMetal.size(); ++index)
                    for (unsigned angle = 0; angle < kAngles; ++angle)
                    {
                        const double cosine = kCosines[angle % 4], sine = std::sqrt(1 - cosine * cosine);
                        const Vector view = angle < 4 ? Vector{sine, 0, cosine} : Vector{0, sine, cosine};
                        const auto expected = Reference(Evaluate(whiteMetal[index], mask), view, mask, table);
                        const Float4* actual = budget.data() + (index * kAngles + angle) * kFields;
                        for (unsigned field = 0; field < kFields; ++field)
                        {
                            invariant.Scalar(actual[field].x, expected.fields[field].x, "white metal x", 5e-5);
                            invariant.Scalar(actual[field].y, expected.fields[field].y, "white metal y", 5e-5);
                            invariant.Scalar(actual[field].z, expected.fields[field].z, "white metal z", 5e-5);
                            invariant.Scalar(actual[field].w, expected.fields[field].w, "white metal w", 5e-5);
                        }
                        if (actual[6].x > 1)
                        {
                            ++rawExcessViews;
                            for (double value : Rgb(actual[21]))
                                invariant.Scalar(value, 1, "white metal compensated furnace", 1e-5);
                        }
                    }
                if (!rawExcessViews)
                    throw std::runtime_error("White metal test did not exercise the prepared energy budget");
                std::cout << "PRINCIPLED_GGX_BUDGET_GPU_OK cases=4 rawExcessViews=" << rawExcessViews
                          << " checks=" << invariant.checks << '\n';
            }
            std::cout << "LAYERED_VARIANT_GPU_OK mask=" << mask << " cases=" << fixtures.size() << '\n';
        }
        for (unsigned mask : {0x80u, 0x100u, 0x200u, 0x400u})
        {
            for (bool spirv : {false, true})
            {
                if (compiler.Compile(source, "CSMain", SLANG_STAGE_COMPUTE, spirv,
                                     {"LX_MATERIAL_FEATURE_MASK=" + std::to_string(mask)}, error) ||
                    error.find("Layered features require core Specular/IOR") == std::string::npos)
                {
                    throw std::runtime_error("Invalid Layered dependency mask was accepted\n" + error);
                }
                ++rejected;
            }
        }
        readback.close();
        referenceArtifact.close();
        costs.close();
        std::cout << "LAYERED_MAX_ERROR field=" << verification.maximumLabel << '\n';
        std::cout << "PRINCIPLED_LAYERED_COMPILE_OK compiled=" << compiled << " rejected=" << rejected << '\n';
        std::cout << std::setprecision(9) << "PRINCIPLED_LAYERED_GPU_OK cases=" << fixtures.size()
                  << " angles=" << kAngles << " variants=" << gpuVariants << " checks=" << verification.checks
                  << " furnaceChecks=" << verification.furnaceChecks
                  << " maxNormalizedError=" << verification.maximumError << '\n';
        return 0;
    }
    catch (const std::exception& exception)
    {
        std::cerr << exception.what() << '\n';
        return 1;
    }
}
