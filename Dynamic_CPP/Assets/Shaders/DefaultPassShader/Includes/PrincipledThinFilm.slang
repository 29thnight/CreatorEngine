#pragma once
#include "PrincipledThinFilmSensitivity.slang"

// Belcour/Barla Fourier Airy series integrated in linear Rec.709.
// Interface phases/F82 conventions adapted from Cycles 5.1.1 bsdf_util.h
// (BSD-3-Clause). See Tools/blender/fixtures/thin-film-sensitivity-5.1.1/NOTICE.md.
float2 FilmUnitPhase(float2 amplitude)
{
    const float magnitude = length(amplitude);
    return magnitude > 0.0 ? amplitude / magnitude : float2(1.0, 0.0);
}

#ifdef LX_FILM_SENSITIVITY_GPU_TABLE
cbuffer FilmSensitivityConstants : register(b1)
{
    float4 filmSensitivityReal[512];
    float4 filmSensitivityImag[512];
};
#endif
void FilmSensitivity(float opd, out float3 real, out float3 imaginary)
{
    const float coordinate = saturate(6.283185307179586 * opd / 60000.0) * 511.0;
    const uint index = min(uint(coordinate), 511u);
    const uint next = min(index + 1u, 511u);
    const float weight = coordinate - index;
#ifdef LX_FILM_SENSITIVITY_GPU_TABLE
    real = lerp(filmSensitivityReal[index].xyz, filmSensitivityReal[next].xyz, weight);
    imaginary = lerp(filmSensitivityImag[index].xyz, filmSensitivityImag[next].xyz, weight);
#else
    real = lerp(FilmSensitivityReal[index], FilmSensitivityReal[next], weight);
    imaginary = lerp(FilmSensitivityImag[index], FilmSensitivityImag[next], weight);
#endif
}

// F82 controls the magnitude at the film/substrate boundary; n+ik supplies phase.
void FilmConductorIor(float3 f0, float3 f82, out float3 n, out float3 k)
{
    const float3 reflection = min(f0, 0.999);
    const float3 root = sqrt(reflection);
    n = lerp((1.0 + root) / (1.0 - root), (1.0 - reflection) / (1.0 + reflection), f82);
    k = sqrt(max((reflection * (n + 1.0) * (n + 1.0) - (n - 1.0) * (n - 1.0))
        / (1.0 - reflection), 0.0));
}

void FilmSubstrate(float cosFilm, float filmIor, float n, float k, float f82,
    out float2 reflectance, out float2 phaseS, out float2 phaseP)
{
    if (f82 < 0.0)
    {
        const float eta = n / filmIor;
        const float transmittedSquared = eta * eta - (1.0 - cosFilm * cosFilm);
        if (transmittedSquared <= 0.0)
        {
            reflectance = float2(1.0);
            phaseS = phaseP = float2(1.0, 0.0);
            return;
        }
        const float cosSubstrate = sqrt(transmittedSquared) / eta;
        const float s = (cosFilm - eta * cosSubstrate) / (cosFilm + eta * cosSubstrate);
        const float p = (eta * cosFilm - cosSubstrate) / (eta * cosFilm + cosSubstrate);
        reflectance = float2(s * s, p * p);
        phaseS = float2(s >= 0.0 ? 1.0 : -1.0, 0.0);
        phaseP = float2(p >= 0.0 ? 1.0 : -1.0, 0.0);
        return;
    }
    const float nSquared = n * n;
    const float kSquared = k * k;
    const float twoNk = 2.0 * n * k;
    const float t1 = nSquared - kSquared - filmIor * filmIor * (1.0 - cosFilm * cosFilm);
    const float t2 = sqrt(t1 * t1 + twoNk * twoNk);
    const float uSquared = max(0.5 * (t2 + t1), 0.0);
    const float vSquared = max(0.5 * (t2 - t1), 0.0);
    const float u = sqrt(uSquared);
    const float v = sqrt(vSquared);
    const float incidentQ = filmIor * cosFilm;
    phaseS = FilmUnitPhase(float2(incidentQ * incidentQ - uSquared - vSquared, -2.0 * incidentQ * v));
    const float opticalNorm = (nSquared + kSquared) * cosFilm;
    phaseP = FilmUnitPhase(float2(opticalNorm * opticalNorm - filmIor * filmIor * (uSquared + vSquared),
        2.0 * incidentQ * (twoNk * u - (nSquared - kSquared) * v)));
    const float relativeN = n / filmIor;
    const float relativeKSquared = kSquared / (filmIor * filmIor);
    const float f0 = ((relativeN - 1.0) * (relativeN - 1.0) + relativeKSquared)
        / ((relativeN + 1.0) * (relativeN + 1.0) + relativeKSquared);
    const float x82 = 6.0 / 7.0;
    const float fifth82 = x82 * x82 * x82 * x82 * x82;
    const float correction = (lerp(f0, 1.0, fifth82) - f82) * 7.0 / (fifth82 * x82);
    const float x = 1.0 - cosFilm;
    const float fifth = x * x * x * x * x;
    const float magnitude = saturate(lerp(f0, 1.0, fifth) - correction * cosFilm * fifth * x);
    reflectance = float2(magnitude);
}

float3 EvaluateFilmRgb(float cosine, float filmIor, float thickness, float3 n, float3 k, float3 f82)
{
    const float incident = saturate(cosine);
    const float transmittedSquared = filmIor * filmIor - (1.0 - incident * incident);
    if (transmittedSquared <= 0.0 || incident == 0.0)
    {
        return float3(1.0);
    }
    const float cosFilm = sqrt(transmittedSquared) / filmIor;
    const float s = (incident - filmIor * cosFilm) / (incident + filmIor * cosFilm);
    const float p = (filmIor * incident - cosFilm) / (filmIor * incident + cosFilm);
    const float2 r12 = float2(s * s, p * p);
    const float2 phase21 = -float2(s >= 0.0 ? 1.0 : -1.0, p >= 0.0 ? 1.0 : -1.0);
    float3 r23s, r23p, phaseSReal, phaseSImag, phasePReal, phasePImag;
    [unroll]
    for (uint channel = 0; channel < 3; ++channel)
    {
        float2 r23, phaseS, phaseP;
        FilmSubstrate(cosFilm, filmIor, n[channel], k[channel], f82[channel], r23, phaseS, phaseP);
        r23s[channel] = r23.x;
        r23p[channel] = r23.y;
        phaseSReal[channel] = phaseS.x * phase21.x;
        phaseSImag[channel] = phaseS.y * phase21.x;
        phasePReal[channel] = phaseP.x * phase21.y;
        phasePImag[channel] = phaseP.y * phase21.y;
    }
    const float3 productS = r12.x * r23s;
    const float3 productP = r12.y * r23p;
    const float3 rootS = sqrt(productS);
    const float3 rootP = sqrt(productP);
    const float transmissionS = 1.0 - r12.x;
    const float transmissionP = 1.0 - r12.y;
    const float3 multipleS = transmissionS * transmissionS * r23s / max(1.0 - productS, 1e-20);
    const float3 multipleP = transmissionP * transmissionP * r23p / max(1.0 - productP, 1e-20);
    float3 result = 0.5 * (multipleS + r12.x + multipleP + r12.y);
    float3 coefficientS = multipleS - transmissionS;
    float3 coefficientP = multipleP - transmissionP;
    float3 powerSReal = phaseSReal, powerSImag = phaseSImag;
    float3 powerPReal = phasePReal, powerPImag = phasePImag;
    const float opd = 2.0 * filmIor * thickness * cosFilm;
    [unroll]
    for (uint order = 1; order <= 3; ++order)
    {
        float3 sensitivityReal, sensitivityImag;
        FilmSensitivity(order * opd, sensitivityReal, sensitivityImag);
        coefficientS *= rootS;
        coefficientP *= rootP;
        result += coefficientS * (powerSReal * sensitivityReal + powerSImag * sensitivityImag)
            + coefficientP * (powerPReal * sensitivityReal + powerPImag * sensitivityImag);
        const float3 nextSImag = powerSReal * phaseSImag + powerSImag * phaseSReal;
        powerSReal = powerSReal * phaseSReal - powerSImag * phaseSImag;
        powerSImag = nextSImag;
        const float3 nextPImag = powerPReal * phasePImag + powerPImag * phasePReal;
        powerPReal = powerPReal * phasePReal - powerPImag * phasePImag;
        powerPImag = nextPImag;
    }
    return saturate(result);
}
