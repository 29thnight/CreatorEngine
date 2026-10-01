#include "material_probe_compiler.h"
#include "principled_layered_reference.h"
#include "../../Lattice/Material/LXMaterialExecution.h"

#include <iomanip>

namespace
{
using namespace MaterialProbe::Reference;
constexpr unsigned kViews = 6;
constexpr unsigned kSpecialFields = 32;
constexpr std::array<double, 3> kViewsCos{0.1, 0.5, 0.9};

struct SpecialInput
{
    ProbeInput base;
    Float4 specialWeightScaleIor{0.0f, 0.0f, 0.05f, 1.4f};
    Float4 radiusAnisotropy{1.0f, 0.2f, 0.1f, 0.0f};
    Float4 volumeColorDensity{0.5f, 0.5f, 0.5f, 1.0f};
    Float4 volumeAbsorptionG{};
    Float4 volumeEmissionStrength{1.0f, 1.0f, 1.0f, 0.0f};
    Float4 distanceFaceSource{0.7f, 1.0f, 0.8f, 1.0f};
    Float4 profileDistance{0.02f, 0.0f, 0.0f, 0.0f};
};
static_assert(sizeof(SpecialInput) == 288);

struct SpecialFixture
{
    std::string name;
    SpecialInput input;
};

std::vector<SpecialFixture> SpecialFixtures()
{
    std::vector<SpecialFixture> fixtures;
    const auto add = [&](const char* name, SpecialInput input) { fixtures.push_back({name, input}); };
    SpecialInput input;
    input.base.options.x = 1;
    add("blender_defaults", input);
    input = {};
    add("explicit_defaults", input);
    input.specialWeightScaleIor.x = 1;
    add("transmission_one", input);
    input.specialWeightScaleIor.x = 0.5f;
    add("transmission_half", input);
    input.specialWeightScaleIor.x = 1;
    input.base.metalIorLevelAo.y = 1;
    add("glass_ior_one", input);
    input.base.metalIorLevelAo.y = 1.5f;
    input.base.metalIorLevelAo.z = 0;
    add("glass_ignores_base_specular_level", input);
    input.base.metalIorLevelAo.z = 0.5f;
    input.base.normalRoughness.w = 0;
    input.distanceFaceSource.y = 0;
    add("backface_mirror_tir", input);
    input.base.normalRoughness.w = 0.5f;
    add("backface_rough_glass", input);
    input = {};
    input.base.metalIorLevelAo.x = 1;
    input.specialWeightScaleIor.x = 1;
    input.specialWeightScaleIor.y = 1;
    add("metal_ignores_transmission_sss", input);
    input.base.metalIorLevelAo.x = 0.4f;
    add("mixed_metal_transmission_sss", input);
    input = {};
    input.specialWeightScaleIor.y = 1;
    add("subsurface_one", input);
    input.base.baseAlpha = {0.85f, 0.4f, 0.2f, 1.0f};
    input.radiusAnisotropy = {1.0f, 0.5f, 0.25f, 0.0f};
    add("subsurface_color_radius", input);
    input.specialWeightScaleIor.z = 0;
    add("subsurface_zero_scale", input);
    input.specialWeightScaleIor.z = 0.05f;
    input.radiusAnisotropy.x = 0;
    add("subsurface_local_channel", input);
    input.radiusAnisotropy.x = 1;
    input.radiusAnisotropy.w = 0.9f;
    add("subsurface_anisotropic_transport", input);
    input.specialWeightScaleIor.w = 2;
    add("subsurface_boundary_ior", input);
    input.specialWeightScaleIor.x = 1;
    add("glass_removes_subsurface", input);
    input = {};
    input.specialWeightScaleIor = {0.4f, 0.7f, 0.1f, 1.4f};
    input.base.coatWeightRoughIorFilmThickness = {0.7f, 0.3f, 1.5f, 250.0f};
    input.base.coatNormalSheenWeight.w = 0.2f;
    input.base.coatTintFilmIor = {0.7f, 0.8f, 0.9f, 1.33f};
    input.base.tintAnisotropy.w = 0.6f;
    input.base.emissionColorStrength = {1, 0.5f, 0.2f, 2};
    add("all_layers_special", input);
    input.base.baseAlpha.w = 0;
    add("all_layers_alpha_zero", input);
    input.base.baseAlpha.w = 1;
    input.base.metalIorLevelAo.w = 0;
    add("all_layers_ao_zero", input);
    input = {};
    input.volumeColorDensity.w = 0;
    add("empty_medium", input);
    input.volumeEmissionStrength = {1, 0.2f, 0.1f, 4};
    add("empty_medium_emission", input);
    input.volumeColorDensity = {0.2f, 0.5f, 0.8f, 2};
    input.volumeAbsorptionG = {0.1f, 0.25f, 0.9f, 0.0f};
    input.volumeEmissionStrength.w = 0;
    add("colored_medium", input);
    input.volumeColorDensity = {2, 4, 0, 1};
    add("hdr_scattering_color", input);
    input = {};
    input.volumeAbsorptionG.w = 0.9f;
    add("volume_forward_phase", input);
    input.volumeAbsorptionG.w = -0.9f;
    add("volume_backward_phase", input);
    input.volumeAbsorptionG.w = 2;
    input.distanceFaceSource.z = 1;
    add("phase_forward_peak_clamp", input);
    input.volumeAbsorptionG.w = -2;
    add("phase_backward_peak_clamp", input);
    input = {};
    input.volumeColorDensity.w = 1e-6f;
    add("volume_density_cutoff", input);
    input.volumeColorDensity.w = 2e-5f;
    input.distanceFaceSource.x = 1e-6f;
    input.volumeEmissionStrength.w = 0.4f;
    add("volume_small_optical_depth", input);
    input = {};
    input.volumeColorDensity.w = 1;
    input.distanceFaceSource.x = 1000;
    add("opaque_medium", input);
    input.distanceFaceSource.x = 0;
    add("zero_path", input);
    input = {};
    input.specialWeightScaleIor = {-1, -2, -1, 0};
    input.radiusAnisotropy = {-1, 0, 1, -1};
    input.volumeColorDensity = {-1, 2, 0, -1};
    input.volumeEmissionStrength.w = -2;
    input.distanceFaceSource.x = -1;
    add("clamps_low", input);
    input.specialWeightScaleIor = {2, 2, 0.2f, 3};
    input.radiusAnisotropy.w = 2;
    input.base.baseAlpha = {1, 1, 1, 2};
    add("clamps_high_white_furnace", input);
    input = {};
    input.specialWeightScaleIor.x = 1;
    input.base.coatWeightRoughIorFilmThickness.w = 400;
    input.base.tintAnisotropy = {0.5f, 0.7f, 0.9f, 0.0f};
    add("glass_thin_film", input);
    return fixtures;
}

Vector Refract(Vector incident, Vector normal, double eta)
{
    const double cosine = Dot(incident, normal);
    const double discriminant = 1 - eta * eta * (1 - cosine * cosine);
    return discriminant < 0 ? Vector{} : incident * eta - normal * (eta * cosine + std::sqrt(discriminant));
}

double ReverseBits(unsigned index)
{
    double reversed = 0, weight = 0.5;
    for (; index; index >>= 1)
    {
        reversed += (index & 1) * weight;
        weight *= 0.5;
    }
    return reversed;
}

Vector TransmissionIntegral(const Material& glass, Vector view)
{
    const double cosine = Clamp(Dot(glass.normal, view));
    if (glass.ior == 1 || glass.roughness == 0)
    {
        return Vector{1, 1, 1} - Fresnel(glass, cosine);
    }
    const Frame frame = MakeFrame(glass, false);
    Vector result{};
    for (unsigned index = 0; index < 1024; ++index)
    {
        const double reversed = ReverseBits(index), angle = 2 * kPi * index / 1024;
        const Vector local = Unit({frame.ax * std::sqrt(reversed) * std::cos(angle),
                                   frame.ay * std::sqrt(reversed) * std::sin(angle), std::sqrt(1 - reversed)});
        const Vector half = frame.tangent * local[0] + frame.bitangent * local[1] + frame.normal * local[2];
        const double vh = Dot(view, half);
        if (vh <= 0)
        {
            continue;
        }
        const Vector light = Refract(view * -1, half, 1 / glass.ior);
        const double nl = -Dot(frame.normal, light);
        if (nl > 0)
        {
            const Vector front = light - frame.normal * (2 * Dot(frame.normal, light));
            const double weight =
                4 * nl * Visibility(frame, view, front) * vh / std::max(Dot(frame.normal, half), 1e-6) / 1024;
            result = result + (Vector{1, 1, 1} - Fresnel(glass, vh)) * weight;
        }
    }
    return Nonnegative(result);
}

struct Profile
{
    Vector zr{}, zv{}, attenuation{}, normalization{}, local{};
};

Profile DiffusionProfile(Vector color, Vector radius, double scale, double ior, double g)
{
    Profile profile;
    const double fd = std::clamp(-1.4399 / (ior * ior) + 0.7099 / ior + 0.6681 + 0.0636 * ior, 0.0, 0.99);
    const double boundary = (1 + fd) / (1 - fd);
    for (size_t channel = 0; channel < 3; ++channel)
    {
        const double length = radius[channel] * scale, albedo = Clamp(color[channel]);
        const double extinction = 1 / std::max(length, 1e-6);
        const double absorption = extinction * (1 - albedo);
        const double reduced = std::max(absorption + extinction * albedo * (1 - g), 1e-8);
        const double diffusion = 1 / (3 * reduced);
        profile.zr[channel] = 1 / reduced;
        profile.zv[channel] = profile.zr[channel] + 4 * boundary * diffusion;
        profile.attenuation[channel] = std::sqrt(absorption / diffusion);
        profile.normalization[channel] = std::exp(-profile.attenuation[channel] * profile.zr[channel]) +
                                         std::exp(-profile.attenuation[channel] * profile.zv[channel]);
        profile.local[channel] = length <= 0 || albedo <= 0 ? 1 : 0;
    }
    return profile;
}

Vector ProfileDensity(const Profile& profile, double distance)
{
    Vector result{};
    for (size_t channel = 0; channel < 3; ++channel)
    {
        if (profile.local[channel])
        {
            continue;
        }
        const double a = profile.attenuation[channel];
        for (double depth : {profile.zr[channel], profile.zv[channel]})
        {
            const double length = std::hypot(depth, std::max(distance, 0.0));
            result[channel] += depth * (a + 1 / length) * std::exp(-a * length) / (length * length);
        }
        result[channel] /= std::max(2 * kPi * profile.normalization[channel], 1e-20);
    }
    return result;
}

Vector ProfileCdf(const Profile& profile, double distance)
{
    Vector result{};
    for (size_t channel = 0; channel < 3; ++channel)
    {
        double remaining = 0;
        for (double depth : {profile.zr[channel], profile.zv[channel]})
        {
            const double length = std::hypot(depth, std::max(distance, 0.0));
            remaining += depth / length * std::exp(-profile.attenuation[channel] * length);
        }
        result[channel] =
            profile.local[channel] ? 1 : Clamp(1 - remaining / std::max(profile.normalization[channel], 1e-20));
    }
    return result;
}

double Phase(double cosine, double g)
{
    return (1 - g * g) / (4 * kPi * std::pow(1 + g * g - 2 * g * std::clamp(cosine, -1.0, 1.0), 1.5));
}

std::array<Float4, kSpecialFields> SpecialReference(SpecialInput input, unsigned mask, Vector view,
                                                    const SheenTable& table)
{
    if (input.base.options.x != 0)
    {
        const auto path = input.distanceFaceSource;
        input = {};
        input.distanceFaceSource = path;
    }
    Material material = Evaluate(input.base, mask);
    if (input.distanceFaceSource.y <= 0)
    {
        material.normal = material.normal * -1;
        material.coatNormal = material.coatNormal * -1;
    }
    const double transmission = (mask & 0x800) ? Clamp(input.specialWeightScaleIor.x) : 0;
    const double subsurface = (mask & 0x1000) ? Clamp(input.specialWeightScaleIor.y) : 0;
    const double scale = std::max(double(input.specialWeightScaleIor.z), 0.0);
    const double sssIor = std::max(double(input.specialWeightScaleIor.w), 1.0);
    const double sssG = std::clamp(double(input.radiusAnisotropy.w), 0.0, double(0.99f));
    const Vector radius = Nonnegative(Rgb(input.radiusAnisotropy));
    const Profile profile = DiffusionProfile(material.base, radius, scale, sssIor, sssG);
    Material metal = material, dielectric = material, glass = material;
    metal.metal = 1;
    dielectric.metal = 0;
    glass.metal = 0;
    glass.level = 0.5;
    glass.aniso = 0;
    if (input.distanceFaceSource.y <= 0)
    {
        glass.filmIor /= glass.ior;
        glass.ior = 1 / glass.ior;
    }
    const Integral metalIntegral = Integrate(metal, view, false),
                   dielectricIntegral = Integrate(dielectric, view, false);
    Integral glassIntegral;
    Vector rawTransmission{};
    if (transmission > 0 && material.metal < 1)
    {
        glassIntegral = Integrate(glass, view, false, false);
        rawTransmission = TransmissionIntegral(glass, view);
    }
    const Vector metalMs{}, dielectricMs{},
                 glassMs = Multiple(glassIntegral);
    auto energy = Energy(material, view);
    const auto baseIntegral = Integrate(material, view, false);
    const auto baseDiffuse = LayeredBaseWeights(material, view, baseIntegral).diffuse;
    const Vector reflectionNormalization = ReflectionBudget(baseDiffuse, baseIntegral.single);
    energy.dielectric = energy.dielectric * reflectionNormalization;
    energy.metal = energy.metal * reflectionNormalization;
    const double remainder = std::max(1 - Maximum(energy.dielectricAlbedo), 0.0);
    const double dWeight = (1 - material.metal) * (1 - transmission), gWeight = (1 - material.metal) * transmission;
    const Vector diffuse = material.base * (dWeight * remainder * (1 - subsurface));
    Vector sss{}, trans{}, normalization{};
    for (size_t channel = 0; channel < 3; ++channel)
    {
        sss[channel] = Clamp(material.base[channel]) * dWeight * remainder * subsurface;
        trans[channel] = gWeight * std::sqrt(Clamp(material.base[channel])) *
                         std::max(1 - glassIntegral.single[channel] - glassMs[channel], 0.0);
        normalization[channel] = rawTransmission[channel] > 1e-8 ? trans[channel] / rawTransmission[channel] : 0;
    }
    const Frame frame = MakeFrame(material, false), glassFrame = MakeFrame(glass, false);
    const double sign = input.distanceFaceSource.y > 0 ? 1 : -1;
    const Vector light = Unit({0.4, -0.1, sign * 0.9});
    const Vector back = light - material.normal * (2 * Dot(material.normal, light));
    const Vector half = Unit(view + light);
    const double nv = Clamp(Dot(material.normal, view)), nl = Clamp(Dot(material.normal, light)),
                 vh = Clamp(Dot(view, half));
    const Expected layered = Reference(material, view, mask, table);
    const Vector lower = Rgb(layered.fields[11]);
    const double distribution = Distribution(frame, half), visibility = Visibility(frame, view, light);
    // The shared Layered reference uses (0.35,-0.2,0.8), whereas this probe uses
    // another light. Evaluate the top layers directly from their analytic terms.
    Vector top{};
    const Integral coatIntegral = Integrate(material, view, true);
    const Vector coatMs{};
    const double sheenTransmission = layered.fields[10].w;
    if (material.coat > 0)
    {
        const Frame cf = MakeFrame(material, true);
        const double cnl = Clamp(Dot(material.coatNormal, light));
        if (cnl > 0 && Dot(material.coatNormal, view) > 0)
        {
            top = (CompensatedFresnel(material, energy, vh, true) * (Distribution(cf, half) * Visibility(cf, view, light)) +
                   coatMs * (1 / kPi)) *
                  (sheenTransmission * material.coat * cnl);
        }
    }
    const Vector sn = Unit(material.normal * (1 - Clamp(material.coat)) + material.coatNormal * Clamp(material.coat));
    const Vector st = Projected(view, sn);
    const Vector coefficients = Rgb(layered.fields[10]);
    const Vector sheenColor = material.sheenTint * (material.sheen * coefficients[2]);
    const double snl = Clamp(Dot(sn, light));
    if (snl > 0 && Dot(sn, view) > 0)
    {
        const double a = coefficients[0], b = coefficients[1];
        const Vector transformed{a * Dot(st, light) + b * snl, a * Dot(Cross(sn, st), light), snl};
        top = top + sheenColor * (snl * a * a / (kPi * std::pow(Dot(transformed, transformed), 2)));
    }
    const Vector multiple = metalMs * material.metal + dielectricMs * dWeight + glassMs * gWeight;
    const Vector specular =
        (Fresnel(metal, vh) * energy.metal * material.metal + Fresnel(dielectric, vh) * energy.dielectric * dWeight) * (distribution * visibility) +
        Fresnel(glass, vh) * (gWeight * Distribution(glassFrame, half) * Visibility(glassFrame, view, light));
    const Vector direct = top + lower * (specular + (multiple + diffuse) * (1 / kPi)) * nl;
    Vector transmittedDirect{};
    Vector refractHalf = Unit(view + back * glass.ior, glass.normal);
    if (Dot(refractHalf, glass.normal) < 0)
    {
        refractHalf = refractHalf * -1;
    }
    const double rvh = Dot(view, refractHalf), rlh = Dot(back, refractHalf);
    if (glass.ior != 1 && gWeight > 0 && rvh > 0 && rlh < 0)
    {
        const double factor = 4 * Distribution(glassFrame, refractHalf) * Visibility(glassFrame, view, light) *
                              glass.ior * glass.ior * rvh * std::abs(rlh) * nl /
                              std::max(std::pow(rvh + glass.ior * rlh, 2), 1e-12);
        transmittedDirect = lower * normalization * (Vector{1, 1, 1} - Fresnel(glass, rvh)) * factor;
    }
    const double ao = Clamp(std::pow(nv + material.ao, std::exp2(-16 * material.roughness - 1)) - 1 + material.ao);
    const double coatAo = Clamp(
        std::pow(Clamp(Dot(material.coatNormal, view)) + material.ao, std::exp2(-16 * material.coatRoughness - 1)) - 1 +
        material.ao);
    const Vector topAmbient =
        (coatIntegral.single * coatAo + coatMs * material.ao) * (sheenTransmission * material.coat) +
        sheenColor * material.ao;
    const Vector localAmbient =
        topAmbient + lower * (((metalIntegral.single * material.metal + dielectricIntegral.single * dWeight) * reflectionNormalization +
                               glassIntegral.single * gWeight) *
                                  ao +
                              (diffuse + multiple) * material.ao);
    const Vector sssAmbient = lower * sss, transAmbient = lower * trans;
    const Vector total = localAmbient + sssAmbient + transAmbient;
    Vector scattering{}, absorption{}, emission{}, segmentT{}, segmentL{};
    const bool volume = (mask & 0x2000) != 0;
    const double density = volume ? std::max(double(input.volumeColorDensity.w), 0.0) : 0;
    const double volumeG = volume ? std::clamp(double(input.volumeAbsorptionG.w), -double(0.999f), double(0.999f)) : 0;
    const double path = std::max(double(input.distanceFaceSource.x), 0.0);
    for (size_t channel = 0; channel < 3; ++channel)
    {
        const double color = std::max(Rgb(input.volumeColorDensity)[channel], 0.0);
        if (density > 1e-5)
        {
            scattering[channel] = color * density;
            absorption[channel] = std::max(1 - color, 0.0) *
                                  std::max(1 - std::sqrt(std::max(Rgb(input.volumeAbsorptionG)[channel], 0.0)), 0.0) *
                                  density;
        }
        emission[channel] =
            volume && input.volumeEmissionStrength.w > 1e-5
                ? std::max(Rgb(input.volumeEmissionStrength)[channel], 0.0) * input.volumeEmissionStrength.w
                : 0;
        const double extinction = scattering[channel] + absorption[channel];
        segmentT[channel] = std::exp(-extinction * path);
        const double integral = extinction == 0 ? path : -std::expm1(-extinction * path) / extinction;
        segmentL[channel] = integral * (scattering[channel] * input.distanceFaceSource.w + emission[channel]);
    }
    const Vector surfaceEmission = lower * material.emission;
    const Vector background{0.2, 0.4, 0.6};
    const Vector composite =
        segmentL + segmentT * (background * (1 - material.alpha) + (total + surfaceEmission) * material.alpha);
    const Vector ray = Refract(view * -1, glass.normal, 1 / glass.ior);
    const double coreF0 = std::pow((material.ior - 1) / (material.ior + 1), 2) * 2 * material.level;
    const double root = std::sqrt(std::clamp(coreF0, 0.0, 0.99));
    const double effective =
        material.level == 0.5 ? material.ior : (material.ior < 1 ? (1 - root) / (1 + root) : (1 + root) / (1 - root));
    Vector glassF0{};
    for (size_t channel = 0; channel < 3; ++channel)
    {
        glassF0[channel] = Clamp(std::pow((glass.ior - 1) / (glass.ior + 1), 2) * glass.tint[channel]);
    }
    std::array<Float4, kSpecialFields> fields{};
    fields[0] = {float(transmission), float(subsurface), float(scale), float(sssIor)};
    fields[1] = Pack(radius, sssG);
    fields[2] = Pack(glassF0, glass.ior);
    fields[3] = Pack(material.normal, effective);
    fields[4] = Pack(glassIntegral.single, glassIntegral.albedo);
    fields[5] = Pack(glassIntegral.average);
    fields[6] = Pack(rawTransmission);
    fields[7] = Pack(trans, gWeight);
    fields[8] = Pack(diffuse);
    fields[9] = Pack(sss);
    fields[10] = Pack(direct);
    fields[11] = Pack(Maximum(sss) > 0 ? lower * (nl / kPi) : Vector{});
    fields[12] = Pack(transmittedDirect);
    fields[13] = Pack(localAmbient);
    fields[14] = Pack(sssAmbient);
    fields[15] = Pack(transAmbient);
    fields[16] = Pack(total);
    fields[17] = Pack(surfaceEmission, material.alpha);
    fields[18] = Pack(ProfileDensity(profile, input.profileDistance.x));
    fields[19] = Pack(ProfileCdf(profile, input.profileDistance.x));
    fields[20] = Pack(profile.local, volumeG);
    fields[21] = Pack(scattering);
    fields[22] = Pack(absorption);
    fields[23] = Pack(emission);
    fields[24] = Pack(segmentT);
    fields[25] = Pack(segmentL);
    fields[26] = Pack(composite);
    fields[27] = {};
    fields[28] = Pack(ray, Dot(ray, ray) > 0 ? 1 : 0);
    fields[29] = Pack(Fresnel(glass, nv));
    fields[30] = Pack(direct * 2);
    fields[31] = {float(Phase(input.distanceFaceSource.z, volumeG)), float(Phase(-input.distanceFaceSource.z, volumeG)),
                  float(1 / (4 * kPi)), 0};
    return fields;
}

struct Verification
{
    unsigned checks{}, furnaceChecks{};
    double maximum{};
    std::string label;

    void Scalar(double actual, double expected, const std::string& name, double tolerance = 1e-4)
    {
        ++checks;
        const double error = std::abs(actual - expected) / std::max(1.0, std::abs(expected));
        if (error > maximum)
        {
            maximum = error;
            label = name;
        }
        if (!std::isfinite(actual) || !std::isfinite(expected) || error > tolerance)
        {
            throw std::runtime_error(name + " actual=" + std::to_string(actual) +
                                     " expected=" + std::to_string(expected));
        }
    }
};

template<class Function>
double Quadrature(const Function& function, double a, double b, double tolerance, unsigned depth = 28)
{
    const double m = (a + b) * 0.5, fa = function(a), fm = function(m), fb = function(b);
    const double quarter = function((a + m) * 0.5), threeQuarter = function((m + b) * 0.5);
    const double whole = (b - a) * (fa + 4 * fm + fb) / 6;
    const double split = (b - a) * (fa + 4 * quarter + 2 * fm + 4 * threeQuarter + fb) / 12;
    if (depth == 0 || std::abs(split - whole) <= 15 * tolerance)
    {
        return split + (split - whole) / 15;
    }
    return Quadrature(function, a, m, tolerance * 0.5, depth - 1) +
           Quadrature(function, m, b, tolerance * 0.5, depth - 1);
}

void VerifyTransportIntegrals(Verification& verification, const std::vector<SpecialFixture>& fixtures)
{
    for (const auto& fixture : fixtures)
    {
        const auto& input = fixture.input;
        const Vector radius = Nonnegative(Rgb(input.radiusAnisotropy));
        const auto profile =
            DiffusionProfile(Rgb(input.base.baseAlpha), radius, std::max(double(input.specialWeightScaleIor.z), 0.0),
                             std::max(double(input.specialWeightScaleIor.w), 1.0),
                             std::clamp(double(input.radiusAnisotropy.w), 0.0, double(0.99f)));
        for (size_t channel = 0; channel < 3; ++channel)
        {
            if (profile.local[channel])
            {
                continue;
            }
            const double scale = profile.zr[channel];
            const auto radialMass = [&](double normalizedRadius) {
                const double r = normalizedRadius * scale;
                return 2 * kPi * r * ProfileDensity(profile, r)[channel] * scale;
            };
            verification.Scalar(Quadrature(radialMass, 0, 4, 1e-10), ProfileCdf(profile, scale * 4)[channel],
                                fixture.name + "/profileMass", 1e-8);
            verification.Scalar(ProfileCdf(profile, scale * 1e8)[channel], 1.0, fixture.name + "/profileTotalMass",
                                1e-6);
        }
    }
    for (double g : {-.999, -.8, -.2, 0.0, .2, .8, .999})
    {
        // Resolve the HG peak with a quadratic map around its forward direction.
        const auto solidAngleMass = [&](double u) {
            const double cosine = (g >= 0 ? 1 : -1) * (1 - 2 * u * u);
            return 8 * kPi * u * Phase(cosine, g);
        };
        verification.Scalar(Quadrature(solidAngleMass, 0, 1, 1e-10), 1.0, "phaseTotalMass", 1e-7);
    }
}

void VerifyIndependentControls(Verification& verification, const std::vector<Float4>& results,
                               const std::vector<SpecialFixture>& fixtures, unsigned mask)
{
    const auto values = [&](const char* name, unsigned view) {
        const auto found = std::find_if(fixtures.begin(), fixtures.end(),
                                        [&](const SpecialFixture& fixture) { return fixture.name == name; });
        if (found == fixtures.end())
        {
            throw std::runtime_error("Missing metamorphic fixture");
        }
        return results.data() + (size_t(found - fixtures.begin()) * kViews + view) * kSpecialFields;
    };
    for (unsigned view = 0; view < kViews; ++view)
    {
        const auto all = values("all_layers_special", view);
        const auto alpha = values("all_layers_alpha_zero", view);
        const auto ao = values("all_layers_ao_zero", view);
        const auto glass = values("transmission_one", view);
        const auto glassLevel = values("glass_ignores_base_specular_level", view);
        const auto defaults = values("blender_defaults", view);
        const auto explicitDefaults = values("explicit_defaults", view);
        for (unsigned field = 0; field < kSpecialFields; ++field)
        {
            for (size_t channel = 0; channel < 3; ++channel)
            {
                verification.Scalar(Rgb(defaults[field])[channel], Rgb(explicitDefaults[field])[channel],
                                    "defaultInputs", 1e-6);
                if (field != 26)
                {
                    verification.Scalar(Rgb(all[field])[channel], Rgb(alpha[field])[channel],
                                        "alphaDoesNotDriveClosures", 1e-6);
                }
            }
        }
        for (unsigned field : {10u, 11u, 12u, 14u, 15u, 17u})
        {
            for (size_t channel = 0; channel < 3; ++channel)
            {
                verification.Scalar(Rgb(all[field])[channel], Rgb(ao[field])[channel], "aoOnlyLocalIbl", 1e-6);
            }
        }
        if ((mask & 0x1000u) != 0)
        {
            const auto white = values("subsurface_one", view);
            const auto colored = values("subsurface_color_radius", view);
            for (size_t channel = 0; channel < 3; ++channel)
            {
                verification.Scalar(Rgb(white[11])[channel], Rgb(colored[11])[channel], "subsurfaceSourceIsUntinted",
                                    1e-6);
            }
        }
        if ((mask & 0x800u) == 0)
        {
            continue;
        }
        for (unsigned field : {7u, 10u, 12u, 15u, 16u, 28u, 29u})
        {
            for (size_t channel = 0; channel < 3; ++channel)
            {
                verification.Scalar(Rgb(glass[field])[channel], Rgb(glassLevel[field])[channel], "glassUsesAuthoredIor",
                                    1e-6);
            }
        }
    }
}

void VerifyExecution(Verification& verification)
{
    using namespace LX;
    for (uint32_t mask : {0x7Fu, 0x87Fu, 0x107Fu, 0x207Fu, 0x387Fu, 0x3FFFu, 0x2000u})
    {
        const auto requirements = LXAnalyzeMaterialExecution(mask);
        const LXMaterialExecutionResources resources{true, true, true};
        const auto check = [&](LXMaterialExecutionRoute route, LXMaterialExecutionResources provided,
                               LXMaterialExecutionError expected) {
            verification.Scalar(int(LXValidateMaterialExecution(requirements, route, provided)), int(expected),
                                "execution", 0);
        };
        check(LXMaterialExecutionRoute::Forward, resources, LXMaterialExecutionError::None);
        check(LXMaterialExecutionRoute::Deferred, resources,
              requirements.forward ? LXMaterialExecutionError::SpecialRequiresForward : LXMaterialExecutionError::None);
        if (requirements.refraction)
        {
            check(LXMaterialExecutionRoute::Forward, {false, true, true}, LXMaterialExecutionError::MissingRefraction);
        }
        if (requirements.subsurface)
        {
            check(LXMaterialExecutionRoute::Forward, {true, false, true}, LXMaterialExecutionError::MissingSubsurface);
        }
        if (requirements.volume)
        {
            check(LXMaterialExecutionRoute::Forward, {true, true, false}, LXMaterialExecutionError::MissingVolume);
        }
    }
    for (uint32_t mask : {0x800u, 0x1000u, 0x4000u, 0x40000000u})
    {
        const auto requirements = LXAnalyzeMaterialExecution(mask);
        verification.Scalar(
            int(requirements.error),
            int(mask >= 0x4000 ? LXMaterialExecutionError::UnknownFeatures : LXMaterialExecutionError::MissingCoreIor),
            "executionInvalid", 0);
    }
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
        const auto source = root / "Tools" / "regression" / "principled_special_probe.slang";
        const auto output = root / "Build" / "Obj" / "PrincipledSpecialProbe";
        std::filesystem::create_directories(output);
        const auto table =
            LoadSheenTable(root / "Tools" / "blender" / "fixtures" / "principled-layered-5.1.1" / "sheen-ltc.csv");
        const auto fixtures = SpecialFixtures();
        std::vector<SpecialInput> inputs;
        for (const auto& fixture : fixtures)
        {
            inputs.push_back(fixture.input);
        }
        std::ofstream readback(output / "gpu-readback.csv"), reference(output / "cpu-reference.csv"),
            costs(output / "compile-cost.csv");
        if (!readback || !reference || !costs)
        {
            throw std::runtime_error("Special artifacts could not be opened");
        }
        readback << "mask,case,view,field,x,y,z,w\n" << std::setprecision(9);
        reference << "mask,case,view,field,x,y,z,w\n" << std::setprecision(9);
        costs << "mask,backend,stage,bytecode_bytes\n";
        MaterialProbe::CompilerRuntime compiler;
        compiler.Initialize(root);
        Verification verification;
        VerifyExecution(verification);
        VerifyTransportIntegrals(verification, fixtures);
        unsigned compiled = 0, rejected = 0;
        std::string error;
        for (unsigned mask : {0x7Fu, 0x87Fu, 0x107Fu, 0x207Fu, 0x387Fu, 0x3FFFu, 0x2000u})
        {
            std::vector<uint8_t> dxil;
            for (bool spirv : {false, true})
            {
                for (bool compute : {true, false})
                {
                    std::vector<uint8_t> code;
                    const char* entry = compute ? "CSMain" : "PSMain";
                    if (!compiler.Compile(source, entry, compute ? SLANG_STAGE_COMPUTE : SLANG_STAGE_FRAGMENT, spirv,
                                          {"LX_MATERIAL_FEATURE_MASK=" + std::to_string(mask), "LX_MATERIAL_ROUTE=2"},
                                          error, &code))
                    {
                        throw std::runtime_error("Special compile " + std::to_string(mask) + "/" + entry + "\n" +
                                                 error);
                    }
                    costs << mask << ',' << (spirv ? "spirv" : "dxil") << ',' << entry << ',' << code.size() << '\n';
                    if (compute && !spirv)
                    {
                        dxil = std::move(code);
                    }
                    ++compiled;
                }
            }
            MaterialProbe::ComputeReadback gpu;
            const auto results = gpu.Run(dxil, inputs, kViews, kSpecialFields);
            VerifyIndependentControls(verification, results, fixtures, mask);
            for (size_t index = 0; index < fixtures.size(); ++index)
            {
                for (unsigned viewIndex = 0; viewIndex < kViews; ++viewIndex)
                {
                    const double cosine = kViewsCos[viewIndex % 3], sine = std::sqrt(1 - cosine * cosine);
                    const double sign = fixtures[index].input.distanceFaceSource.y > 0 ? 1 : -1;
                    const Vector view = viewIndex < 3 ? Vector{sine, 0, sign * cosine} : Vector{0, sine, sign * cosine};
                    const auto expected = SpecialReference(fixtures[index].input, mask, view, table);
                    const Float4* values = results.data() + (index * kViews + viewIndex) * kSpecialFields;
                    const std::string label =
                        std::to_string(mask) + "/" + fixtures[index].name + "/" + std::to_string(viewIndex);
                    for (unsigned field = 0; field < kSpecialFields; ++field)
                    {
                        const Float4 actual = values[field], cpu = expected[field];
                        verification.Scalar(actual.x, cpu.x, label + "/" + std::to_string(field) + ".x");
                        verification.Scalar(actual.y, cpu.y, label + "/" + std::to_string(field) + ".y");
                        verification.Scalar(actual.z, cpu.z, label + "/" + std::to_string(field) + ".z");
                        verification.Scalar(actual.w, cpu.w, label + "/" + std::to_string(field) + ".w");
                        readback << mask << ',' << fixtures[index].name << ',' << viewIndex << ',' << field << ','
                                 << actual.x << ',' << actual.y << ',' << actual.z << ',' << actual.w << '\n';
                        reference << mask << ',' << fixtures[index].name << ',' << viewIndex << ',' << field << ','
                                  << cpu.x << ',' << cpu.y << ',' << cpu.z << ',' << cpu.w << '\n';
                    }
                    const Vector furnace = Rgb(values[16]), direct = Rgb(values[10]), twice = Rgb(values[30]);
                    for (size_t channel = 0; channel < 3; ++channel)
                    {
                        if (fixtures[index].name == "volume_small_optical_depth")
                        {
                            verification.Scalar(Rgb(values[25])[channel], Rgb(expected[25])[channel],
                                                label + "/tinySourceIntegral", 1e-12);
                        }
                        verification.Scalar(twice[channel], 2 * direct[channel], label + "/linearLight", 1e-5);
                        ++verification.furnaceChecks;
                        if (furnace[channel] < -1e-5 || furnace[channel] > 1.005)
                        {
                            throw std::runtime_error(label + "/whiteFurnace: " + std::to_string(furnace[channel]));
                        }
                    }
                }
            }
            std::cout << "SPECIAL_VARIANT_GPU_OK mask=" << mask << " cases=" << fixtures.size() << '\n';
        }
        for (unsigned mask : {0x87Fu, 0x107Fu, 0x207Fu})
        {
            for (const char* route : {"", "LX_MATERIAL_ROUTE=1", "LX_MATERIAL_ROUTE=3"})
            {
                for (bool spirv : {false, true})
                {
                    std::vector<std::string> defines{"LX_MATERIAL_FEATURE_MASK=" + std::to_string(mask)};
                    if (*route)
                    {
                        defines.emplace_back(route);
                    }
                    if (compiler.Compile(source, "CSMain", SLANG_STAGE_COMPUTE, spirv, defines, error) ||
                        error.find("Special material") == std::string::npos)
                    {
                        throw std::runtime_error("Special invalid route accepted\n" + error);
                    }
                    ++rejected;
                }
            }
        }
        for (unsigned mask : {0x800u, 0x1000u})
        {
            for (bool spirv : {false, true})
            {
                if (compiler.Compile(source, "CSMain", SLANG_STAGE_COMPUTE, spirv,
                                     {"LX_MATERIAL_FEATURE_MASK=" + std::to_string(mask), "LX_MATERIAL_ROUTE=2"},
                                     error) ||
                    error.find("Special surface features require core Specular/IOR") == std::string::npos)
                {
                    throw std::runtime_error("Invalid Special dependency accepted\n" + error);
                }
                ++rejected;
            }
        }
        std::cout << "SPECIAL_MAX_ERROR field=" << verification.label << '\n';
        std::cout << "PRINCIPLED_SPECIAL_COMPILE_OK compiled=" << compiled << " rejected=" << rejected << '\n';
        std::cout << std::setprecision(9) << "PRINCIPLED_SPECIAL_GPU_OK cases=" << fixtures.size()
                  << " views=" << kViews << " variants=7 checks=" << verification.checks
                  << " furnaceChecks=" << verification.furnaceChecks << " maxNormalizedError=" << verification.maximum
                  << '\n';
        return 0;
    }
    catch (const std::exception& exception)
    {
        std::cerr << exception.what() << '\n';
        return 1;
    }
}
