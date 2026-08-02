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

    auto* device = context.resources->GetDevice();

    // 캐스케이드마다 DSV가 하나씩 필요하다. 배열 텍스처를 슬라이스 단위로
    // 묶어 그리기 때문이다(SRV는 배열 전체 하나로 충분하다).
    D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc{};
    dsvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
    dsvHeapDesc.NumDescriptors = kCascadeCount;
    if (FAILED(device->CreateDescriptorHeap(&dsvHeapDesc, IID_PPV_ARGS(&m_dsvHeap))))
    {
        outError = "그림자 DSV 힙 생성 실패";
        return false;
    }
    m_dsvIncrement = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);

    return CreatePipeline(context, outError);
}

void EnhancedShadowPass::ComputeCascades(const EnhancedFrameContext& context)
{
    m_hasDirectionalLight = false;
    m_shadowData = EnhancedShadowData{};
    for (auto& cascade : m_cascades) cascade = Cascade{};

    if (nullptr == context.camera || nullptr == context.lights) return;

    // 첫 방향광을 쓴다. 여러 방향광의 그림자는 맵이 그만큼 필요하고, 실제로
    // 둘 이상 쓰는 씬이 나왔을 때 정하는 것이 맞다.
    for (const auto& light : *context.lights)
    {
        if (0 == static_cast<uint32_t>(light.position.w))
        {
            m_lightDirection = Mathf::Vector3{ light.direction.x, light.direction.y,
                light.direction.z };
            m_hasDirectionalLight = true;
            break;
        }
    }
    if (!m_hasDirectionalLight) return;

    if (m_lightDirection.LengthSquared() < 1e-6f) m_lightDirection = { 0.f, -1.f, 0.f };
    m_lightDirection.Normalize();

    const DirectX::BoundingFrustum frustum(context.camera->projection);
    const Mathf::xMatrix inverseView = context.camera->inverseView;

    const float nearPlane = context.camera->nearPlane;
    const float farPlane = context.camera->farPlane;

    // ── 분할 지점 ──
    //
    // 로그 분할은 원근 투영에 맞는 이론값이지만 그대로 쓰면 첫 캐스케이드가
    // 지나치게 좁아진다(near가 작을수록 심하다). 균등 분할과 섞는다.
    std::array<float, kCascadeCount + 1> splits{};
    splits[0] = nearPlane;
    splits[kCascadeCount] = farPlane;
    for (uint32_t i = 1; i < kCascadeCount; ++i)
    {
        const float ratio = static_cast<float>(i) / static_cast<float>(kCascadeCount);
        const float logSplit = nearPlane * std::pow(farPlane / (std::max)(nearPlane, 1e-4f), ratio);
        const float uniformSplit = nearPlane + (farPlane - nearPlane) * ratio;
        splits[i] = kSplitLambda * logSplit + (1.f - kSplitLambda) * uniformSplit;
    }

    const float slopes[4][2] = {
        { frustum.RightSlope, frustum.TopSlope },
        { frustum.RightSlope, frustum.BottomSlope },
        { frustum.LeftSlope,  frustum.TopSlope },
        { frustum.LeftSlope,  frustum.BottomSlope },
    };

    for (uint32_t index = 0; index < kCascadeCount; ++index)
    {
        const float sliceNear = splits[index];
        const float sliceFar = splits[index + 1];

        std::array<Mathf::Vector3, 8> corners{};
        for (int i = 0; i < 4; ++i)
        {
            corners[i] = Mathf::Vector3::Transform(
                { slopes[i][0] * sliceNear, slopes[i][1] * sliceNear, sliceNear }, inverseView);
            corners[i + 4] = Mathf::Vector3::Transform(
                { slopes[i][0] * sliceFar, slopes[i][1] * sliceFar, sliceFar }, inverseView);
        }

        // 경계 구를 쓴다. 축 정렬 상자를 쓰면 카메라가 회전할 때 상자 크기가
        // 출렁여 그림자 가장자리가 떨린다 — 구는 회전에 불변이다.
        Mathf::Vector3 center = Mathf::Vector3::Zero;
        for (const auto& corner : corners) center += corner;
        center /= 8.f;

        float radius = 0.f;
        for (const auto& corner : corners)
        {
            radius = (std::max)(radius, Mathf::Vector3::Distance(center, corner));
        }
        // 반지름도 계단으로 만든다. 카메라가 앞뒤로 조금 움직일 때마다 반지름이
        // 미세하게 달라지면 투영 배율이 바뀌고, 그것도 지글거림이 된다.
        radius = std::ceil(radius * 16.f) / 16.f;

        // 중심을 텍셀 단위로 양자화한다. 이게 없으면 카메라가 조금만 움직여도
        // 그림자 가장자리가 지글거린다(shadow shimmering).
        const float texelsPerUnit = static_cast<float>(kShadowMapSize) / (radius * 2.f);
        center.x = std::floor(center.x * texelsPerUnit) / texelsPerUnit;
        center.y = std::floor(center.y * texelsPerUnit) / texelsPerUnit;
        center.z = std::floor(center.z * texelsPerUnit) / texelsPerUnit;

        // 광원을 구 밖으로 충분히 물린다. 가까우면 상자 밖의 그림자 드리우개가 잘린다.
        const float backOff = radius * 2.f;
        const Mathf::xVector lightPosition = Mathf::Vector3(center - m_lightDirection * backOff);

        // 광원이 정확히 위나 아래를 볼 때 up이 평행해지는 것을 피한다.
        const Mathf::xVector up = (std::fabs(m_lightDirection.y) > 0.99f)
            ? Mathf::xVector{ 0.f, 0.f, 1.f, 0.f }
            : Mathf::xVector{ 0.f, 1.f, 0.f, 0.f };

        const Mathf::xMatrix lightView = XMMatrixLookAtLH(lightPosition, center, up);
        const Mathf::xMatrix lightProjection = XMMatrixOrthographicOffCenterLH(
            -radius, radius, -radius, radius, 0.f, backOff + radius * 2.f);

        Cascade& cascade = m_cascades[index];
        cascade.lightViewProjection = XMMatrixMultiply(lightView, lightProjection);
        cascade.center = center;
        cascade.radius = radius;
        cascade.splitDepth = sliceFar;

        m_shadowData.lightViewProjection[index] = cascade.lightViewProjection;
    }

    // 셰이더가 읽는 형태로 옮긴다. 분할 지점과 편향을 float4 하나씩에 담고
    // 있으므로 캐스케이드가 셋을 넘으면 담는 방식부터 바꿔야 한다.
    static_assert(3 == kCascadeCount, "splitDepths·bias가 float4 하나에 셋을 담는다");

    m_shadowData.splitDepths = Mathf::Vector4{ m_cascades[0].splitDepth,
        m_cascades[1].splitDepth, m_cascades[2].splitDepth, 0.f };

    // 먼 캐스케이드는 텍셀 하나가 덮는 월드 범위가 넓다. 같은 편향을 쓰면
    // 그쪽에만 여드름이 남으므로 반지름 비만큼 키운다.
    const float baseRadius = (std::max)(m_cascades[0].radius, 1e-4f);
    m_shadowData.bias = Mathf::Vector4{
        m_baseBias,
        m_baseBias * (m_cascades[1].radius / baseRadius),
        m_baseBias * (m_cascades[2].radius / baseRadius),
        0.f };

    m_shadowData.cameraForward = context.camera->forward;
    m_shadowData.lightDirection = Mathf::Vector4{ m_lightDirection.x, m_lightDirection.y,
        m_lightDirection.z, 0.f };
    m_shadowData.enabled = true;
}

bool EnhancedShadowPass::CastsInto(const Cascade& cascade, const Mathf::Vector3& center,
    float radius) const
{
    // 캐스케이드 경계 구를 광원 방향으로 늘린 원기둥과의 교차 판정이다.
    //
    // 상자(구) 안에 있는지만 보면 안 된다 — 밖에 있어도 광원과 상자 사이에
    // 있으면 상자 안에 그림자를 드리운다. 그래서 광원 방향 성분은 '광원 쪽으로는
    // 무한히' 허용하고, 그 방향에 수직인 거리만 잰다.
    const Mathf::Vector3 offset = center - cascade.center;
    const float along = offset.Dot(m_lightDirection);

    // 상자 뒤쪽(광원 반대편)으로 완전히 벗어난 것은 그림자를 드리울 수 없다.
    if (along > cascade.radius + radius) return false;

    const Mathf::Vector3 perpendicular = offset - m_lightDirection * along;
    return perpendicular.Length() <= cascade.radius + radius;
}

bool EnhancedShadowPass::PrepareFrame(const EnhancedFrameContext& context, std::string& outError)
{
    m_drawGeometry.clear();
    m_lastDrawCount.store(0, std::memory_order_relaxed);
    m_lastCulledCount.store(0, std::memory_order_relaxed);

    ComputeCascades(context);

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

        Geometry geometry{};
        geometry.entry = entry;
        geometry.boundRadius = draw.mesh->GetBoundingSphere().Radius;
        m_drawGeometry.emplace(draw.mesh, geometry);
    }

    // 조각 수를 정하는 근거. 컬링 전 후보라 상한이지만, '몇 조각으로 나눌까'에는
    // 그것으로 충분하다 — 정확한 수는 그려 봐야 알고, 그때는 이미 늦다.
    m_lastCasterCandidates = static_cast<uint32_t>(context.draws->size());

    return true;
}

void EnhancedShadowPass::Declare(EnhancedRenderGraph& graph, const EnhancedFrameContext& context)
{
    RGTextureDesc desc{};
    desc.width = kShadowMapSize;
    desc.height = kShadowMapSize;
    desc.arraySize = kCascadeCount;
    desc.format = kShadowFormat;
    desc.allowDepthStencil = true;
    desc.name = "Shadow.Cascades";
    m_shadowMap = graph.CreateTexture(desc);

    // 방향광이 없으면 그리지 않는다. 다만 리소스는 선언해 두어야 Deferred가
    // 읽을 것이 있다 — 클리어만 된 맵은 '그림자 없음'과 같은 뜻이다.
    // 쪼갤 수 있는 패스로 선언한다.
    //
    // 병렬 경로의 패스별 GPU 시간을 재 보니 이 패스가 가장 무거웠다
    // (드로우 704에서 Shadow 0.4864 ms · GBuffer 0.2345 ms). 캐스케이드 셋에
    // 드로우를 전부 다시 그리기 때문이다.
    //
    // 조각 경계는 두 층이다. 캐스케이드가 바깥, 드로우 범위가 안쪽 —
    // 캐스케이드가 셋뿐이라 그것만으로는 세 조각이 상한이 된다.
    graph.AddSplitPass(GetName(), { { m_shadowMap, RGResourceState::DepthWrite } },
        [this, &context](const EnhancedRenderGraph::ExecuteContext& executeContext,
            uint32_t slice, uint32_t sliceCount)
        {
            auto* commandList = executeContext.commandList;
            auto* device = context.resources->GetDevice();
            auto* shadowMap = executeContext.Resolve(m_shadowMap);

            const D3D12_VIEWPORT viewport{ 0.f, 0.f,
                static_cast<float>(kShadowMapSize), static_cast<float>(kShadowMapSize), 0.f, 1.f };
            const D3D12_RECT scissor{ 0, 0,
                static_cast<LONG>(kShadowMapSize), static_cast<LONG>(kShadowMapSize) };
            commandList->RSSetViewports(1, &viewport);
            commandList->RSSetScissorRects(1, &scissor);

            const bool draws = m_hasDirectionalLight && nullptr != context.draws;
            if (draws)
            {
                // 캐스케이드마다 상태를 다시 걸지 않는다. PSO와 루트 시그니처는
                // 셋이 같으므로 바깥에서 한 번이면 된다.
                commandList->SetGraphicsRootSignature(m_rootSignature);
                commandList->SetPipelineState(m_pso);
                commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            }

            // 조각 하나가 (캐스케이드, 드로우 범위) 하나를 맡는다.
            //
            // 캐스케이드당 조각 수를 먼저 정하고, 그 안에서 드로우를 나눈다.
            // sliceCount가 1이면 캐스케이드 전부·드로우 전부가 되어 통째로
            // 기록하는 것과 같다 — 그것이 분할 콜백의 계약이다.
            const uint32_t slicesPerCascade = (std::max)(1u, sliceCount / kCascadeCount);
            const uint32_t cascadeBegin = (sliceCount <= kCascadeCount)
                ? (kCascadeCount * slice / sliceCount) : (slice / slicesPerCascade);
            const uint32_t cascadeEnd = (sliceCount <= kCascadeCount)
                ? (kCascadeCount * (slice + 1) / sliceCount) : (cascadeBegin + 1);
            const uint32_t drawSlice = (sliceCount <= kCascadeCount)
                ? 0u : (slice % slicesPerCascade);
            const uint32_t drawSliceCount = (sliceCount <= kCascadeCount) ? 1u : slicesPerCascade;

            for (uint32_t index = cascadeBegin; index < cascadeEnd && index < kCascadeCount; ++index)
            {
                D3D12_CPU_DESCRIPTOR_HANDLE dsv = m_dsvHeap->GetCPUDescriptorHandleForHeapStart();
                dsv.ptr += static_cast<SIZE_T>(index) * m_dsvIncrement;

                D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc{};
                dsvDesc.Format = kShadowFormat;
                dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2DARRAY;
                dsvDesc.Texture2DArray.FirstArraySlice = index;
                dsvDesc.Texture2DArray.ArraySize = 1;
                device->CreateDepthStencilView(shadowMap, &dsvDesc, dsv);

                // 렌더 타깃 없이 깊이만 묶는다.
                commandList->OMSetRenderTargets(0, nullptr, FALSE, &dsv);

                // 클리어는 그 캐스케이드의 첫 조각에서만. 뒤 조각이 또 지우면
                // 앞 조각이 그린 것이 사라진다.
                if (0 == drawSlice)
                {
                    commandList->ClearDepthStencilView(dsv,
                        D3D12_CLEAR_FLAG_DEPTH, 1.f, 0, 0, nullptr);
                }

                if (!draws) continue;

                const Cascade& cascade = m_cascades[index];
                const Mathf::xMatrix transposed = XMMatrixTranspose(cascade.lightViewProjection);

                // 자기 몫의 드로우만 본다. 컬링 판정도 그 범위 안에서만 센다.
                const size_t drawCount = context.draws->size();
                const size_t drawBegin = drawCount * drawSlice / drawSliceCount;
                const size_t drawEnd = drawCount * (drawSlice + 1) / drawSliceCount;

                for (size_t drawIndex = drawBegin; drawIndex < drawEnd; ++drawIndex)
                {
                    const auto& draw = (*context.draws)[drawIndex];

                    const auto found = m_drawGeometry.find(draw.mesh);
                    if (found == m_drawGeometry.end() || !found->second.entry.IsValid()) continue;

                    // 경계 구를 월드로 옮긴다. 비균등 배율에서는 최대 축으로
                    // 잡아야 보수적이다 — 작게 잡으면 그림자가 사라진다.
                    const Mathf::xVector worldCenter = XMVector3Transform(
                        XMVectorSet(0.f, 0.f, 0.f, 1.f), draw.worldMatrix);
                    const float scale = (std::max)({
                        XMVectorGetX(XMVector3Length(draw.worldMatrix.r[0])),
                        XMVectorGetX(XMVector3Length(draw.worldMatrix.r[1])),
                        XMVectorGetX(XMVector3Length(draw.worldMatrix.r[2])) });

                    if (!CastsInto(cascade, Mathf::Vector3(worldCenter),
                        found->second.boundRadius * scale))
                    {
                        m_lastCulledCount.fetch_add(1, std::memory_order_relaxed);
                        continue;
                    }

                    ShadowConstants constants{};
                    constants.lightViewProjection = transposed;
                    constants.world = XMMatrixTranspose(draw.worldMatrix);

                    const auto allocation = context.resources->GetUploadRing().Allocate(
                        sizeof(ShadowConstants), DX12UploadRing::kConstantBufferAlignment);
                    if (!allocation.IsValid()) break;

                    memcpy(allocation.cpuAddress, &constants, sizeof(constants));
                    commandList->SetGraphicsRootConstantBufferView(0, allocation.gpuAddress);

                    commandList->IASetVertexBuffers(0, 1, &found->second.entry.vertexView);
                    commandList->IASetIndexBuffer(&found->second.entry.indexView);
                    commandList->DrawIndexedInstanced(found->second.entry.indexCount, 1, 0, 0, 0);
                    m_lastDrawCount.fetch_add(1, std::memory_order_relaxed);
                }
            }
        },
        // 조각 수는 캐스터 수로 정한다. GBuffer와 같은 이유로 무조건 쪼개지
        // 않는다 — 조각마다 상태를 다시 걸어야 하고, 그 비용이 드로우 몇 개
        // 그리는 것보다 크면 손해다.
        ComputeSliceCount(),
        // 그림자 맵을 읽는 것은 Deferred다. 뿌리로 표시하지 않아도 컬링이
        // 살려야 하고, 그것이 3-5 컬링의 또 한 번의 확인이다.
        false);
}

uint32_t EnhancedShadowPass::ComputeSliceCount() const
{
    // 캐스케이드가 셋이므로 최소 단위는 셋이다. 그 위로는 드로우 수를 보고
    // 캐스케이드마다 몇 조각으로 더 나눌지 정한다.
    //
    // 조각당 최소 드로우 수는 GBuffer와 같은 값을 쓴다. 실측으로 정한 경계이고,
    // 두 패스의 드로우당 기록 비용이 비슷하다(둘 다 상수 하나 올리고 드로우 하나).
    constexpr uint32_t kMinDrawsPerSlice = 32;

    // 후보 수를 쓴다. 실제 그린 수(m_lastDrawCount)는 Record에서 채워지므로
    // Declare 시점에는 지난 프레임 값이고, 그것으로 이번 프레임의 조각 수를
    // 정하면 씬이 바뀔 때 한 프레임씩 늦는다.
    const uint32_t drawCount = m_lastCasterCandidates;

    if (drawCount <= kMinDrawsPerSlice) return kCascadeCount;

    const uint32_t perCascade = (std::max)(1u, drawCount / kMinDrawsPerSlice);
    return (std::min)(DX12CommandListPool::kMaxWorkers, kCascadeCount * perCascade);
}

void EnhancedShadowPass::Shutdown()
{
    m_drawGeometry.clear();
    m_dsvHeap.Reset();
    m_dsvIncrement = 0;
    m_pso = nullptr;
    m_rootSignature = nullptr;
}

#endif
