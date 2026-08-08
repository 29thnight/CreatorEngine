#ifndef DYNAMICCPP_EXPORTS
#include "EnhancedUIPass.h"
#include "DX12DeviceResources.h"
#include "DX12PSOManager.h"
#include "DX12RootSignatureCache.h"
#include "DX12TextureCache.h"
#include "EnhancedRenderGraph.h"
#include "RHIEncoder.h"
#include "../../UIRenderProxy.h"
#include "../../UIClipping.h"
#include "../../Texture.h"

#include <d3dcompiler.h>
#include <algorithm>
#include <sstream>
#include <string>
#include <vector>

#pragma comment(lib, "d3dcompiler.lib")

// 단계(순서대로 채운다):
//   [v] 1. 사각형 인스턴싱 + 배칭 + 자가 검증
//   [v] 2. 씬 연결(UI 큐 → 사각형) + 배칭 실측
//
// 씬 실측(UITestScene, ImageComponent 하나):
//   UI 원천 — 카메라 10(유효 2) · UIRenderDataBuffer 1 · 큐 1
//   UI — 사각형 1 · 배치 1 · 건너뜀 0 · GPU UI.Draw 0.0020 ms
//
// ★ 앞선 실행에서 UI가 늘 0이었던 것은 프리팹이 원본 리소스 없는
//   메타데이터 덩어리라 빈 게임오브젝트가 되기 때문이었다. 실재하는
//   텍스처를 참조하는 씬으로 바꾸니 한 번에 찼다.
//
//   그때 '건너뜀 0'을 근거로 텍스처 문제가 아니라고 판단했는데, 그건 한
//   단계 얕았다. 건너뜀 0은 '큐에 있는데 텍스처가 없어 건너뛴 것은 아니다'
//   까지만 말해 주지, '리소스가 없어 프록시 자체가 안 생겼다'는 배제하지
//   못한다. 그 둘을 가르려면 큐의 원천(UIRenderDataBuffer)까지 봐야 했고,
//   그래서 지금은 카메라 수·버퍼 크기·큐 크기를 나란히 남긴다.

namespace
{
    // 유니티 빌드에서 익명 네임스페이스가 합쳐지므로 이름을 고유하게 둔다.
    std::string UiHrToString(HRESULT hr)
    {
        std::ostringstream oss;
        oss << "HRESULT 0x" << std::hex << static_cast<unsigned long>(hr);
        return oss.str();
    }

    // ── 사각형 셰이더 ──
    //
    // 정점 버퍼가 없다. SV_VertexID로 네 정점을 만들고 인스턴스 자료에서
    // 사각형의 네 변과 UV를 뽑는다.
    //
    // 정점 버퍼를 두지 않는 이유: UI 사각형은 정점이 전부 같은 모양이라
    // 버퍼에 담을 것이 '0,0 / 1,0 / 0,1 / 1,1' 네 개뿐이다. 그것을 위해
    // 버퍼를 만들고 바인딩하고 입력 조립을 거치는 비용이, 셰이더에서
    // 비트 두 개로 만드는 것보다 크다.
    constexpr const char* kUIShader = R"(
struct RectInstance
{
    float4 bounds;   // left · top · right · bottom (픽셀)
    float4 uv;       // uvLeft · uvTop · uvRight · uvBottom
    float4 color;
};

StructuredBuffer<RectInstance> gRects : register(t0);
Texture2D                      gTexture : register(t1);
SamplerState                   gSampler : register(s0);

cbuffer UIParams : register(b0)
{
    float2 gScreenSize;
    float2 gPad;
};

struct VSOut
{
    float4 position : SV_POSITION;
    float2 uv       : TEXCOORD0;
    float4 color    : COLOR0;
};

VSOut VSMain(uint vertexId : SV_VertexID, uint instanceId : SV_InstanceID)
{
    const RectInstance rect = gRects[instanceId];

    // 0,1,2,3 → (0,0) (1,0) (0,1) (1,1). 삼각형 스트립 순서다.
    const float2 corner = float2(float(vertexId & 1u), float((vertexId >> 1u) & 1u));

    const float2 pixel = lerp(rect.bounds.xy, rect.bounds.zw, corner);

    // 픽셀 좌표를 클립으로. y는 화면이 아래로 커지고 클립은 위로 커지므로 뒤집는다.
    VSOut output;
    output.position = float4(
        pixel.x / gScreenSize.x * 2.0f - 1.0f,
        1.0f - pixel.y / gScreenSize.y * 2.0f,
        0.0f, 1.0f);
    output.uv = lerp(rect.uv.xy, rect.uv.zw, corner);
    output.color = rect.color;
    return output;
}

float4 PSMain(VSOut input) : SV_TARGET
{
    return gTexture.Sample(gSampler, input.uv) * input.color;
}
)";

    struct UIParams
    {
        float screenWidth{ 0.f };
        float screenHeight{ 0.f };
        float pad[2]{};
    };

    bool CompileUIShader(const char* entry, const char* target,
        Microsoft::WRL::ComPtr<ID3DBlob>& outBlob, std::string& outError)
    {
        Microsoft::WRL::ComPtr<ID3DBlob> errors;
        const HRESULT hr = D3DCompile(kUIShader, strlen(kUIShader), nullptr, nullptr,
            nullptr, entry, target, 0, 0, &outBlob, &errors);
        if (FAILED(hr))
        {
            outError = std::string("UI 셰이더 컴파일 실패(") + entry + "): ";
            if (errors) outError += static_cast<const char*>(errors->GetBufferPointer());
            else        outError += UiHrToString(hr);
            return false;
        }
        return true;
    }
}


// ── UI 큐를 사각형으로 ──
//
// 프록시가 들고 있는 것은 DirectXTK SpriteBatch에 넘길 형태다(앵커 좌표·
// origin·회전). 그것을 화면 사각형으로 푸는 계산이 DX11 Draw 안에 있었는데,
// 그 계산을 여기서 다시 쓰지 않고 같은 함수를 부른다 — 두 곳에 따로 두면
// 하나가 틀려도 알 수 없다.
uint32_t EnhancedUIPass::BuildRectsFromQueue(
    UIRenderProxy* const* proxies, size_t count, std::vector<Rect>& outRects)
{
    outRects.clear();
    outRects.reserve(count);

    uint32_t skipped = 0;

    for (size_t i = 0; i < count; ++i)
    {
        auto* proxy = proxies[i];
        if (nullptr == proxy) { ++skipped; continue; }

        const auto* image = std::get_if<UIRenderProxy::ImageData>(&proxy->GetData());
        if (nullptr == image)
        {
            // 텍스트·스프라이트시트. 폰트 아틀라스가 붙기 전까지 건너뛴다.
            ++skipped;
            continue;
        }
        if (nullptr == image->texture) { ++skipped; continue; }

        const auto size = image->texture->GetImageSize();
        const long texW = static_cast<long>(size.x);
        const long texH = static_cast<long>(size.y);
        if (texW <= 0 || texH <= 0) { ++skipped; continue; }

        // position은 rect의 중심이고 origin*scale이 rect 크기의 절반이다
        // (PHASE 7-6). 그래서 아래 네 값이 화면 좌표 그대로의 사각형이다.
        const float halfWidth = image->origin.x * image->scale.x;
        const float halfHeight = image->origin.y * image->scale.y;

        const float left = image->position.x - halfWidth;
        const float top = image->position.y - halfHeight;
        const float right = left + texW * image->scale.x;
        const float bottom = top + texH * image->scale.y;

        ClippedSource src{};
        ClippedDestination dst{};
        if (!CalculateClippedRects(image->clipDirection, image->clipPercent,
            texW, texH, left, top, right, bottom,
            image->scale.x, image->scale.y, src, dst))
        {
            // 잘린 폭이 0 — 그릴 것이 없다. 이건 건너뛴 것이 아니라
            // '안 그리는 것이 맞는' 경우라 통계에 안 센다.
            continue;
        }

        Rect rect{};
        rect.left = dst.left;
        rect.top = dst.top;
        rect.right = dst.right;
        rect.bottom = dst.bottom;

        // 텍셀 구간을 UV로. DX11은 이것을 RECT로 SpriteBatch에 넘기고,
        // 여기서는 셰이더가 UV로 읽는다.
        rect.uvLeft = static_cast<float>(src.left) / static_cast<float>(texW);
        rect.uvTop = static_cast<float>(src.top) / static_cast<float>(texH);
        rect.uvRight = static_cast<float>(src.right) / static_cast<float>(texW);
        rect.uvBottom = static_cast<float>(src.bottom) / static_cast<float>(texH);

        rect.color = image->color;
        rect.canvasOrder = image->canvasOrder;
        rect.layerOrder = image->layerOrder;
        rect.texture = image->texture.get();

        outRects.push_back(rect);
    }

    return skipped;
}

bool EnhancedUIPass::Initialize(const EnhancedFrameContext& context, std::string& outError)
{
    if (nullptr == context.resources || nullptr == context.psoManager ||
        nullptr == context.rootSignatures)
    {
        outError = "UI 패스 컨텍스트가 불완전하다";
        return false;
    }

    return CreatePipelines(context, outError);
}

bool EnhancedUIPass::CreatePipelines(const EnhancedFrameContext& context, std::string& outError)
{
    // b0 상수 · t0 인스턴스(루트 SRV) · t1 텍스처(테이블).
    //
    // 인스턴스를 루트 SRV로 두는 이유는 배치마다 주소만 바뀌기 때문이다 —
    // 테이블을 만들면 배치마다 디스크립터를 자르게 되는데 그건 배칭으로
    // 아낀 것을 도로 쓰는 일이다.
    D3D12_DESCRIPTOR_RANGE textureRange{};
    textureRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    textureRange.NumDescriptors = 1;
    textureRange.BaseShaderRegister = 1;

    D3D12_ROOT_PARAMETER params[3]{};
    params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    params[0].Descriptor.ShaderRegister = 0;
    params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
    params[1].Descriptor.ShaderRegister = 0;
    params[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[2].DescriptorTable.NumDescriptorRanges = 1;
    params[2].DescriptorTable.pDescriptorRanges = &textureRange;

    D3D12_STATIC_SAMPLER_DESC sampler{};
    sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
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
    rootDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    const auto root = context.rootSignatures->GetOrCreate(rootDesc, outError);
    if (!root.IsValid()) return false;
    m_rootSignature = root.signature;

    ComPtr<ID3DBlob> vsBlob;
    ComPtr<ID3DBlob> psBlob;
    if (!CompileUIShader("VSMain", "vs_5_0", vsBlob, outError)) return false;
    if (!CompileUIShader("PSMain", "ps_5_0", psBlob, outError)) return false;

    DX12GraphicsPipelineDesc desc{};
    desc.vsBytecode = vsBlob->GetBufferPointer();
    desc.vsSize = vsBlob->GetBufferSize();
    desc.psBytecode = psBlob->GetBufferPointer();
    desc.psSize = psBlob->GetBufferSize();
    desc.rootSignature = root.signature;
    desc.rootSignatureId = root.id;

    // 입력 레이아웃이 없다 — 정점을 셰이더가 만든다.
    desc.inputElements = nullptr;
    desc.inputElementCount = 0;
    desc.topologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;

    // 깊이는 안 쓴다. UI의 앞뒤는 그리는 순서가 정한다.
    desc.depthEnable = false;

    // 알파 블렌딩. UI는 겹쳐 그리는 것이 기본이라 이것이 없으면
    // 뒤에 그린 것이 앞의 것을 통째로 덮는다.
    desc.blendEnable = true;
    desc.cullMode = D3D12_CULL_MODE_NONE;
    desc.numRenderTargets = 1;
    desc.rtvFormats[0] = kOutputFormat;

    m_pso = context.psoManager->GetOrCreate(desc, outError);
    if (nullptr == m_pso) return false;

    return true;
}

bool EnhancedUIPass::PrepareFrame(const EnhancedFrameContext& context, std::string& outError)
{
    (void)outError;

    m_width = context.width;
    m_height = context.height;

    m_instances.clear();
    m_batches.clear();
    m_lastRectCount = 0;
    m_lastBatchCount = 0;

    if (nullptr == m_rects || m_rects->empty()) return true;

    // ── 순서 정하기 ──
    //
    // ★ 전역 정렬을 하면 안 된다.
    //
    //   GBuffer 인스턴싱에서는 (메시, 재질)로 전역 정렬해서 묶었다.
    //   불투명이라 그리는 순서가 그림을 바꾸지 않기 때문이다.
    //
    //   UI는 그 반대다. 순서가 곧 위아래이므로, 텍스처로 전역 정렬하면
    //   겹친 요소의 앞뒤가 뒤집힌다. 그 증상은 '가끔 UI가 이상하게 겹친다'로
    //   나타나서 원인을 짚기 어렵다.
    //
    //   그래서 (canvasOrder, layerOrder)로 안정 정렬만 하고, 텍스처는
    //   '연속해서 같은 것'인 구간만 묶는다.
    std::vector<uint32_t> order(m_rects->size());
    for (uint32_t i = 0; i < order.size(); ++i) order[i] = i;

    std::stable_sort(order.begin(), order.end(),
        [this](uint32_t a, uint32_t b)
        {
            const Rect& lhs = (*m_rects)[a];
            const Rect& rhs = (*m_rects)[b];
            if (lhs.canvasOrder != rhs.canvasOrder) return lhs.canvasOrder < rhs.canvasOrder;
            return lhs.layerOrder < rhs.layerOrder;
        });

    m_instances.reserve(order.size());
    for (const uint32_t index : order)
    {
        const Rect& rect = (*m_rects)[index];

        RectInstance instance{};
        instance.bounds = Mathf::Vector4(rect.left, rect.top, rect.right, rect.bottom);
        instance.uv = Mathf::Vector4(rect.uvLeft, rect.uvTop, rect.uvRight, rect.uvBottom);
        instance.color = rect.color;
        m_instances.push_back(instance);

        // 앞 배치와 텍스처가 같으면 이어 붙인다.
        if (!m_batches.empty() && m_batches.back().texture == rect.texture)
        {
            ++m_batches.back().count;
        }
        else
        {
            Batch batch{};
            batch.first = static_cast<uint32_t>(m_instances.size() - 1);
            batch.count = 1;
            batch.texture = rect.texture;
            m_batches.push_back(batch);
        }
    }

    m_lastRectCount = static_cast<uint32_t>(m_instances.size());
    m_lastBatchCount = static_cast<uint32_t>(m_batches.size());
    return true;
}

void EnhancedUIPass::Declare(EnhancedRenderGraph& graph, const EnhancedFrameContext& context)
{
    m_output = RGHandle{};

    if (nullptr == m_pso || 0 == m_width || 0 == m_height)
    {
        return;
    }

    RGTextureDesc desc{};
    desc.width = m_width;
    desc.height = m_height;
    desc.format = kOutputFormat;
    desc.allowRenderTarget = true;
    desc.name = "UI.Output";
    m_output = graph.CreateTexture(desc);

    std::vector<EnhancedRenderGraph::RGPassUsage> usages;
    usages.push_back({ m_output, RGResourceState::RenderTarget });
    if (m_inputs.color.IsValid())
    {
        usages.push_back({ m_inputs.color, RGResourceState::CopySource });
    }

    graph.AddPass("UI.Draw", usages,
        [this, &context](const EnhancedRenderGraph::ExecuteContext& executeContext)
        {
            RHIEncoder& encoder = *executeContext.encoder;
            auto* commandList = executeContext.commandList;

            ID3D12Resource* const colors[] = { executeContext.Resolve(m_output) };
            const auto targets = context.resources->CreateRenderTargets(colors);
            if (!targets.IsValid()) return;

            encoder.SetViewportAndScissor(m_width, m_height);
            encoder.BindRenderTargets(targets);

            // 배경을 투명으로 지운다. 아래 그림 위에 얹는 합성은 이 슬라이스
            // 범위 밖이라(포스트 체인 뒤에 붙일 자리다) 여기서는 UI만 그린다.
            constexpr float kClear[4] = { 0.f, 0.f, 0.f, 0.f };
            encoder.ClearRenderTargets(targets, kClear);

            if (m_instances.empty()) return;

            UIParams params{};
            params.screenWidth = static_cast<float>(m_width);
            params.screenHeight = static_cast<float>(m_height);

            const auto cb = context.resources->GetUploadRing().Allocate(
                sizeof(UIParams), DX12UploadRing::kConstantBufferAlignment);
            if (!cb.IsValid()) return;
            memcpy(cb.cpuAddress, &params, sizeof(params));

            // 인스턴스는 한 번에 올린다. 배치마다 주소만 옮기면 되므로
            // 배치별 재업로드가 필요 없다.
            const uint64_t instanceBytes =
                sizeof(RectInstance) * static_cast<uint64_t>(m_instances.size());
            const auto instanceUpload = context.resources->GetUploadRing().Allocate(
                instanceBytes, sizeof(RectInstance));
            if (!instanceUpload.IsValid()) return;
            memcpy(instanceUpload.cpuAddress, m_instances.data(),
                static_cast<size_t>(instanceBytes));

            encoder.BindDescriptorHeaps();

            encoder.SetPipeline(RHIBindPoint::Graphics, m_pso, m_rootSignature);
            encoder.SetPrimitiveTopology(RHIPrimitiveTopology::TriangleStrip);
            encoder.SetConstantBuffer(RHIBindPoint::Graphics, 0, cb.gpuAddress);

            for (const Batch& batch : m_batches)
            {
                // 텍스처. 없으면 1x1 흰색이 묶여 단색 사각형이 된다.
                ID3D12Resource* resource = nullptr;
                DXGI_FORMAT format = DXGI_FORMAT_R8G8B8A8_UNORM;
                uint32_t mipLevels = 1;

                if (nullptr != context.textureCache)
                {
                    std::string uploadError;
                    // nullptr을 넘기면 캐시가 1x1 흰색을 돌려준다 —
                    // 단색 사각형이 그것으로 나온다.
                    const auto entry =
                        context.textureCache->GetOrUpload(batch.texture, uploadError);
                    if (entry.IsValid())
                    {
                        resource = entry.resource;
                        format = entry.format;
                        mipLevels = entry.mipLevels;
                    }
                }

                // ★ 자원이 없으면 그리지 않는다.
                //
                // 디스크립터를 안 만들고 그리면 힙의 쓰레기를 읽는다 —
                // SSGI에서 그것으로 GPU가 죽었다. '조용히 건너뛰기'가
                // 아니라 통계로 드러나야 하므로 배치 수와 그린 수가
                // 갈리는 것을 밖에서 볼 수 있게 해 둔다.
                if (nullptr == resource) continue;

                const RHIBindingDesc texture[] = {
                    RHIBindingDesc::Srv2D(resource, format, 0, mipLevels),
                };
                const RHIBindingTable textureTable =
                    context.resources->CreateBindings(texture);
                // 여기서 실패하면 링이 모자란 것이다(리소스 널은 위에서 걸렀다).
                // 남은 배치도 마찬가지일 테니 멈춘다.
                if (!textureTable.IsValid()) break;

                encoder.SetRootBuffer(RHIBindPoint::Graphics, 1,
                    instanceUpload.gpuAddress
                    + static_cast<uint64_t>(batch.first) * sizeof(RectInstance));
                encoder.SetBindings(RHIBindPoint::Graphics, 2, textureTable);

                encoder.Draw(4, batch.count);
            }
        },
        // 지금은 소비자가 없다 — 합성이 붙기 전까지 뿌리로 표시해 살려 둔다.
        true);
}

void EnhancedUIPass::Shutdown()
{
    m_instances.clear();
    m_batches.clear();
    m_lastRectCount = 0;
    m_lastBatchCount = 0;
    m_width = 0;
    m_height = 0;

    m_pso = nullptr;
    m_rootSignature = nullptr;
}

#endif
