#pragma once

// Three-channel wave-optics approximation: phase wavelengths are 650/550/450 nm.
// These are effective RGB wavelengths, not a spectral/CIE reconstruction.
// MAT-9 measures this approximation against the pinned Cycles reference.
float2 FilmMultiply(float2 a, float2 b)
{
    return float2(a.x * b.x - a.y * b.y, a.x * b.y + a.y * b.x);
}

float2 FilmDivide(float2 a, float2 b)
{
    return float2(dot(a, b), a.y * b.x - a.x * b.y) / max(dot(b, b), 1e-20);
}

float2 FilmSquareRoot(float2 value)
{
    const float magnitude = length(value);
    return float2(sqrt(max(0.5 * (magnitude + value.x), 0.0)),
        sqrt(max(0.5 * (magnitude - value.x), 0.0)));
}

float FilmAiryReflectance(float r12, float2 r23, float phase)
{
    const float2 propagated = FilmMultiply(r23, float2(cos(phase), sin(phase)));
    const float2 numerator = propagated + float2(r12, 0.0);
    const float2 denominator = r12 * propagated + float2(1.0, 0.0);
    return saturate(dot(numerator, numerator) / max(dot(denominator, denominator), 1e-20));
}

float FilmReflectance(float cosine, float filmIor, float thickness,
    float2 substrateIor, float wavelength)
{
    const float cosIncident = saturate(cosine);
    if (cosIncident == 0.0)
    {
        return 1.0;
    }
    const float sineSquared = 1.0 - cosIncident * cosIncident;
    const float cosFilmSquared = 1.0 - sineSquared / (filmIor * filmIor);
    if (cosFilmSquared <= 0.0)
    {
        return 1.0;
    }
    const float cosFilm = sqrt(cosFilmSquared);
    const float filmQ = filmIor * cosFilm;
    const float r12s = (cosIncident - filmQ) / max(cosIncident + filmQ, 1e-20);
    const float r12p = (filmIor * cosIncident - cosFilm)
        / max(filmIor * cosIncident + cosFilm, 1e-20);
    const float2 substrateSquared = FilmMultiply(substrateIor, substrateIor);
    const float2 substrateQ = FilmSquareRoot(substrateSquared - float2(sineSquared, 0.0));
    const float2 r23s = FilmDivide(float2(filmQ, 0.0) - substrateQ,
        float2(filmQ, 0.0) + substrateQ);
    const float2 pTerm = substrateSquared * cosFilm;
    const float2 r23p = FilmDivide(pTerm - filmIor * substrateQ, pTerm + filmIor * substrateQ);
    const float phase = 12.566370614359172 * filmQ * thickness / wavelength;
    return 0.5 * (FilmAiryReflectance(r12s, r23s, phase)
        + FilmAiryReflectance(r12p, r23p, phase));
}

// Recover a conductor n+ik from F0 and the F82 response. This is an RGB optical
// estimate; the authored metallic F0/F82 remain exact when film is disabled.
void FilmConductorIor(float3 f0, float3 f82, out float3 n, out float3 k)
{
    const float3 reflection = min(f0, 0.999);
    const float3 root = sqrt(reflection);
    n = lerp((1.0 + root) / (1.0 - root), (1.0 - reflection) / (1.0 + reflection), f82);
    k = sqrt(max((reflection * (n + 1.0) * (n + 1.0) - (n - 1.0) * (n - 1.0))
        / (1.0 - reflection), 0.0));
}

float3 EvaluateFilmRgb(float cosine, float filmIor, float thickness, float3 n, float3 k)
{
    return float3(FilmReflectance(cosine, filmIor, thickness, float2(n.r, k.r), 650.0),
        FilmReflectance(cosine, filmIor, thickness, float2(n.g, k.g), 550.0),
        FilmReflectance(cosine, filmIor, thickness, float2(n.b, k.b), 450.0));
}
