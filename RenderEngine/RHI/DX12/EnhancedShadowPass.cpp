#ifndef DYNAMICCPP_EXPORTS
#include "EnhancedShadowPass.h"
#include "DX12DeviceResources.h"
#include "DX12PSOManager.h"
#include "DX12RootSignatureCache.h"
#include "../../Mesh.h"

#include <d3dcompiler.h>
#include <DirectXCollision.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <sstream>

#pragma comment(lib, "d3dcompiler.lib")

namespace
{
    // 유니티 빌드에서 익명 네임스페이스가 파일 간 합쳐지므로 이름을 고유하게 둔다.
    constexpr const char* kShadowShader = R"(
cbuffer ShadowConstants : register(b0)
{
    float4x4 gLightViewProjection;
    float4x4 gWorld;
};

struct VSIn
{
    float3 position : POSITION;
};

// 깊이만 쓰므로 픽셀 셰이더가 없다. 위치 외에는 아무것도 보간하지 않는다 —
// 그림자 패스는 드로우 수가 많아 정점 처리 비용이 그대로 프레임에 얹힌다.
float4 VSMain(VSIn input) : SV_POSITION
{
    const float4 worldPosition = mul(float4(input.position, 1.0f), gWorld);
    return mul(worldPosition, gLightViewProjection);
}
)";

    bool CompileShadowShader(const char* entry, const char* target,
        Microsoft::WRL::ComPtr<ID3DBlob>& outBlob, std::string& outError)
    {
        Microsoft::WRL::ComPtr<ID3DBlob> errors;
        const HRESULT hr = D3DCompile(kShadowShader, strlen(kShadowShader), nullptr,
            nullptr, nullptr, entry, target, 0, 0, &outBlob, &errors);
        if (FAILED(hr))
        {
            outError = std::string("그림자 셰이더 컴파일 실패(") + entry + "): ";
            if (errors) outError += static_cast<const char*>(errors->GetBufferPointer());
            return false;
        }
        return true;
    }
}

bool EnhancedShadowPass::CreatePipeline(const EnhancedFrameContext& context, std::string& outError)
{
    ComPtr<ID3DBlob> vsBlob;
    if (!CompileShadowShader("VSMain", "vs_5_0", vsBlob, outError)) return false;

    D3D12_ROOT_PARAMETER param{};
    param.ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    param.Descriptor.ShaderRegister = 0;
    param.ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;

    D3D12_ROOT_SIGNATURE_DESC rootDesc{};
    rootDesc.NumParameters = 1;
    rootDesc.pParameters = &param;
    rootDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    const auto root = context.rootSignatures->GetOrCreate(rootDesc, outError);
    if (!root.IsValid()) return false;
    m_rootSignature = root.signature;

    // 위치만 읽는다. 정점 구조체는 같지만 나머지 요소를 선언하지 않으면
    // 입력 조립이 그것들을 가져오지 않는다 — 대역폭이 그만큼 준다.
    static const D3D12_INPUT_ELEMENT_DESC kInputElements[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,
          D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    };

    DX12GraphicsPipelineDesc desc{};
    desc.inputElements = kInputElements;
    desc.inputElementCount = _countof(kInputElements);
    desc.vsBytecode = vsBlob->GetBufferPointer();
    desc.vsSize = vsBlob->GetBufferSize();
    desc.psBytecode = nullptr;   // 깊이 전용
    desc.psSize = 0;
    desc.rootSignature = root.signature;
    desc.rootSignatureId = root.id;
    desc.depthEnable = true;
    // 컬링을 끈다. 앞면만 그리면 닫히지 않은 메시(평면·판때기)가 그림자를
    // 아예 못 드리우고, 뒷면만 그리면 그런 메시가 그림자를 두 번 드리운다.
    // 여드름은 편향으로 잡는 편이 씬 종류를 덜 탄다.
    desc.cullMode = D3D12_CULL_MODE_NONE;
    desc.numRenderTargets = 0;
    desc.dsvFormat = kShadowFormat;

    m_pso = context.psoManager->GetOrCreate(desc, outError);
    return nullptr != m_pso;
}

bool EnhancedShadowPass::Initialize(const EnhancedFrameContext& context, std::string& outError)
{
    if (nullptr == context.resources || nullptr == context.psoManager ||
        nullptr == context.rootSignatures)
    {
        outError = "그림자 패스 컨텍스트가 불완전하다";
        return false;
    }

    D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc{};
    dsvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
    dsvHeapDesc.NumDescriptors = 1;
    if (FAILED(context.resources->GetDevice()->CreateDescriptorHeap(&dsvHeapDesc,
        IID_PPV_ARGS(&m_dsvHeap))))
    {
        outError = "그림자 DSV 힙 생성 실패";
        return false;
    }

    return CreatePipeline(context, outError);
}

void EnhancedShadowPass::ComputeLightMatrix(const EnhancedFrameContext& context)
{
    m_hasDirectionalLight = false;
    m_lightViewProjection = XMMatrixIdentity();

    if (nullptr == context.camera || nullptr == context.lights) return;

    // 첫 방향광을 쓴다. 여러 방향광의 그림자는 맵이 그만큼 필요하고, 실제로
    // 둘 이상 쓰는 씬이 나왔을 때 정하는 것이 맞다.
    for (const auto& light : *context.lights)
    {
        if (0 == static_cast<uint32_t>(light.position.w))
        {
            m_lightDirection = light.direction;
            m_hasDirectionalLight = true;
            break;
        }
    }
    if (!m_hasDirectionalLight) return;

    // 카메라 프러스텀의 앞쪽 일부를 감싸는 상자를 만든다. 전체를 덮으면 먼 곳까지
    // 한 장에 들어가 해상도가 낮아진다 — 캐스케이드가 필요한 이유이고, 단일
    // 캐스케이드에서는 가까운 구간만 덮는 것이 실용적이다.
    constexpr float kCoverage = 0.3f;

    const DirectX::BoundingFrustum frustum(context.camera->projection);
    const Mathf::xMatrix inverseView = context.camera->inverseView;

    const float nearDistance = context.camera->nearPlane;
    const float farDistance = context.camera->nearPlane
        + (context.camera->farPlane - context.camera->nearPlane) * kCoverage;

    std::array<Mathf::Vector3, 8> corners{};
    const float slopes[4][2] = {
        { frustum.RightSlope, frustum.TopSlope },
        { frustum.RightSlope, frustum.BottomSlope },
        { frustum.LeftSlope,  frustum.TopSlope },
        { frustum.LeftSlope,  frustum.BottomSlope },
    };
    for (int i = 0; i < 4; ++i)
    {
        corners[i] = Mathf::Vector3::Transform(
            { slopes[i][0] * nearDistance, slopes[i][1] * nearDistance, nearDistance }, inverseView);
        corners[i + 4] = Mathf::Vector3::Transform(
            { slopes[i][0] * farDistance, slopes[i][1] * farDistance, farDistance }, inverseView);
    }

    // 경계 구를 쓴다. 축 정렬 상자를 쓰면 카메라가 회전할 때 상자 크기가 출렁여
    // 그림자 가장자리가 떨린다 — 구는 회전에 불변이다.
    Mathf::Vector3 center = Mathf::Vector3::Zero;
    for (const auto& corner : corners) center += corner;
    center /= 8.f;

    float radius = 0.f;
    for (const auto& corner : corners)
    {
        radius = (std::max)(radius, Mathf::Vector3::Distance(center, corner));
    }

    // 텍셀 단위로 양자화한다. 이게 없으면 카메라가 조금만 움직여도 그림자
    // 가장자리가 지글거린다(shadow shimmering).
    const float texelsPerUnit = static_cast<float>(kShadowMapSize) / (radius * 2.f);
    center.x = std::floor(center.x * texelsPerUnit) / texelsPerUnit;
    center.y = std::floor(center.y * texelsPerUnit) / texelsPerUnit;
    center.z = std::floor(center.z * texelsPerUnit) / texelsPerUnit;

    Mathf::Vector3 direction{ m_lightDirection.x, m_lightDirection.y, m_lightDirection.z };
    if (direction.LengthSquared() < 1e-6f) direction = Mathf::Vector3(0.f, -1.f, 0.f);
    direction.Normalize();

    // 광원을 구 밖으로 충분히 물린다. 가까우면 상자 밖의 그림자 드리우개가 잘린다.
    const float backOff = radius * 2.f;
    const Mathf::xVector lightPosition = Mathf::Vector3(center - direction * backOff);

    // 광원이 정확히 위나 아래를 볼 때 up이 평행해지는 것을 피한다.
    const Mathf::xVector up = (std::fabs(direction.y) > 0.99f)
        ? Mathf::xVector{ 0.f, 0.f, 1.f, 0.f }
        : Mathf::xVector{ 0.f, 1.f, 0.f, 0.f };

    const Mathf::xMatrix lightView = XMMatrixLookAtLH(lightPosition, center, up);
    const Mathf::xMatrix lightProjection = XMMatrixOrthographicOffCenterLH(
        -radius, radius, -radius, radius, 0.f, backOff + radius * 2.f);

    m_lightViewProjection = XMMatrixMultiply(lightView, lightProjection);
}

bool EnhancedShadowPass::PrepareFrame(const EnhancedFrameContext& context, std::string& outError)
{
    m_drawGeometry.clear();
    m_lastDrawCount = 0;

    ComputeLightMatrix(context);

    if (nullptr == context.draws || nullptr == context.meshCache) return true;

    // GBuffer가 이미 올려 둔 것을 캐시 히트로 받는다 — 같은 메시를 두 번
    // 올리지 않는다는 것이 캐시의 요점이다.
    for (const auto& draw : *context.draws)
    {
        if (nullptr == draw.mesh) continue;
        if (m_drawGeometry.find(draw.mesh) != m_drawGeometry.end()) continue;

        std::string uploadError;
        const auto entry = context.meshCache->GetOrUpload(draw.mesh, uploadError);
        if (!entry.IsValid())
        {
            if (!uploadError.empty()) outError = uploadError;
            continue;
        }

        m_drawGeometry.emplace(draw.mesh, entry);
        ++m_lastDrawCount;
    }

    return true;
}

void EnhancedShadowPass::Declare(EnhancedRenderGraph& graph, const EnhancedFrameContext& context)
{
    RGTextureDesc desc{};
    desc.width = kShadowMapSize;
    desc.height = kShadowMapSize;
    desc.format = kShadowFormat;
    desc.allowDepthStencil = true;
    desc.name = "Shadow.Cascade0";
    m_shadowMap = graph.CreateTexture(desc);

    // 방향광이 없으면 그리지 않는다. 다만 리소스는 선언해 두어야 Deferred가
    // 읽을 것이 있다 — 클리어만 된 맵은 '그림자 없음'과 같은 뜻이다.
    graph.AddPass(GetName(), { { m_shadowMap, RGResourceState::DepthWrite } },
        [this, &context](const EnhancedRenderGraph::ExecuteContext& executeContext)
        {
            auto* commandList = executeContext.commandList;
            auto* device = context.resources->GetDevice();

            const auto dsv = m_dsvHeap->GetCPUDescriptorHandleForHeapStart();
            D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc{};
            dsvDesc.Format = kShadowFormat;
            dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
            device->CreateDepthStencilView(executeContext.Resolve(m_shadowMap), &dsvDesc, dsv);

            const D3D12_VIEWPORT viewport{ 0.f, 0.f,
                static_cast<float>(kShadowMapSize), static_cast<float>(kShadowMapSize), 0.f, 1.f };
            const D3D12_RECT scissor{ 0, 0,
                static_cast<LONG>(kShadowMapSize), static_cast<LONG>(kShadowMapSize) };
            commandList->RSSetViewports(1, &viewport);
            commandList->RSSetScissorRects(1, &scissor);

            // 렌더 타깃 없이 깊이만 묶는다.
            commandList->OMSetRenderTargets(0, nullptr, FALSE, &dsv);
            commandList->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH, 1.f, 0, 0, nullptr);

            if (!m_hasDirectionalLight || nullptr == context.draws) return;

            commandList->SetGraphicsRootSignature(m_rootSignature);
            commandList->SetPipelineState(m_pso);
            commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

            for (const auto& draw : *context.draws)
            {
                const auto mesh = m_drawGeometry.find(draw.mesh);
                if (mesh == m_drawGeometry.end() || !mesh->second.IsValid()) continue;

                ShadowConstants constants{};
                constants.lightViewProjection = XMMatrixTranspose(m_lightViewProjection);
                constants.world = XMMatrixTranspose(draw.worldMatrix);

                const auto allocation = context.resources->GetUploadRing().Allocate(
                    sizeof(ShadowConstants), DX12UploadRing::kConstantBufferAlignment);
                if (!allocation.IsValid()) break;

                memcpy(allocation.cpuAddress, &constants, sizeof(constants));
                commandList->SetGraphicsRootConstantBufferView(0, allocation.gpuAddress);

                commandList->IASetVertexBuffers(0, 1, &mesh->second.vertexView);
                commandList->IASetIndexBuffer(&mesh->second.indexView);
                commandList->DrawIndexedInstanced(mesh->second.indexCount, 1, 0, 0, 0);
            }
        },
        // 그림자 맵을 읽는 것은 Deferred다. 뿌리로 표시하지 않아도 컬링이
        // 살려야 하고, 그것이 3-5 컬링의 또 한 번의 확인이다.
        false);
}

void EnhancedShadowPass::Shutdown()
{
    m_drawGeometry.clear();
    m_dsvHeap.Reset();
    m_pso = nullptr;
    m_rootSignature = nullptr;
}

#endif
