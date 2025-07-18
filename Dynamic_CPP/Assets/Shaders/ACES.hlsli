
#define PI 3.1415926535897932384626433832795

// Converts color from Linear sRGB (Rec. 709 primaries) to ACEScg (AP1 primaries)
static const float3x3 sRGB_2_ACEScg =
{
    0.662454, 0.134004, 0.157542,
    0.272229, 0.674082, 0.053689,
    -0.005572, 0.004061, 1.001511
};

// Converts color from ACEScg (AP1 primaries) back to Linear sRGB (Rec. 709 primaries)
static const float3x3 ACEScg_2_sRGB =
{
    1.64102, -0.32480, -0.23649,
    -0.66366, 1.61628, 0.01677,
    0.01179, -0.00829, 0.98833
};

// -- 2. CORE ACES RRT + ODT CURVE --

// This function approximates the combined ACES Reference Rendering Transform (RRT)
// and the sRGB Output Device Transform (ODT) using a rational polynomial fit.
// This is the "look" of ACES.
// Input and output are in ACEScg color space.
float3 RRTAndODTFit(float3 v)
{
    float3 a = v * (v + 0.0245786f) - 0.000090537f;
    float3 b = v * (0.983729f * v + 0.4329510f) + 0.238081f;
    return a / b;
}


// -- 3. OETF (GAMMA CORRECTION) --

// The sRGB Opto-Electrical Transfer Function (OETF).
// This is the standard "gamma correction" for sRGB displays.
// It converts a linear color value to a non-linear value for display.
float OETF_sRGB(float linearValue)
{
    if (linearValue <= 0.0031308)
    {
        return linearValue * 12.92;
    }
    else
    {
        return 1.055 * pow(abs(linearValue), 1.0 / 2.4) - 0.055;
    }
}

float3 OETF_sRGB(float3 linearRGB)
{
    return float3(
        OETF_sRGB(linearRGB.r),
        OETF_sRGB(linearRGB.g),
        OETF_sRGB(linearRGB.b)
    );
}


// -- 4. MAIN TONEMAPPING FUNCTION --

// Applies the full ACES tonemapping pipeline to an input color.
//
// @param linearSRGBColor The input HDR color in linear sRGB space.
// @return The final LDR color in non-linear sRGB space, ready for display.
float3 ApplyACES_Full(float3 linearSRGBColor)
{
    // 1. Transform from the working color space (Linear sRGB) to ACEScg.
    float3 acesCG = mul(sRGB_2_ACEScg, linearSRGBColor);

    // 2. Apply the ACES RRT and ODT tonemapping curve.
    float3 tonemappedACES = RRTAndODTFit(acesCG);

    // 3. Transform from ACEScg back to the display color space (Linear sRGB).
    float3 linearOutput = mul(ACEScg_2_sRGB, tonemappedACES);

    // 4. Clamp the result to the valid [0, 1] range before gamma correction.
    // This prevents negative colors or values over 1 from causing issues.
    linearOutput = saturate(linearOutput);

    // 5. Apply the sRGB OETF (gamma correction) for the final display color.
    float3 displayColor = OETF_sRGB(linearOutput);

    return displayColor;
}