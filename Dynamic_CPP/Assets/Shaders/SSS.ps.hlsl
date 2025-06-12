#include "Sampler.hlsli"
#define NUM_SAMPLE 200
#define DENSITY 2.
#define WEIGHT 0.2
#define DECAY 0.92
#define EXPOSURE 0.4
Texture2D DepthTexture : register(t0);
Texture2D Diffuse : register(t1);
Texture2D MetalRough : register(t2);

struct PixelShaderInput // see Fullscreen.vs.hlsl
{
    float4 position : SV_POSITION;
    float2 texCoord : TEXCOORD0;
};

cbuffer DeferredCBuffer : register(b0)
{
    float2 g_direction;
    float g_strength;
    float g_width;
    float g_CameraFOV;
    int3 pad;
}

static const int NUM_SAMPLES = 25;

// 25 samples kernel
static const float4 kernel[NUM_SAMPLES] =
{
    float4(0.530605,    0.613514,       0.739601,        0),
    float4(0.000973794, 1.11862e-005,   9.43437e-007,   -3),
    float4(0.00333804,  7.85443e-005,   1.2945e-005,    -2.52083),
    float4(0.00500364,  0.00020094,     5.28848e-005,   -2.08333),
    float4(0.00700976,  0.00049366,     0.000151938,    -1.6875),
    float4(0.0094389,   0.00139119,     0.000416598,    -1.33333),
    float4(0.0128496,   0.00356329,     0.00132016,     -1.02083),
    float4(0.017924,    0.00711691,     0.00347194,     -0.75),
    float4(0.0263642,   0.0119715,      0.00684598,     -0.520833),
    float4(0.0410172,   0.0199899,      0.0118481,      -0.333333),
    float4(0.0493588,   0.0367726,      0.0219485,      -0.1875),
    float4(0.0402784,   0.0657244,      0.04631,        -0.0833333),
    float4(0.0211412,   0.0459286,      0.0378196,      -0.0208333),
    float4(0.0211412,   0.0459286,      0.0378196,       0.0208333),
    float4(0.0402784,   0.0657244,      0.04631,         0.0833333),
    float4(0.0493588,   0.0367726,      0.0219485,       0.1875),
    float4(0.0410172,   0.0199899,      0.0118481,       0.333333),
    float4(0.0263642,   0.0119715,      0.00684598,      0.520833),
    float4(0.017924,    0.00711691,     0.00347194,      0.75),
    float4(0.0128496,   0.00356329,     0.00132016,      1.02083),
    float4(0.0094389,   0.00139119,     0.000416598,     1.33333),
    float4(0.00700976,  0.00049366,     0.000151938,     1.6875),
    float4(0.00500364,  0.00020094,     5.28848e-005,    2.08333),
    float4(0.00333804,  7.85443e-005,   1.2945e-005,     2.52083),
    float4(0.000973794, 1.11862e-005,   9.43437e-007,    3)
};

float4 main(PixelShaderInput input) : SV_TARGET
{
    // Fetch color of current pixel:
    float4 colorM = Diffuse.SampleLevel(LinearSampler, input.texCoord, 0);
    
    // Fetch linear depth of current pixel:
    float depthM = DepthTexture.SampleLevel(LinearSampler, input.texCoord, 0).r;

    // Calculate the sssWidth scale (1.0 for a unit plane sitting on the
    // projection window):
    float distanceToProjectionWindow = 1.0 / tan(0.5 * g_CameraFOV * 3.141592 / 180.0);
    float scale = distanceToProjectionWindow / depthM;

    // Calculate the final step to fetch the surrounding pixels:
    float2 finalStep = g_width * scale * g_direction;
    finalStep *= g_strength; // Modulate it using the strength.
    finalStep *= 0.333f; // Divide by 3 as the kernels range from -3 to 3.

    // Accumulate the center sample:
    float4 colorBlurred = colorM;
    colorBlurred.rgb *= kernel[0].rgb;
    
    // Accumulate the other samples:
    [unroll]
    for (int i = 1; i < NUM_SAMPLES; i++)
    {
        // Fetch color and depth for current sample:
        float2 offset = input.texCoord + kernel[i].a * finalStep;
        float4 color = Diffuse.SampleLevel(LinearSampler, offset, 0);

        // follow the surface
        {
            float depth = DepthTexture.SampleLevel(LinearSampler, offset, 0).r;
            float s = saturate(300.0f * distanceToProjectionWindow * g_width * abs(depthM - depth));
            color.rgb = lerp(color.rgb, colorM.rgb, s);
        }

        // Accumulate:
        colorBlurred.rgb += kernel[i].rgb * color.rgb;
    }
    
    //{
    //    // Normalized pixel coordinates (from 0 to 1)
    //    float2 uv = input.texCoord;
    //    float4 col = Diffuse.Sample(PointSampler, uv);
    //    float2 lightPos = g_direction; 
    //    float2 sampleStep = (uv - lightPos) * 0.5;
    //    sampleStep *= DENSITY / float(NUM_SAMPLE);  
    //    float illuminationDecay = 1.;
    //    for (int i = 0; i < NUM_SAMPLE; i++)
    //    {
    //        uv -= sampleStep;
    //        float4 sampleCol = Diffuse.Sample(PointSampler, uv);
    //    
    //     sampleCol *= illuminationDecay * WEIGHT;
    //     col += sampleCol;
    //    
    //        float brightness = (sampleCol.x + sampleCol.y + sampleCol.z) / 3.;
    //        brightness *= illuminationDecay * WEIGHT;
    //        col += brightness;
    //    
    //        illuminationDecay *= DECAY;
    //    }
    //    float4 fragColor = float4(col.xyz * g_strength, 1);
    //    colorBlurred += fragColor;
    //}
    //return colorM;
    return colorBlurred;
}
