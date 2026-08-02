#ifndef DYNAMICCPP_EXPORTS
#include "EnhancedGBufferPass.h"
#include "DX12DeviceResources.h"
#include "DX12PSOManager.h"
#include "DX12RootSignatureCache.h"
#include "DX12MeshCache.h"
#include "DX12TextureCache.h"
#include "../../Mesh.h"
#include "../../Texture.h"

#include <d3dcompiler.h>
#include <algorithm>
#include <sstream>

#pragma comment(lib, "d3dcompiler.lib")

namespace
{
    // 유니티 빌드에서 익명 네임스페이스가 파일 간 합쳐지므로 이름을 고유하게 둔다.
    std::string GBufferHrToString(HRESULT hr)
    {
        std::ostringstream oss;
        oss << "HRESULT 0x" << std::hex << static_cast<unsigned long>(hr);
        return oss.str();
    }

    // 첫 슬라이스의 셰이더는 소스에 담는다. 재질 셰이더 연결은 씬 연결
    // 슬라이스에서 ShaderSystem·PSOManager와 함께 붙인다.
    //
    // 타깃마다 서로 다른 값을 쓰는 이유는 검증 때문이다 — 다섯 타깃이 실제로
    // 각각 기록되는지 확인하려면 값이 구분되어야 한다. 한 타깃만 기록되고
    // 나머지가 비어 있어도 '그려지긴 한다'로 보이는 것을 막는다.
    constexpr const char* kGBufferShader = R"(
cbuffer PerFrame : register(b0)
{
    float4x4 gViewProjection;
};

// 드로우별 상수를 cbuffer가 아니라 구조화 버퍼로 넘긴다.
//
// 같은 메시·재질을 쓰는 드로우가 여럿이면 인스턴스로 묶어 한 번에 그린다.
// cbuffer로는 그럴 수 없다 — 드로우마다 다른 값을 넣어야 하므로 드로우도
// 그만큼 나뉜다. 인스턴스 인덱스로 읽으면 한 드로우로 끝난다.
struct InstanceData
{
    float4x4 world;
    float4   baseColorFactor;
    float    metallic;
    float    roughness;
    uint     useNormalMap;
    uint     padding;
};

StructuredBuffer<InstanceData> gInstances : register(t4);

Texture2D    gBaseColor     : register(t0);
Texture2D    gNormalMap     : register(t1);
Texture2D    gOccRoughMetal : register(t2);
Texture2D    gEmissive      : register(t3);
SamplerState gSampler       : register(s0);

struct VSIn
{
    float3 position  : POSITION;
    float3 normal    : NORMAL;
    float2 uv        : TEXCOORD0;
    float3 tangent   : TANGENT;
    float3 bitangent : BINORMAL;
};

struct VSOut
{
    float4 position  : SV_POSITION;
    float3 normal    : NORMAL;
    float2 uv        : TEXCOORD0;
    float3 tangent   : TANGENT;
    float3 bitangent : BINORMAL;

    // 픽셀 셰이더는 SV_InstanceID를 받을 수 없다. 재질 값은 정점에서 실어
    // 보내되 보간하지 않는다 — 인스턴스 안에서 상수이기 때문이다.
    nointerpolation float4 baseColorFactor : COLOR0;
    nointerpolation float3 material        : TEXCOORD1;  // x 금속성 · y 거칠기 · z 노멀맵
};

VSOut VSMain(VSIn input, uint instanceId : SV_InstanceID)
{
    const InstanceData instance = gInstances[instanceId];
    const float4x4 gWorld = instance.world;

    VSOut output;
    const float4 worldPosition = mul(float4(input.position, 1.0f), gWorld);
    output.position = mul(worldPosition, gViewProjection);

    output.baseColorFactor = instance.baseColorFactor;
    output.material = float3(instance.metallic, instance.roughness,
        (float)instance.useNormalMap);

    // 법선·탄젠트는 회전만 적용한다. 비균등 스케일에는 역전치 행렬이 필요한데,
    // 그건 스케일이 실제로 문제가 되는 씬이 나왔을 때 상수에 추가한다 —
    // 지금 넣으면 검증할 수 없는 값이 하나 더 늘어난다.
    output.normal    = mul(input.normal,    (float3x3)gWorld);
    output.tangent   = mul(input.tangent,   (float3x3)gWorld);
    output.bitangent = mul(input.bitangent, (float3x3)gWorld);
    output.uv        = input.uv;
    return output;
}

struct PSOut
{
    float4 diffuse    : SV_TARGET0;
    float4 metalRough : SV_TARGET1;
    float4 normal     : SV_TARGET2;
    float4 emissive   : SV_TARGET3;
    uint   bitmask    : SV_TARGET4;
};

PSOut PSMain(VSOut input)
{
    // 텍스처가 없는 슬롯에는 1x1 흰색이 묶여 있다. 그래서 "텍스처가 있으면"
    // 분기가 필요 없다 — 분기 없는 쪽이 셰이더에서 빠르고, 재질마다 다른
    // 셰이더 변형을 만들지 않아도 된다.
    const float4 albedo = gBaseColor.Sample(gSampler, input.uv) * input.baseColorFactor;

    // occlusion/roughness/metallic은 glTF 규약대로 G에 거칠기, B에 금속성이다.
    const float3 orm = gOccRoughMetal.Sample(gSampler, input.uv).rgb;

    float3 normal = normalize(input.normal);
    if (input.material.z != 0.0f)
    {
        // 접선 공간 변환. 탄젠트를 법선에 대해 다시 직교화한다(그람-슈미트) —
        // 보간을 거치면 둘이 어긋나고, 그대로 쓰면 조명이 미묘하게 틀어진다.
        const float3 n = normal;
        const float3 t = normalize(input.tangent - n * dot(n, input.tangent));

        // 종법선은 저장된 것을 쓰되 방향(핸디드니스)만 원본에서 가져온다.
        // cross로 다시 만들면 UV가 뒤집힌 메시에서 조명이 반대로 나온다.
        const float  handedness = (dot(cross(n, t), input.bitangent) < 0.0f) ? -1.0f : 1.0f;
        const float3 b = cross(n, t) * handedness;

        const float3 sampled = gNormalMap.Sample(gSampler, input.uv).rgb * 2.0f - 1.0f;
        normal = normalize(sampled.x * t + sampled.y * b + sampled.z * n);
    }

    PSOut output;
    output.diffuse    = albedo;
    output.metalRough = float4(orm.r, orm.g * input.material.y, orm.b + input.material.x, 1.0f);
    output.normal     = float4(normal * 0.5f + 0.5f, 1.0f);
    output.emissive   = gEmissive.Sample(gSampler, input.uv);
    output.bitmask    = 0xABCDu;
    return output;
}
)";

    bool CompileGBufferShader(const char* entry, const char* target,
        Microsoft::WRL::ComPtr<ID3DBlob>& outBlob, std::string& outError)
    {
        Microsoft::WRL::ComPtr<ID3DBlob> errors;
        const HRESULT hr = D3DCompile(kGBufferShader, strlen(kGBufferShader), nullptr,
            nullptr, nullptr, entry, target, 0, 0, &outBlob, &errors);
        if (FAILED(hr))
        {
            outError = std::string("GBuffer 셰이더 컴파일 실패(") + entry + "): ";
            if (errors) outError += static_cast<const char*>(errors->GetBufferPointer());
            return false;
        }
        return true;
    }
}

DXGI_FORMAT EnhancedGBufferPass::GetRenderTargetFormat(uint32_t index)
{
    // DX11 쪽 구성과 같아야 대조가 성립한다.
    switch (index)
    {
    case 0: return DXGI_FORMAT_R16G16B16A16_FLOAT;  // Diffuse
    case 1: return DXGI_FORMAT_R16G16B16A16_FLOAT;  // MetalRough
    case 2: return DXGI_FORMAT_R16G16B16A16_FLOAT;  // Normal
    case 3: return DXGI_FORMAT_R16G16B16A16_FLOAT;  // Emissive
    case 4: return DXGI_FORMAT_R32_UINT;            // Bitmask
    default: return DXGI_FORMAT_UNKNOWN;
    }
}

bool EnhancedGBufferPass::PrepareFrame(const EnhancedFrameContext& context, std::string& outError)
{
    m_drawGeometry.clear();
    m_drawTextures.clear();
    m_lastDrawCount = 0;

    // 프레임 밀봉된 카메라에서 뷰·투영을 만든다. 스냅샷이 없으면 항등으로 두는데,
    // 그러면 클립 공간에 바로 그리게 되므로 '카메라가 안 붙었다'가 화면에 드러난다.
    m_frameViewProjection = (nullptr != context.camera)
        ? XMMatrixMultiply(context.camera->view, context.camera->projection)
        : XMMatrixIdentity();

    if (nullptr == context.draws || nullptr == context.meshCache) return true;

    // 지오메트리와 재질을 따로 훑는다.
    //
    // 예전에는 메시 중복 제거 블록 안에서 재질까지 올렸는데, 그러면 같은 메시를
    // 다른 재질로 두 번 그릴 때 두 번째 재질이 통째로 건너뛰어진다. 중복 제거의
    // 단위가 둘이 다르다 — 지오메트리는 메시별로, 재질은 재질별로 한 번이다.
    for (const auto& draw : *context.draws)
    {
        if (nullptr == draw.mesh) continue;

        if (m_drawGeometry.find(draw.mesh) == m_drawGeometry.end())
        {
            std::string uploadError;
            const auto entry = context.meshCache->GetOrUpload(draw.mesh, uploadError);
            if (!entry.IsValid())
            {
                // 빈 메시는 그냥 건너뛴다. 업로드 실패는 알린다 — 조용히 안 그리면
                // '왜 이 오브젝트만 안 보이지'가 된다.
                if (!uploadError.empty()) outError = uploadError;
                continue;
            }

            m_drawGeometry.emplace(draw.mesh, entry);
        }
        else if (!m_drawGeometry[draw.mesh].IsValid())
        {
            continue;
        }

        // 재질 텍스처. 없는 슬롯은 캐시가 흰색을 돌려주므로 항상 넷이 채워지고,
        // 셰이더에 분기가 필요 없다.
        if (nullptr != context.textureCache)
        {
            const MaterialKey key{
                draw.baseColor, draw.normalMap, draw.occRoughMetal, draw.emissive };

            if (m_drawTextures.find(key) == m_drawTextures.end())
            {
                DrawTextures textures{};
                for (uint32_t i = 0; i < 4; ++i)
                {
                    std::string textureError;
                    const auto uploaded = context.textureCache->GetOrUpload(key[i], textureError);
                    textures.resources[i] = uploaded.resource;
                    textures.formats[i] = uploaded.format;
                    textures.mipLevels[i] = uploaded.mipLevels;

                    if (!textureError.empty()) outError = textureError;
                }
                m_drawTextures.emplace(key, textures);
            }
        }

        ++m_lastDrawCount;
    }

    m_lastMeshCount = static_cast<uint32_t>(m_drawGeometry.size());
    m_lastMaterialCount = static_cast<uint32_t>(m_drawTextures.size());

    BuildBatches(context);

    return true;
}

void EnhancedGBufferPass::BuildBatches(const EnhancedFrameContext& context)
{
    m_instances.clear();
    m_batches.clear();
    m_lastBatchCount = 0;

    if (nullptr == context.draws) return;

    m_instances.reserve(context.draws->size());

    // 같은 (메시, 재질)을 묶는다.
    //
    // 둘 중 하나라도 다르면 같은 드로우로 묶을 수 없다 — 메시가 다르면 정점·
    // 인덱스 버퍼를, 재질이 다르면 SRV 테이블을 바꿔야 하기 때문이다.
    //
    // ★ 정렬해야 실제로 묶인다.
    //
    // 처음에는 '연속한 같은 것'만 묶었다. 순서를 흔들면 깊이 테스트 결과가
    // 달라질까 봐서였는데, 재 보니 배치 수가 드로우 수와 똑같았다(704 → 704) —
    // 씬의 드로우 순서는 대개 메시가 번갈아 나오므로 연속이 거의 없다.
    // 병합이 통째로 죽어 있었고, 수치를 안 봤으면 몰랐을 것이다.
    //
    // 정렬해도 되는 이유: GBuffer는 불투명만 그리고 깊이 함수가 LESS다.
    // 겹치는 픽셀은 더 가까운 쪽이 이기므로 그리는 순서와 무관하다.
    // (같은 깊이면 순서를 타지만 그건 원래 불안정한 경우다.)
    //
    // 투명 재질이 들어오면 이 전제가 깨진다 — 그때는 불투명만 정렬하고
    // 투명은 뒤에서 앞으로 따로 그려야 한다.
    std::vector<const EnhancedDrawItem*> sorted;
    sorted.reserve(context.draws->size());
    for (const auto& draw : *context.draws)
    {
        if (nullptr == draw.mesh) continue;

        const auto geometry = m_drawGeometry.find(draw.mesh);
        if (geometry == m_drawGeometry.end() || !geometry->second.IsValid()) continue;

        sorted.push_back(&draw);
    }

    std::stable_sort(sorted.begin(), sorted.end(),
        [](const EnhancedDrawItem* a, const EnhancedDrawItem* b)
        {
            if (a->mesh != b->mesh) return a->mesh < b->mesh;

            const MaterialKey keyA{ a->baseColor, a->normalMap, a->occRoughMetal, a->emissive };
            const MaterialKey keyB{ b->baseColor, b->normalMap, b->occRoughMetal, b->emissive };
            return keyA < keyB;
        });

    for (const auto* drawPtr : sorted)
    {
        const auto& draw = *drawPtr;

        const MaterialKey key{
            draw.baseColor, draw.normalMap, draw.occRoughMetal, draw.emissive };

        if (m_batches.empty() || m_batches.back().mesh != draw.mesh
            || m_batches.back().material != key)
        {
            DrawBatch batch{};
            batch.mesh = draw.mesh;
            batch.material = key;
            batch.firstInstance = static_cast<uint32_t>(m_instances.size());
            batch.instanceCount = 0;
            m_batches.push_back(batch);
        }

        InstanceData instance{};
        instance.world = XMMatrixTranspose(draw.worldMatrix);
        instance.baseColorFactor = draw.baseColorFactor;
        instance.metallic = draw.metallic;
        instance.roughness = draw.roughness;
        instance.useNormalMap = draw.useNormalMap;

        m_instances.push_back(instance);
        ++m_batches.back().instanceCount;
    }

    m_lastBatchCount = static_cast<uint32_t>(m_batches.size());
}

bool EnhancedGBufferPass::CreatePipeline(const EnhancedFrameContext& context, std::string& outError)
{
    ComPtr<ID3DBlob> vsBlob;
    ComPtr<ID3DBlob> psBlob;
    if (!CompileGBufferShader("VSMain", "vs_5_0", vsBlob, outError)) return false;
    if (!CompileGBufferShader("PSMain", "ps_5_0", psBlob, outError)) return false;

    // 루트 시그니처는 캐시가 식별자를 준다 — 손번호를 붙이지 않는 것이 3-4의 계약이다.
    //
    // 상수는 디스크립터 테이블이 아니라 루트 CBV로 넘긴다. 업로드 링에서 자른
    // 조각의 GPU 주소를 그대로 꽂으면 되므로 디스크립터를 만들 필요가 없고,
    // 드로우마다 바뀌는 값에는 이쪽이 싸다(테이블은 디스크립터 힙을 거친다).
    D3D12_DESCRIPTOR_RANGE srvRange{};
    srvRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    srvRange.NumDescriptors = 4;   // baseColor · normal · occRoughMetal · emissive

    D3D12_DESCRIPTOR_RANGE samplerRange{};
    samplerRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER;
    samplerRange.NumDescriptors = 1;

    D3D12_ROOT_PARAMETER params[4]{};
    params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    params[0].Descriptor.ShaderRegister = 0;   // b0 — 프레임 상수
    params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;

    // 드로우 상수는 픽셀 셰이더도 읽는다(재질 계수). ALL로 두면 두 단계 모두 본다.
    // 인스턴스 버퍼는 루트 SRV로 넘긴다. 디스크립터를 만들 필요 없이 업로드
    // 링의 GPU 주소를 그대로 꽂으면 되고, 배치마다 한 번이면 된다.
    params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
    params[1].Descriptor.ShaderRegister = 4;   // t4 — 인스턴스 데이터
    params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;

    params[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[2].DescriptorTable.NumDescriptorRanges = 1;
    params[2].DescriptorTable.pDescriptorRanges = &srvRange;
    params[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    params[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[3].DescriptorTable.NumDescriptorRanges = 1;
    params[3].DescriptorTable.pDescriptorRanges = &samplerRange;
    params[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_ROOT_SIGNATURE_DESC rootDesc{};
    rootDesc.NumParameters = _countof(params);
    rootDesc.pParameters = params;
    rootDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    const auto root = context.rootSignatures->GetOrCreate(rootDesc, outError);
    if (!root.IsValid()) return false;
    m_rootSignature = root.signature;

    // 입력 레이아웃. 정점 구조체와 순서가 맞아야 하고, 어긋나면 검증 레이어가
    // 잡아 주지 않는 경우도 있어 화면이 조용히 이상해진다.
    // 오프셋은 엔진 Vertex 구조체를 그대로 따른다:
    //   position 0 · normal 12 · uv0 24 · uv1 32 · tangent 40 · bitangent 52
    // 어긋나면 검증 레이어가 잡아 주지 않는 경우도 있어 화면이 조용히 이상해진다.
    static const D3D12_INPUT_ELEMENT_DESC kInputElements[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0,  0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,    0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TANGENT",  0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 40, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "BINORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 52, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    };

    // 오프셋이 Vertex와 어긋나면 조용히 틀리므로 컴파일 시점에 못박는다.
    static_assert(offsetof(Vertex, normal) == 12, "Vertex 레이아웃이 바뀌었다 — 입력 요소 오프셋을 맞출 것");
    static_assert(offsetof(Vertex, uv0) == 24, "Vertex 레이아웃이 바뀌었다");
    static_assert(offsetof(Vertex, tangent) == 40, "Vertex 레이아웃이 바뀌었다");
    static_assert(offsetof(Vertex, bitangent) == 52, "Vertex 레이아웃이 바뀌었다");

    DX12GraphicsPipelineDesc desc{};
    desc.inputElements = kInputElements;
    desc.inputElementCount = _countof(kInputElements);
    desc.vsBytecode = vsBlob->GetBufferPointer();
    desc.vsSize = vsBlob->GetBufferSize();
    desc.psBytecode = psBlob->GetBufferPointer();
    desc.psSize = psBlob->GetBufferSize();
    desc.rootSignature = root.signature;
    desc.rootSignatureId = root.id;
    desc.depthEnable = true;
    desc.cullMode = D3D12_CULL_MODE_NONE;
    desc.numRenderTargets = kRenderTargetCount;
    for (uint32_t i = 0; i < kRenderTargetCount; ++i)
    {
        desc.rtvFormats[i] = GetRenderTargetFormat(i);
    }
    desc.dsvFormat = kDepthFormat;

    m_pso = context.psoManager->GetOrCreate(desc, outError);
    return nullptr != m_pso;
}

bool EnhancedGBufferPass::Initialize(const EnhancedFrameContext& context, std::string& outError)
{
    if (nullptr == context.resources || nullptr == context.psoManager ||
        nullptr == context.rootSignatures)
    {
        outError = "GBuffer 패스 컨텍스트가 불완전하다";
        return false;
    }

    auto* device = context.resources->GetDevice();

    D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc{};
    rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    rtvHeapDesc.NumDescriptors = kRenderTargetCount;
    if (FAILED(device->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&m_rtvHeap))))
    {
        outError = "GBuffer RTV 힙 생성 실패";
        return false;
    }
    m_rtvIncrement = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

    D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc{};
    dsvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
    dsvHeapDesc.NumDescriptors = 1;
    if (FAILED(device->CreateDescriptorHeap(&dsvHeapDesc, IID_PPV_ARGS(&m_dsvHeap))))
    {
        outError = "GBuffer DSV 힙 생성 실패";
        return false;
    }

    if (!CreatePipeline(context, outError)) return false;

    D3D12_SAMPLER_DESC sampler{};
    sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    sampler.MaxLOD = D3D12_FLOAT32_MAX;

    m_sampler = context.resources->GetSamplerHeap().GetOrCreate(sampler);
    if (0 == m_sampler.ptr)
    {
        outError = "GBuffer 샘플러 생성 실패";
        return false;
    }

    return true;
}

void EnhancedGBufferPass::Declare(EnhancedRenderGraph& graph, const EnhancedFrameContext& context)
{
    // 타깃을 그래프에 선언한다. 실제 생성과 배리어는 그래프가 맡는다 —
    // 이 패스는 무엇을 어떤 상태로 쓸지만 말한다.
    static const char* kNames[kRenderTargetCount] = {
        "GBuffer.Diffuse", "GBuffer.MetalRough", "GBuffer.Normal",
        "GBuffer.Emissive", "GBuffer.Bitmask" };

    RGHandle targets[kRenderTargetCount]{};
    for (uint32_t i = 0; i < kRenderTargetCount; ++i)
    {
        RGTextureDesc desc{};
        desc.width = context.width;
        desc.height = context.height;
        desc.format = GetRenderTargetFormat(i);
        desc.allowRenderTarget = true;
        desc.name = kNames[i];
        targets[i] = graph.CreateTexture(desc);
    }

    RGTextureDesc depthDesc{};
    depthDesc.width = context.width;
    depthDesc.height = context.height;
    depthDesc.format = kDepthFormat;
    depthDesc.allowDepthStencil = true;
    depthDesc.name = "GBuffer.Depth";
    const RGHandle depth = graph.CreateTexture(depthDesc);

    m_outputs.diffuse = targets[0];
    m_outputs.metalRough = targets[1];
    m_outputs.normal = targets[2];
    m_outputs.emissive = targets[3];
    m_outputs.bitmask = targets[4];
    m_outputs.depth = depth;

    std::vector<EnhancedRenderGraph::RGPassUsage> usages;
    usages.reserve(kRenderTargetCount + 1);
    for (uint32_t i = 0; i < kRenderTargetCount; ++i)
    {
        usages.push_back({ targets[i], RGResourceState::RenderTarget });
    }
    usages.push_back({ depth, RGResourceState::DepthWrite });

    // 소비자가 없을 때만 뿌리로 표시해 살려 둔다(SetKeepAlive).
    // Deferred가 붙으면 그쪽이 읽으므로 표시 없이도 살아남아야 한다.
    const bool keepAlive = m_keepAlive;

    // 쪼갤 수 있는 패스로 선언한다.
    //
    // 이 패스가 기록 시간의 대부분을 차지한다 — 패스 단위 병렬화만으로는
    // '가장 무거운 패스'보다 빨라질 수 없어 1.25배에서 평평했다.
    //
    // 조각 상한은 워커 상한과 맞춘다. 그보다 잘게 쪼개도 같은 워커가 연달아
    // 맡게 되고, 그러면 조각마다 상태를 다시 거는 비용만 늘어난다.
    graph.AddSplitPass(GetName(), usages,
        [this, &context, targets, depth](const EnhancedRenderGraph::ExecuteContext& executeContext,
            uint32_t slice, uint32_t sliceCount)
        {
            auto* commandList = executeContext.commandList;
            auto* device = context.resources->GetDevice();

            // 뷰는 매 프레임 만든다. 그래프가 리소스를 프레임마다 다르게 줄 수
            // 있으므로(컬링·앨리어싱) 캐시하면 어긋난다.
            D3D12_CPU_DESCRIPTOR_HANDLE rtvHandles[kRenderTargetCount]{};
            const auto rtvBase = m_rtvHeap->GetCPUDescriptorHandleForHeapStart();
            for (uint32_t i = 0; i < kRenderTargetCount; ++i)
            {
                rtvHandles[i] = rtvBase;
                rtvHandles[i].ptr += static_cast<SIZE_T>(i) * m_rtvIncrement;
                device->CreateRenderTargetView(executeContext.Resolve(targets[i]), nullptr,
                    rtvHandles[i]);
            }

            const auto dsvHandle = m_dsvHeap->GetCPUDescriptorHandleForHeapStart();
            D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc{};
            dsvDesc.Format = kDepthFormat;
            dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
            device->CreateDepthStencilView(executeContext.Resolve(depth), &dsvDesc, dsvHandle);

            const D3D12_VIEWPORT viewport{ 0.f, 0.f,
                static_cast<float>(context.width), static_cast<float>(context.height), 0.f, 1.f };
            const D3D12_RECT scissor{ 0, 0,
                static_cast<LONG>(context.width), static_cast<LONG>(context.height) };
            commandList->RSSetViewports(1, &viewport);
            commandList->RSSetScissorRects(1, &scissor);

            commandList->OMSetRenderTargets(kRenderTargetCount, rtvHandles, FALSE, &dsvHandle);

            // 클리어 값은 0으로 둔다. 그려진 곳과 안 그려진 곳이 값으로 구분되어야
            // 픽셀 검증이 '다섯 타깃 각각이 실제로 기록됐는가'를 볼 수 있다.
            //
            // 클리어는 첫 조각에서만 한다. 조각들은 순서대로 실행되므로 뒤
            // 조각이 또 지우면 앞 조각이 그린 것이 사라진다.
            constexpr float kZero[4] = { 0.f, 0.f, 0.f, 0.f };
            if (0 == slice)
            {
                for (uint32_t i = 0; i < kRenderTargetCount; ++i)
                {
                    commandList->ClearRenderTargetView(rtvHandles[i], kZero, 0, nullptr);
                }
                commandList->ClearDepthStencilView(dsvHandle,
                    D3D12_CLEAR_FLAG_DEPTH, 1.f, 0, 0, nullptr);
            }

            commandList->SetGraphicsRootSignature(m_rootSignature);
            commandList->SetPipelineState(m_pso);
            commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

            // 프레임 상수는 한 번만 올린다. 드로우마다 올리면 같은 값을 수백 번
            // 복사하는 꼴이고, 그건 CE 단계를 늘리는 방향이다.
            //
            // HLSL은 행 우선으로 읽으므로 전치해서 넣는다.
            const Mathf::Matrix viewProjection = XMMatrixTranspose(m_frameViewProjection);
            const auto frameConstants = context.resources->GetUploadRing().Allocate(
                sizeof(Mathf::Matrix), DX12UploadRing::kConstantBufferAlignment);
            if (!frameConstants.IsValid()) return;
            memcpy(frameConstants.cpuAddress, &viewProjection, sizeof(viewProjection));
            commandList->SetGraphicsRootConstantBufferView(0, frameConstants.gpuAddress);

            // 그릴 것이 없으면 클리어만 하고 끝난다 — 빈 씬도 정상 경로다.
            if (nullptr == context.draws) return;

            // 힙 바인딩은 드로우 밖에서 한 번만. 드로우마다 바꾸면 그 자체가 비싸다.
            ID3D12DescriptorHeap* heaps[] = {
                context.resources->GetDescriptorRing().GetHeap(),
                context.resources->GetSamplerHeap().GetHeap() };
            commandList->SetDescriptorHeaps(2, heaps);
            commandList->SetGraphicsRootDescriptorTable(3, m_sampler);

            // 자기 몫의 배치만 그린다.
            //
            // 단위가 드로우가 아니라 배치다. 같은 메시·재질을 쓰는 드로우들이
            // 인스턴스 하나로 묶여 DrawIndexedInstanced 한 번에 나간다 —
            // 드로우마다 상수를 올리고 SRV 테이블을 만들던 것이 배치마다 한 번이 된다.
            //
            // 조각들은 선언 순서대로 실행되므로 결과는 통째로 그린 것과 같다.
            const size_t batchCount = m_batches.size();
            const size_t sliceBegin = batchCount * slice / sliceCount;
            const size_t sliceEnd = batchCount * (slice + 1) / sliceCount;

            for (size_t batchIndex = sliceBegin; batchIndex < sliceEnd; ++batchIndex)
            {
                const DrawBatch& batch = m_batches[batchIndex];
                if (0 == batch.instanceCount) continue;

                const auto mesh = m_drawGeometry.find(batch.mesh);
                if (mesh == m_drawGeometry.end() || !mesh->second.IsValid()) continue;

                // 이 배치의 인스턴스를 업로드 링에 연속으로 올린다.
                const uint64_t instanceBytes =
                    sizeof(InstanceData) * static_cast<uint64_t>(batch.instanceCount);
                const auto instanceBuffer = context.resources->GetUploadRing().Allocate(
                    instanceBytes, sizeof(InstanceData));
                if (!instanceBuffer.IsValid()) break;   // 구간이 찼다 — 남은 배치는 다음 프레임

                memcpy(instanceBuffer.cpuAddress, &m_instances[batch.firstInstance],
                    static_cast<size_t>(instanceBytes));
                commandList->SetGraphicsRootShaderResourceView(1, instanceBuffer.gpuAddress);

                // 재질 텍스처 넷을 연속으로 잘라 테이블 하나로 묶는다.
                // PrepareFrame이 올려 둔 것을 쓴다 — 기록 중에는 만들지 않는다.
                const auto textures = m_drawTextures.find(batch.material);
                if (textures != m_drawTextures.end())
                {
                    const auto srvRange = context.resources->GetDescriptorRing().Allocate(4);
                    if (!srvRange.IsValid()) break;

                    for (uint32_t i = 0; i < 4; ++i)
                    {
                        auto* resource = textures->second.resources[i];
                        if (nullptr == resource) continue;

                        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
                        srvDesc.Format = textures->second.formats[i];
                        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
                        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
                        srvDesc.Texture2D.MipLevels = textures->second.mipLevels[i];

                        device->CreateShaderResourceView(resource, &srvDesc, srvRange.CpuAt(i));
                    }

                    commandList->SetGraphicsRootDescriptorTable(2, srvRange.gpu);
                }

                commandList->IASetVertexBuffers(0, 1, &mesh->second.vertexView);
                commandList->IASetIndexBuffer(&mesh->second.indexView);
                commandList->DrawIndexedInstanced(mesh->second.indexCount,
                    batch.instanceCount, 0, 0, 0);
            }
        },
        // 조각 수는 드로우 수로 정한다.
        //
        // ★ 상한만 두고 무조건 쪼갰더니 작은 씬에서 오히려 느려졌다.
        // 실측: 드로우 44에서 1.23배(분할 없음) → 0.71배(6조각)로 뒤집혔다.
        // 조각마다 상태를 다시 걸어야 하기 때문이다 — RTV 5개와 DSV를 만들고
        // 뷰포트·루트 시그니처·PSO·힙을 세우고 프레임 상수를 올린다. 그 비용이
        // 드로우 일곱 개 그리는 것보다 크다.
        //
        // 그래서 조각당 최소 드로우 수를 둔다. 그 아래로는 쪼개지 않는다.
        ComputeSliceCount(),
        keepAlive);
}

uint32_t EnhancedGBufferPass::ComputeSliceCount() const
{
    // 조각당 최소 드로우 수. 실측으로 정했다 — 드로우 44를 6조각으로 나누면
    // 조각당 일곱이고 그때 상태 설정 비용이 이겼다. 176(조각당 29)부터는
    // 분할이 이겼으므로 그 사이 어딘가가 경계다.
    constexpr uint32_t kMinDrawsPerSlice = 32;

    // 기준은 드로우가 아니라 배치다. 인스턴싱으로 묶인 뒤에는 배치 수가
    // 기록 비용을 결정한다 — 드로우 700개가 배치 11개로 묶였다면 쪼갤 것이 없다.
    if (m_lastBatchCount <= kMinDrawsPerSlice) return 1;

    const uint32_t byBatches = m_lastBatchCount / kMinDrawsPerSlice;
    return (std::max)(1u, (std::min)(DX12CommandListPool::kMaxWorkers, byBatches));
}

void EnhancedGBufferPass::Shutdown()
{
    m_drawGeometry.clear();
    m_drawTextures.clear();
    m_rtvHeap.Reset();
    m_dsvHeap.Reset();
    m_pso = nullptr;
    m_rootSignature = nullptr;
}

#endif
