#include "Sampler.hlsli"
#include "Lighting.hlsli"
#include "Shading.hlsli"
#include "BRDF.hlsli"

static const uint RENDERING_OBJECT_FLAG_USE_GLOBAL_DIF_PROBE = 1; //  0000000000001 // use global diffuse probe
static const uint RENDERING_OBJECT_FLAG_USE_GLOBAL_SPEC_PROBE = 2; //  0000000000010 // use global specular probe
static const uint RENDERING_OBJECT_FLAG_SSS = 4; //  0000000000100 // use sss
static const uint RENDERING_OBJECT_FLAG_POM = 8; //  0000000001000 // use pom
static const uint RENDERING_OBJECT_FLAG_REFLECTION = 16; //  0000000010000 // use reflection mask
static const uint RENDERING_OBJECT_FLAG_FOLIAGE = 32; //  0000000100000 // is foliage
static const uint RENDERING_OBJECT_FLAG_TRANSPARENT = 64; //  0000001000000 // is transparent
static const uint RENDERING_OBJECT_FLAG_FUR = 128; //  0000010000000 // is fur
static const uint RENDERING_OBJECT_FLAG_SKIP_DEFERRED_PASS = 256; //  0000100000000 // skip deferred pass
static const uint RENDERING_OBJECT_FLAG_SKIP_INDIRECT_DIF = 512; //  0001000000000 // skip indirect specular lighting
static const uint RENDERING_OBJECT_FLAG_SKIP_INDIRECT_SPEC = 1024; //  0010000000000 // skip indirect specular lighting
static const uint RENDERING_OBJECT_FLAG_GPU_INDIRECT_DRAW = 2048; //  0100000000000 // used for GPU indirect drawing
static const uint RENDERING_OBJECT_FLAG_TRIPLANAR_MAPPING = 4096; //  1000000000000 // use triplanar mapping

Texture2D DepthTexture : register(t0);
Texture2D Diffuse : register(t1);
Texture2D MetalRough : register(t2);
Texture2D Normals : register(t3);

struct PixelShaderInput // see Fullscreen.vs.hlsl
{
    float4 position : SV_POSITION;
    float2 texCoord : TEXCOORD0;
};

cbuffer DeferredCBuffer : register(b0)
{
    float4x4 InvProjMatrix;
    float4x4 InvViewMatrix;
    float4x4 ViewProjMatrix;
    float4 CameraPosition;
    float StepSize;
    float MaxThickness;
    float Time;
    int MaxRayCount;
}

float3 ReconstructWorldPosFromDepth(float2 uv, float depth, float4x4 invProj, float4x4 invView)
{
    //float ndcX = uv.x * 2 - 1;
    //float ndcY = 1 - uv.y * 2; // Remember to flip y!!!
    //float4 viewPos = mul(invProj, float4(ndcX, ndcY, depth, 1.0f));
    //viewPos = viewPos / viewPos.w;
    //return mul(invView, viewPos).xyz;
    
    
    // clip space between [-1, 1]
    // flip y so that +ve y is upwards
    float2 clipXY = uv * 2.0 - 1.0;
    clipXY.y = -clipXY.y;

    // NOTE: depth is not linearized
    // Also in range [0, 1] due to DirectX Convention
    float4 clipSpace = float4(clipXY, depth, 1.0);
    float4 viewSpace = mul(invProj, clipSpace);

    // perspective divide
    viewSpace /= viewSpace.w;

    float4 worldSpace = mul(invView, viewSpace);
    return worldSpace.xyz;
}
float noise(float2 seed)
{
    return frac(sin(dot(seed.xy, float2(12.9898, 78.233))) * 43758.5453);
}
float4 Raytrace(float3 reflectionWorld, const int maxCount, float stepSize, float3 pos, float2 uv)
{
    float4 color = float4(0.0, 0.0f, 0.0f, 0.0f);
    float3 step = stepSize * reflectionWorld;
    bool success = false;
    
    [loop]
    for (int i = 1; i <= maxCount; i++)
    {
        float3 ray = (i + noise(uv + Time)) * step;
        float3 rayPos = pos + ray;
        float4 vpPos = mul(ViewProjMatrix, float4(rayPos, 1.0f));
        //color = vpPos;
        float rayDepth = vpPos.z / vpPos.w;
        float2 rayUv = vpPos.xy / vpPos.w * 0.5 + 0.5;
        rayUv.y = 1 - rayUv.y; // y축 반전 (DirectX 좌표계)
        float gbufferDepth = DepthTexture.Sample(LinearSampler, rayUv).r;
        if (rayUv.x < 0.0 || rayUv.x > 1.0 || rayUv.y < 0.0 || rayUv.y > 1.0)
            break;
        if (rayDepth - gbufferDepth > 0 && rayDepth - gbufferDepth < MaxThickness)
        {
            float a = 0.3f * pow(min(1.0, (stepSize * maxCount / 2) / length(ray)), 2.0);
            color = color * (1.0f - a) + float4(Diffuse.Sample(LinearSampler, rayUv).rgb, 1.0f) * a;
            color = float4(gbufferDepth, gbufferDepth, gbufferDepth, 1);
            break;
            //success = true;
        }
    }
    //if (!success)
    //{
    //    TODO i.e read from local cubemap
    //}
	
    return color;
}

float4 main(PixelShaderInput IN) : SV_TARGET
{
    float4 color = Diffuse.Sample(LinearSampler, IN.texCoord);
    
    //uint width, height;
    //GbufferExtra2Texture.GetDimensions(width, height);
    
    //uint objectFlags = GbufferExtra2Texture.Load(uint3(IN.TexCoord * uint2(width, height), 0)).r;
    //if (!(objectFlags & RENDERING_OBJECT_FLAG_REFLECTION) ||
    //    (objectFlags & RENDERING_OBJECT_FLAG_FOLIAGE) || (objectFlags & RENDERING_OBJECT_FLAG_TRANSPARENT) || (objectFlags & RENDERING_OBJECT_FLAG_FUR))
    //    return color;

    //float reflectionMaskFactor = GbufferExtraTexture.Sample(LinearSampler, IN.TexCoord).r;
    //if (reflectionMaskFactor < 0.0001)
    //    return color;
    
    float depth = DepthTexture.Sample(LinearSampler, IN.texCoord).r;
    if (depth >= 1.0f) 
        return color;
    
    float3 normal = Normals.Sample(LinearSampler, IN.texCoord).xyz;
    normal = normalize(normal * 2.0 - 1.0);
    
    float metallic = MetalRough.Sample(LinearSampler, IN.texCoord).b; // B 채널이 metallic
    float roughness = MetalRough.Sample(LinearSampler, IN.texCoord).g; // G 채널이 roughness
    
    float4 worldSpacePosition = float4(ReconstructWorldPosFromDepth(IN.texCoord, depth, InvProjMatrix, InvViewMatrix), 1.0f);
    float3 camDir = normalize(worldSpacePosition.xyz - CameraPosition.xyz);
    float3 refDir = (normalize(reflect(normalize(camDir), normal))).rgb;
    
    float reflectFactor = (1.0 - roughness) * (0.04 * (1.0 - metallic) + metallic);
    
    float4 reflectedColor = Raytrace(refDir, MaxRayCount, StepSize, worldSpacePosition.rgb, IN.texCoord);

    float edgeFade = 1.f - pow(length(IN.texCoord.xy - 0.5f) * 2.f, 2.f);
    reflectFactor *= edgeFade;
    
    //return float4(depth, depth, depth, 1);
    return /*color +*/ reflectedColor; // * reflectFactor;
}