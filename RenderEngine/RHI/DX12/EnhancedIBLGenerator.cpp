#ifndef DYNAMICCPP_EXPORTS
#include "EnhancedIBLGenerator.h"
#include "DX12DeviceResources.h"
#include "DX12PSOManager.h"
#include "DX12RootSignatureCache.h"

#include <d3dcompiler.h>
#include <cstring>
#include <sstream>
#include <string>

#pragma comment(lib, "d3dcompiler.lib")

namespace
{
    // 유니티 빌드에서 익명 네임스페이스가 합쳐지므로 이름을 고유하게 둔다.
    std::string IblHrToString(HRESULT hr)
    {
        std::ostringstream oss;
        oss << "HRESULT 0x" << std::hex << static_cast<unsigned long>(hr);
        return oss.str();
    }

    // D3D 큐브 면 기저(+X -X +Y -Y +Z -Z). DX11의 면별 카메라(forward/up)에서
    // right = cross(up, forward)로 유도한 것과 같고, D3D 텍셀 규약과도 같다 —
    // 하드웨어 샘플이 (방향 → 텍셀)을 정하므로 여기가 어긋나면 하늘이 뒤집힌다.
    struct IblFaceBasis
    {
        float forward[3];
        float right[3];
        float up[3];
    };
    constexpr IblFaceBasis kIblFaces[6] = {
        { {  1, 0, 0 }, { 0, 0, -1 }, { 0, 1, 0 } },
        { { -1, 0, 0 }, { 0, 0, 1 },  { 0, 1, 0 } },
        { { 0, 1, 0 },  { 1, 0, 0 },  { 0, 0, -1 } },
        { { 0, -1, 0 }, { 1, 0, 0 },  { 0, 0, 1 } },
        { { 0, 0, 1 },  { 1, 0, 0 },  { 0, 1, 0 } },
        { { 0, 0, -1 }, { -1, 0, 0 }, { 0, 1, 0 } },
    };

    // VS·PS가 공유하는 드로우 상수. HLSL cbuffer와 배치가 같아야 한다.
    struct IblDrawConstants
    {
        float forward[4];
        float right[4];
        float up[4];
        float params[4];   // x = roughness
    };

    // ── 면 방향 VS — 풀스크린 삼각형의 uv에서 면 방향을 만든다 ──
    constexpr const char* kIblFaceVS = R"(
cbuffer IblDrawConstants : register(b0)
{
    float4 gForward;
    float4 gRight;
    float4 gUp;
    float4 gParams;
};

struct VSOut
{
    float4 position : SV_POSITION;
    float3 texCoord : TEXCOORD0;
};

VSOut VSMain(uint id : SV_VertexID)
{
    const float2 uv = float2((id << 1) & 2, id & 2);

    VSOut output;
    output.position = float4(uv * float2(2.0f, -2.0f) + float2(-1.0f, 1.0f), 0.0f, 1.0f);
    output.texCoord = gForward.xyz
        + (2.0f * uv.x - 1.0f) * gRight.xyz
        + (1.0f - 2.0f * uv.y) * gUp.xyz;
    return output;
}
)";

    // ── BRDF LUT용 풀스크린 VS — uv가 곧 (NdotV, roughness)다 ──
    constexpr const char* kIblFullscreenVS = R"(
struct VSOut
{
    float4 position : SV_POSITION;
    float2 texCoord : TEXCOORD0;
};

VSOut VSMain(uint id : SV_VertexID)
{
    VSOut output;
    output.texCoord = float2((id << 1) & 2, id & 2);
    output.position = float4(output.texCoord * float2(2.0f, -2.0f) + float2(-1.0f, 1.0f),
        0.0f, 1.0f);
    return output;
}
)";

    // ── rect→cube (DX11 RectToCubeMap.ps의 이식) ──
    constexpr const char* kIblRectToCubePS = R"(
Texture2D    gEquirect : register(t0);
SamplerState gSampler  : register(s0);

struct VSOut
{
    float4 position : SV_POSITION;
    float3 texCoord : TEXCOORD0;
};

static const float2 invAtan = float2(0.15915494309189535, 0.3183098861837907);
float2 SampleSphericalMap(float3 v)
{
    float2 uv = float2(atan2(v.z, v.x), -asin(v.y));
    uv *= invAtan;
    uv += 0.5;
    return uv;
}

float4 PSMain(VSOut input) : SV_TARGET
{
    float2 uv = SampleSphericalMap(normalize(input.texCoord));
    return float4(gEquirect.Sample(gSampler, uv).rgb, 1.0);
}
)";

    // ── 조도 맵 (DX11 IrradianceMap.ps의 이식) ──
    //
    // ★ 원본의 수식·quirk를 그대로 둔다(이식 검수에서 발견·기록):
    //   · color * NoL — 코사인 샘플링의 pdf(cosθ/π)에 이미 cosθ가 있어
    //     수학적으로는 이중 가중(cos² 편향, 전체적으로 어둡다)이다.
    //   · SampleLevel(…, 7) — 소스 큐브맵이 밉 1장이라 밉0으로 클램프되어
    //     의도한 사전 블러가 무동작이다.
    //   · 휘도 상한 초과 샘플의 전량 폐기, 로그 공간 평균 — 톤 정책.
    // 그림의 기준선이 이 결과물이라 여기서 고치면 대조가 성립하지 않는다.
    constexpr const char* kIblIrradiancePS = R"(
TextureCube  gCube    : register(t0);
SamplerState gSampler : register(s0);

struct VSOut
{
    float4 position : SV_POSITION;
    float3 texCoord : TEXCOORD0;
};

static const float PI = 3.14159265359;
static const uint SAMPLE_COUNT = 1024u;
static const float BLUR_MIP_LEVEL = 7.0f;
static const float HARD_CLAMP = 1.2f;
static const float MAX_LUMINANCE = 1.5f;
static const float LOG_POWER = 0.5f;

float RadicalInverse_VdC(uint bits)
{
    bits = (bits << 16) | (bits >> 16);
    bits = ((bits & 0x55555555) << 1) | ((bits & 0xAAAAAAAA) >> 1);
    bits = ((bits & 0x33333333) << 2) | ((bits & 0xCCCCCCCC) >> 2);
    bits = ((bits & 0x0F0F0F0F) << 4) | ((bits & 0xF0F0F0F0) >> 4);
    bits = ((bits & 0x00FF00FF) << 8) | ((bits & 0xFF00FF00) >> 8);
    return float(bits) * 2.3283064365386963e-10;
}

float2 Hammersley(uint i, uint N)
{
    return float2((float) i / (float) N, RadicalInverse_VdC(i));
}

float3 SampleHemisphereCosine(float2 Xi)
{
    float phi = 2.0 * PI * Xi.x;
    float cosTheta = sqrt(1.0 - Xi.y);
    float sinTheta = sqrt(Xi.y);
    return float3(cos(phi) * sinTheta, sin(phi) * sinTheta, cosTheta);
}

float Luminance(float3 color)
{
    return dot(color, float3(0.2126f, 0.7152f, 0.0722f));
}

float4 PSMain(VSOut input) : SV_TARGET
{
    float3 N = normalize(input.texCoord);
    float3 up = abs(N.y) < 0.999 ? float3(0.0, 1.0, 0.0) : float3(1.0, 0.0, 0.0);
    float3 right = normalize(cross(up, N));
    float3 irradiance = float3(0.0, 0.0, 0.0);

    up = cross(N, right);

    for (uint i = 0; i < SAMPLE_COUNT; ++i)
    {
        float2 Xi = Hammersley(i, SAMPLE_COUNT);
        float3 localDir = SampleHemisphereCosine(Xi);
        float3 L = normalize(localDir.x * right + localDir.y * up + localDir.z * N);
        float NoL = localDir.z;
        float3 color = gCube.SampleLevel(gSampler, L, BLUR_MIP_LEVEL).rgb;

        color = min(color, HARD_CLAMP);

        if (Luminance(color) > MAX_LUMINANCE)
            color = 0;

        color = pow(color + 1e-4f, LOG_POWER);

        irradiance += color * NoL;
    }

    irradiance *= (PI / SAMPLE_COUNT);

    irradiance = pow(irradiance, 1.0f / LOG_POWER);

    return float4(irradiance, 1.0);
}
)";

    // ── 프리필터 스페큘러 (DX11 SpecularPreFilter.ps의 이식) ──
    //
    // ★ Sample(자동 LOD)을 그대로 둔다 — 발산하는 중요도 샘플 방향에 화면
    //   미분 기반 LOD는 관행(SampleLevel 0)에서 벗어나지만 원본이 그렇다.
    //   휘도 폐기·로그 공간도 조도 맵과 같은 정책.
    constexpr const char* kIblPrefilterPS = R"(
TextureCube  gCube    : register(t0);
SamplerState gSampler : register(s0);

cbuffer IblDrawConstants : register(b0)
{
    float4 gForward;
    float4 gRight;
    float4 gUp;
    float4 gParams;   // x = roughness
};

struct VSOut
{
    float4 position : SV_POSITION;
    float3 texCoord : TEXCOORD0;
};

static const float PI = 3.14159265359;
static const uint SAMPLE_COUNT = 1024u;
static const float defaultWeight = 0.0000001f;
static const float HARD_CLAMP = 1.2f;
static const float MAX_LUMINANCE = 1.5f;
static const float LOG_POWER = 0.5f;

float RadicalInverse_VdC(uint bits)
{
    bits = (bits << 16u) | (bits >> 16u);
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
    return float(bits) * 2.3283064365386963e-10;
}

float2 Hammersley(uint i, uint N)
{
    return float2(float(i) / float(N), RadicalInverse_VdC(i));
}

float3 ImportanceSampleGGX(float2 st, float3 N, float roughness)
{
    float a = roughness * roughness;

    float phi = 2.0 * PI * st.x;
    float cosTheta = sqrt((1.0 - st.y) / (1.0 + (a * a - 1.0) * st.y));
    float sinTheta = sqrt(1.0 - cosTheta * cosTheta);

    float3 H;
    H.x = cos(phi) * sinTheta;
    H.y = sin(phi) * sinTheta;
    H.z = cosTheta;

    float3 up = abs(N.z) < 0.999 ? float3(0.0, 0.0, 1.0) : float3(1.0, 0.0, 0.0);
    float3 tangent = normalize(cross(up, N));
    float3 bitangent = cross(N, tangent);

    float3 sampleVec = tangent * H.x + bitangent * H.y + N * H.z;
    return normalize(sampleVec);
}

float Luminance(float3 color)
{
    return dot(color, float3(0.2126f, 0.7152f, 0.0722f));
}

float4 PSMain(VSOut input) : SV_TARGET
{
    float3 N = normalize(input.texCoord);
    float3 R = N;
    float3 V = R;
    float totalWeight = defaultWeight;
    float3 prefilteredColor = float3(0.0, 0.0, 0.0);

    for (uint i = 0u; i < SAMPLE_COUNT; ++i)
    {
        float2 st = Hammersley(i, SAMPLE_COUNT);
        float3 H = ImportanceSampleGGX(st, N, gParams.x);
        float3 L = normalize(2.0 * dot(V, H) * H - V);

        float NdotL = saturate(dot(N, L));
        if (NdotL > 0.0)
        {
            float3 color = gCube.Sample(gSampler, L).rgb;

            color = min(color, HARD_CLAMP);

            if (Luminance(color) > MAX_LUMINANCE)
                color = 0;

            color = pow(color + 1e-4f, LOG_POWER);

            prefilteredColor += color * NdotL;
            totalWeight += NdotL;
        }
    }
    prefilteredColor = prefilteredColor / max(totalWeight, defaultWeight);

    prefilteredColor = pow(prefilteredColor, 1.0f / LOG_POWER);

    return float4(prefilteredColor, 1.0);
}
)";

    // ── BRDF LUT (DX11 IntegrateBRDF.ps의 이식) ──
    constexpr const char* kIblBrdfPS = R"(
struct VSOut
{
    float4 position : SV_POSITION;
    float2 texCoord : TEXCOORD0;
};

static const float PI = 3.14159265359;
static const uint SAMPLE_COUNT = 1024u;

float RadicalInverse_VdC(uint bits)
{
    bits = (bits << 16u) | (bits >> 16u);
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
    return float(bits) * 2.3283064365386963e-10;
}

float2 Hammersley(uint i, uint N)
{
    return float2(float(i) / float(N), RadicalInverse_VdC(i));
}

float3 ImportanceSampleGGX(float2 st, float3 N, float roughness)
{
    float a = roughness * roughness;

    float phi = 2.0 * PI * st.x;
    float cosTheta = sqrt((1.0 - st.y) / (1.0 + (a * a - 1.0) * st.y));
    float sinTheta = sqrt(1.0 - cosTheta * cosTheta);

    float3 H;
    H.x = cos(phi) * sinTheta;
    H.y = sin(phi) * sinTheta;
    H.z = cosTheta;

    float3 up = abs(N.z) < 0.999 ? float3(0.0, 0.0, 1.0) : float3(1.0, 0.0, 0.0);
    float3 tangent = normalize(cross(up, N));
    float3 bitangent = cross(N, tangent);

    float3 sampleVec = tangent * H.x + bitangent * H.y + N * H.z;
    return normalize(sampleVec);
}

float GeometrySchlickGGXIBL(float NdotX, float roughness)
{
    float r = roughness;
    float k = (r * r) / 2.0;

    float num = NdotX;
    float denom = NdotX * (1.0 - k) + k;

    return num / denom;
}

float GeometrySmithIBL(float NdotV, float NdotL, float roughness)
{
    float ggx2 = GeometrySchlickGGXIBL(NdotV, roughness);
    float ggx1 = GeometrySchlickGGXIBL(NdotL, roughness);

    return ggx1 * ggx2;
}

float2 IntegrateBRDF(float NdotV, float roughness)
{
    float3 V;
    V.x = sqrt(1.0 - NdotV * NdotV);
    V.y = 0.0;
    V.z = NdotV;

    float A = 0.0;
    float B = 0.0;

    float3 N = float3(0.0, 0.0, 1.0);

    for (uint i = 0u; i < SAMPLE_COUNT; ++i)
    {
        float2 st = Hammersley(i, SAMPLE_COUNT);
        float3 H = ImportanceSampleGGX(st, N, roughness);
        float3 L = normalize(2.0 * dot(V, H) * H - V);

        float NdotL = saturate(L.z);
        float NdotH = saturate(H.z);
        float VdotH = saturate(dot(V, H));

        if (NdotL > 0.0)
        {
            float G = GeometrySmithIBL(NdotV, NdotL, roughness);
            float G_Vis = (G * VdotH) / (NdotH * NdotV);
            float Fc = pow(1.0 - VdotH, 5.0);

            A += (1.0 - Fc) * G_Vis;
            B += Fc * G_Vis;
        }
    }
    A /= float(SAMPLE_COUNT);
    B /= float(SAMPLE_COUNT);
    return float2(A, B);
}

float4 PSMain(VSOut input) : SV_TARGET
{
    // DX11 원본은 float2를 반환하지만 RGBA16F 타깃의 나머지 채널이
    // 미정의라는 D3D12 검증 경고가 나온다. 소비자는 RG만 읽으므로
    // BA를 명시해 채운다 — RG 내용은 원본과 같다.
    return float4(IntegrateBRDF(input.texCoord.x, input.texCoord.y), 0.0f, 1.0f);
}
)";

    bool CompileIblShader(const char* source, const char* entry, const char* target,
        Microsoft::WRL::ComPtr<ID3DBlob>& outBlob, std::string& outError)
    {
        Microsoft::WRL::ComPtr<ID3DBlob> errors;
        const HRESULT hr = D3DCompile(source, strlen(source), nullptr, nullptr,
            nullptr, entry, target, 0, 0, &outBlob, &errors);
        if (FAILED(hr))
        {
            outError = std::string("IBL 셰이더 컴파일 실패(") + entry + "): ";
            if (errors) outError += static_cast<const char*>(errors->GetBufferPointer());
            else        outError += IblHrToString(hr);
            return false;
        }
        return true;
    }
}

bool EnhancedIBLGenerator::Initialize(const EnhancedFrameContext& context,
    std::string& outError)
{
    if (nullptr == context.resources || nullptr == context.psoManager ||
        nullptr == context.rootSignatures)
    {
        outError = "IBL 생성기 컨텍스트가 불완전하다";
        return false;
    }

    return CreatePipelines(context, outError);
}

bool EnhancedIBLGenerator::CreatePipelines(const EnhancedFrameContext& context,
    std::string& outError)
{
    // b0 드로우 상수 · t0 소스 텍스처(테이블) · s0 선형 샘플러.
    // Equirect는 경도 U만 순환하고 위도 V는 극에서 멈춰야 한다. V까지
    // WRAP하면 +Y/-Y 극점에서 반대편 행이 선형 필터에 섞인다.
    D3D12_DESCRIPTOR_RANGE sourceRange{};
    sourceRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    sourceRange.NumDescriptors = 1;
    sourceRange.BaseShaderRegister = 0;

    D3D12_ROOT_PARAMETER params[2]{};
    params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    params[0].Descriptor.ShaderRegister = 0;
    params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[1].DescriptorTable.NumDescriptorRanges = 1;
    params[1].DescriptorTable.pDescriptorRanges = &sourceRange;

    D3D12_STATIC_SAMPLER_DESC sampler{};
    sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.MaxLOD = D3D12_FLOAT32_MAX;
    sampler.ShaderRegister = 0;
    sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_ROOT_SIGNATURE_DESC rootDesc{};
    rootDesc.NumParameters = _countof(params);
    rootDesc.pParameters = params;
    rootDesc.NumStaticSamplers = 1;
    rootDesc.pStaticSamplers = &sampler;

    const auto root = context.rootSignatures->GetOrCreate(rootDesc, outError);
    if (!root.IsValid()) return false;
    m_rootSignature = root.signature;

    ComPtr<ID3DBlob> faceVs;
    ComPtr<ID3DBlob> fullscreenVs;
    ComPtr<ID3DBlob> rectPs;
    ComPtr<ID3DBlob> irradiancePs;
    ComPtr<ID3DBlob> prefilterPs;
    ComPtr<ID3DBlob> brdfPs;
    if (!CompileIblShader(kIblFaceVS, "VSMain", "vs_5_0", faceVs, outError) ||
        !CompileIblShader(kIblFullscreenVS, "VSMain", "vs_5_0", fullscreenVs, outError) ||
        !CompileIblShader(kIblRectToCubePS, "PSMain", "ps_5_0", rectPs, outError) ||
        !CompileIblShader(kIblIrradiancePS, "PSMain", "ps_5_0", irradiancePs, outError) ||
        !CompileIblShader(kIblPrefilterPS, "PSMain", "ps_5_0", prefilterPs, outError) ||
        !CompileIblShader(kIblBrdfPS, "PSMain", "ps_5_0", brdfPs, outError))
    {
        return false;
    }

    const auto makePso = [&](ID3DBlob* vs, ID3DBlob* ps) -> ID3D12PipelineState*
    {
        DX12GraphicsPipelineDesc desc{};
        desc.vsBytecode = vs->GetBufferPointer();
        desc.vsSize = vs->GetBufferSize();
        desc.psBytecode = ps->GetBufferPointer();
        desc.psSize = ps->GetBufferSize();
        desc.rootSignature = root.signature;
        desc.rootSignatureId = root.id;
        desc.inputElements = nullptr;
        desc.inputElementCount = 0;
        desc.topologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        desc.depthEnable = false;
        desc.blendEnable = false;
        desc.cullMode = D3D12_CULL_MODE_NONE;
        desc.numRenderTargets = 1;
        desc.rtvFormats[0] = kFormat;

        return context.psoManager->GetOrCreate(desc, outError);
    };

    m_rectToCubePso = makePso(faceVs.Get(), rectPs.Get());
    if (nullptr == m_rectToCubePso) return false;
    m_irradiancePso = makePso(faceVs.Get(), irradiancePs.Get());
    if (nullptr == m_irradiancePso) return false;
    m_prefilterPso = makePso(faceVs.Get(), prefilterPs.Get());
    if (nullptr == m_prefilterPso) return false;
    m_brdfPso = makePso(fullscreenVs.Get(), brdfPs.Get());
    if (nullptr == m_brdfPso) return false;

    return true;
}

bool EnhancedIBLGenerator::CreateTargets(ID3D12Device* device, uint32_t cubeSize,
    uint32_t brdfSize, std::string& outError)
{
    const auto makeCube = [&](uint32_t size, uint32_t mips,
        ComPtr<ID3D12Resource>& out, const wchar_t* name) -> bool
    {
        D3D12_HEAP_PROPERTIES heap{};
        heap.Type = D3D12_HEAP_TYPE_DEFAULT;

        D3D12_RESOURCE_DESC desc{};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        desc.Width = size;
        desc.Height = size;
        desc.DepthOrArraySize = 6;
        desc.MipLevels = static_cast<UINT16>(mips);
        desc.Format = kFormat;
        desc.SampleDesc.Count = 1;
        desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

        const HRESULT hr = device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE,
            &desc, D3D12_RESOURCE_STATE_RENDER_TARGET, nullptr, IID_PPV_ARGS(&out));
        if (FAILED(hr))
        {
            outError = "IBL 타깃 생성 실패: " + IblHrToString(hr);
            return false;
        }
        out->SetName(name);
        return true;
    };

    if (!makeCube(cubeSize, 1, m_cubeMap, L"IBL.CubeMap")) return false;
    if (!makeCube(cubeSize, 1, m_irradianceMap, L"IBL.Irradiance")) return false;
    if (!makeCube(cubeSize, kPrefilterMips, m_prefilteredMap, L"IBL.Prefiltered")) return false;

    {
        D3D12_HEAP_PROPERTIES heap{};
        heap.Type = D3D12_HEAP_TYPE_DEFAULT;

        D3D12_RESOURCE_DESC desc{};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        desc.Width = brdfSize;
        desc.Height = brdfSize;
        desc.DepthOrArraySize = 1;
        desc.MipLevels = 1;
        desc.Format = kFormat;
        desc.SampleDesc.Count = 1;
        desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

        const HRESULT hr = device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE,
            &desc, D3D12_RESOURCE_STATE_RENDER_TARGET, nullptr, IID_PPV_ARGS(&m_brdfLut));
        if (FAILED(hr))
        {
            outError = "BRDF LUT 생성 실패: " + IblHrToString(hr);
            return false;
        }
        m_brdfLut->SetName(L"IBL.BrdfLut");
    }

    // RTV: 큐브 6 + 조도 6 + 프리필터 36 + LUT 1.
    D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc{};
    rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    rtvHeapDesc.NumDescriptors = 6 + 6 + 6 * kPrefilterMips + 1;
    const HRESULT hr = device->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&m_rtvHeap));
    if (FAILED(hr))
    {
        outError = "IBL RTV 힙 생성 실패: " + IblHrToString(hr);
        return false;
    }
    m_rtvIncrement = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

    return true;
}

bool EnhancedIBLGenerator::Generate(const EnhancedFrameContext& context,
    ID3D12Resource* equirect, DXGI_FORMAT equirectFormat,
    uint32_t cubeSize, uint32_t brdfSize, std::string& outError)
{
    if (nullptr == equirect || 0 == cubeSize || 0 == brdfSize)
    {
        outError = "IBL 입력이 불완전하다";
        return false;
    }

    auto* device = context.resources->GetDevice();
    auto* commandList = context.resources->GetCommandList();

    m_cubeSize = cubeSize;
    m_brdfSize = brdfSize;

    if (!CreateTargets(device, cubeSize, brdfSize, outError)) return false;

    // RTV들을 미리 깔아 둔다. 배치: [0..5] 큐브, [6..11] 조도,
    // [12..47] 프리필터(밉 m 면 f → 12 + m*6 + f), [48] LUT.
    const auto rtvAt = [&](uint32_t index)
    {
        auto handle = m_rtvHeap->GetCPUDescriptorHandleForHeapStart();
        handle.ptr += static_cast<SIZE_T>(index) * m_rtvIncrement;
        return handle;
    };

    for (uint32_t face = 0; face < 6; ++face)
    {
        D3D12_RENDER_TARGET_VIEW_DESC rtv{};
        rtv.Format = kFormat;
        rtv.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2DARRAY;
        rtv.Texture2DArray.FirstArraySlice = face;
        rtv.Texture2DArray.ArraySize = 1;
        device->CreateRenderTargetView(m_cubeMap.Get(), &rtv, rtvAt(face));
        device->CreateRenderTargetView(m_irradianceMap.Get(), &rtv, rtvAt(6 + face));

        for (uint32_t mip = 0; mip < kPrefilterMips; ++mip)
        {
            rtv.Texture2DArray.MipSlice = mip;
            device->CreateRenderTargetView(m_prefilteredMap.Get(), &rtv,
                rtvAt(12 + mip * 6 + face));
        }
        rtv.Texture2DArray.MipSlice = 0;
    }
    device->CreateRenderTargetView(m_brdfLut.Get(), nullptr, rtvAt(48));

    // 면 6장을 그린다. 소스 SRV는 스테이지마다 하나라 테이블도 하나면 된다.
    const auto drawFaces = [&](ID3D12PipelineState* pso, uint32_t rtvBase,
        uint32_t size, D3D12_GPU_DESCRIPTOR_HANDLE source, float roughness)
    {
        const D3D12_VIEWPORT viewport{ 0.f, 0.f,
            static_cast<float>(size), static_cast<float>(size), 0.f, 1.f };
        const D3D12_RECT scissor{ 0, 0,
            static_cast<LONG>(size), static_cast<LONG>(size) };
        commandList->RSSetViewports(1, &viewport);
        commandList->RSSetScissorRects(1, &scissor);
        commandList->SetPipelineState(pso);

        for (uint32_t face = 0; face < 6; ++face)
        {
            IblDrawConstants constants{};
            memcpy(constants.forward, kIblFaces[face].forward, sizeof(float) * 3);
            memcpy(constants.right, kIblFaces[face].right, sizeof(float) * 3);
            memcpy(constants.up, kIblFaces[face].up, sizeof(float) * 3);
            constants.params[0] = roughness;

            const auto cb = context.resources->GetUploadRing().Allocate(
                sizeof(IblDrawConstants), DX12UploadRing::kConstantBufferAlignment);
            if (!cb.IsValid()) return false;
            memcpy(cb.cpuAddress, &constants, sizeof(constants));

            const auto rtvHandle = rtvAt(rtvBase + face);
            commandList->OMSetRenderTargets(1, &rtvHandle, FALSE, nullptr);
            commandList->SetGraphicsRootConstantBufferView(0, cb.gpuAddress);
            commandList->SetGraphicsRootDescriptorTable(1, source);
            commandList->DrawInstanced(3, 1, 0, 0);
        }
        return true;
    };

    const auto transition = [&](ID3D12Resource* resource,
        D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after)
    {
        D3D12_RESOURCE_BARRIER barrier{};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource = resource;
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        barrier.Transition.StateBefore = before;
        barrier.Transition.StateAfter = after;
        commandList->ResourceBarrier(1, &barrier);
    };

    ID3D12DescriptorHeap* heaps[] = { context.resources->GetDescriptorRing().GetHeap() };
    commandList->SetDescriptorHeaps(1, heaps);
    commandList->SetGraphicsRootSignature(m_rootSignature);
    commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    // ── ① rect → cube ──
    const auto equirectTable = context.resources->GetDescriptorRing().Allocate(1);
    if (!equirectTable.IsValid()) { outError = "IBL 디스크립터 부족"; return false; }
    {
        D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
        srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srv.Format = equirectFormat;
        srv.Texture2D.MipLevels = 1;
        device->CreateShaderResourceView(equirect, &srv, equirectTable.CpuAt(0));
    }
    if (!drawFaces(m_rectToCubePso, 0, cubeSize, equirectTable.gpu, 0.f))
    {
        outError = "IBL 업로드 링 부족(rect→cube)";
        return false;
    }

    transition(m_cubeMap.Get(),
        D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

    const auto cubeTable = context.resources->GetDescriptorRing().Allocate(1);
    if (!cubeTable.IsValid()) { outError = "IBL 디스크립터 부족"; return false; }
    {
        D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
        srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
        srv.Format = kFormat;
        srv.TextureCube.MipLevels = 1;
        device->CreateShaderResourceView(m_cubeMap.Get(), &srv, cubeTable.CpuAt(0));
    }

    // ── ② 조도 맵 ──
    if (!drawFaces(m_irradiancePso, 6, cubeSize, cubeTable.gpu, 0.f))
    {
        outError = "IBL 업로드 링 부족(조도)";
        return false;
    }
    transition(m_irradianceMap.Get(),
        D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

    // ── ③ 프리필터 스페큘러 — 밉 m의 거칠기 = m/(밉수-1), 크기는 절반씩 ──
    {
        uint32_t mipSize = cubeSize;
        for (uint32_t mip = 0; mip < kPrefilterMips; ++mip)
        {
            const float roughness =
                static_cast<float>(mip) / static_cast<float>(kPrefilterMips - 1);
            if (!drawFaces(m_prefilterPso, 12 + mip * 6, mipSize, cubeTable.gpu, roughness))
            {
                outError = "IBL 업로드 링 부족(프리필터)";
                return false;
            }
            mipSize = (mipSize > 1) ? mipSize / 2 : 1;
        }
    }
    transition(m_prefilteredMap.Get(),
        D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

    // ── ④ BRDF LUT ──
    {
        const D3D12_VIEWPORT viewport{ 0.f, 0.f,
            static_cast<float>(brdfSize), static_cast<float>(brdfSize), 0.f, 1.f };
        const D3D12_RECT scissor{ 0, 0,
            static_cast<LONG>(brdfSize), static_cast<LONG>(brdfSize) };
        commandList->RSSetViewports(1, &viewport);
        commandList->RSSetScissorRects(1, &scissor);
        commandList->SetPipelineState(m_brdfPso);

        // b0·t0을 형식상 채운다 — BRDF 셰이더는 읽지 않지만, 테이블 파라미터가
        // 선언된 루트를 쓰는 이상 유효한 핸들을 두는 쪽이 안전하다.
        IblDrawConstants constants{};
        const auto cb = context.resources->GetUploadRing().Allocate(
            sizeof(IblDrawConstants), DX12UploadRing::kConstantBufferAlignment);
        if (!cb.IsValid()) { outError = "IBL 업로드 링 부족(LUT)"; return false; }
        memcpy(cb.cpuAddress, &constants, sizeof(constants));

        const auto rtvHandle = rtvAt(48);
        commandList->OMSetRenderTargets(1, &rtvHandle, FALSE, nullptr);
        commandList->SetGraphicsRootConstantBufferView(0, cb.gpuAddress);
        commandList->SetGraphicsRootDescriptorTable(1, cubeTable.gpu);
        commandList->DrawInstanced(3, 1, 0, 0);
    }
    transition(m_brdfLut.Get(),
        D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

    return true;
}

void EnhancedIBLGenerator::Shutdown()
{
    m_cubeMap.Reset();
    m_irradianceMap.Reset();
    m_prefilteredMap.Reset();
    m_brdfLut.Reset();
    m_rtvHeap.Reset();
    m_rectToCubePso = nullptr;
    m_irradiancePso = nullptr;
    m_prefilterPso = nullptr;
    m_brdfPso = nullptr;
    m_rootSignature = nullptr;
    m_cubeSize = 0;
    m_brdfSize = 0;
}

#endif
