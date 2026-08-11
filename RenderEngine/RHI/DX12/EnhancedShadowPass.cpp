#ifndef DYNAMICCPP_EXPORTS
#include "EnhancedShadowPass.h"
#include "DX12DeviceResources.h"
#include "DX12PSOManager.h"
#include "DX12RootSignatureCache.h"
#include "../RHIEncoder.h"
#include "../../Mesh.h"

#include <DirectXCollision.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <sstream>
#include "DX12ShaderCompiler.h"

namespace
{
    // 유니티 빌드에서 익명 네임스페이스가 파일 간 합쳐지므로 이름을 고유하게 둔다.
    //
    // 셰이더는 하나이고 SHADOW_SKINNING 매크로로 두 벌을 만든다.
    //
    // ★ 왜 한 벌로 안 두는가: 그림자 패스는 "위치만 읽는다"가 설계다.
    //   캐스케이드 셋을 전부 다시 그리므로 정점 대역폭이 세 배로 얹히고,
    //   정적 메시가 대부분인 씬에서 본 인덱스·가중치 32바이트를 읽고 버리는
    //   것은 그 설계를 무르는 일이다. 입력 레이아웃과 PSO를 갈라, 스킨드
    //   배치에서만 그 32바이트를 읽는다.
    constexpr const char* kShadowShaderFile = "Shadow.hlsl";

    bool CompileShadowShader(const char* entry, const char* target, bool skinning,
        RHIShaderBlob& outBlob, std::string& outError)
    {
        const D3D_SHADER_MACRO skinningMacros[] = {
            { "SHADOW_SKINNING", "1" }, { nullptr, nullptr } };

        return DX12ShaderCompiler::CompileFile(kShadowShaderFile, entry, target,
            skinning ? skinningMacros : nullptr, outBlob, outError);
    }
}

bool EnhancedShadowPass::CreatePipeline(const EnhancedFrameContext& context, std::string& outError)
{
    RHIShaderBlob vsBlob;
    RHIShaderBlob skinnedVsBlob;
    if (!CompileShadowShader("VSMain", "vs_5_0", false, vsBlob, outError)) return false;
    if (!CompileShadowShader("VSMain", "vs_5_0", true, skinnedVsBlob, outError)) return false;

    // 인스턴스 배열은 루트 SRV로 넘긴다. 업로드 링에서 자른 조각의 GPU 주소를
    // 그대로 꽂으면 되므로 디스크립터를 만들 필요가 없다 — 배치마다 바뀌는
    // 값이라 디스크립터 테이블보다 이쪽이 싸다.
    //
    // 본 팔레트(t1)는 정적 PSO가 읽지 않지만 루트 시그니처는 하나로 둔다 —
    // 둘로 나누면 PSO 전환마다 루트까지 갈아야 하고, 그건 이 패스가 아끼려는
    // 상태 변경을 도로 늘리는 일이다.
    const RHIPipelineLayoutParam params[] = {
        RHILayout::Cbv(0, RHIShaderVisibility::Vertex),
        RHILayout::Srv(0, RHIShaderVisibility::Vertex),
        RHILayout::Srv(1, RHIShaderVisibility::Vertex),
    };

    RHIPipelineLayoutDesc rootDesc{};
    rootDesc.params = params;
    rootDesc.allowInputAssembler = true;

    const auto root = context.rootSignatures->GetOrCreate(rootDesc, outError);
    if (!root.IsValid()) return false;

    // 위치만 읽는다. 정점 구조체는 같지만 나머지 요소를 선언하지 않으면
    // 입력 조립이 그것들을 가져오지 않는다 — 대역폭이 그만큼 준다.
    static const RHIInputElement kInputElements[] = {
        { "POSITION", 0, RHIFormat::RGB32Float, 0, 0, 0 },
    };

    // 스킨드 경로만 본 인덱스·가중치를 읽는다. 오프셋은 엔진 Vertex를 따른다.
    static const RHIInputElement kSkinnedInputElements[] = {
        { "POSITION",     0, RHIFormat::RGB32Float,    0,  0, 0 },
        { "BLENDINDICES", 0, RHIFormat::RGBA32Float, 0, 64, 0 },
        { "BLENDWEIGHT",  0, RHIFormat::RGBA32Float, 0, 80, 0 },
    };
    static_assert(offsetof(Vertex, boneIndices) == 64, "Vertex 레이아웃이 바뀌었다");
    static_assert(offsetof(Vertex, boneWeights) == 80, "Vertex 레이아웃이 바뀌었다");

    RHIGraphicsPipelineDesc desc{};
    desc.inputElements = kInputElements;
    desc.inputElementCount = _countof(kInputElements);
    desc.vsBytecode = vsBlob.Data();
    desc.vsSize = vsBlob.Size();
    desc.psBytecode = nullptr;   // 깊이 전용
    desc.psSize = 0;
    desc.layout = root;
    desc.depthEnable = true;
    // 컬링을 끈다. 앞면만 그리면 닫히지 않은 메시(평면·판때기)가 그림자를
    // 아예 못 드리우고, 뒷면만 그리면 그런 메시가 그림자를 두 번 드리운다.
    // 여드름은 편향으로 잡는 편이 씬 종류를 덜 탄다.
    desc.cullMode = RHICullMode::None;
    desc.numRenderTargets = 0;
    desc.dsvFormat = kShadowFormat;

    m_pso = context.psoManager->GetOrCreate(desc, outError);
    if (!m_pso.IsValid()) return false;

    // 스킨드 변형 — 입력 레이아웃과 정점 셰이더만 다르고 나머지는 같다.
    desc.inputElements = kSkinnedInputElements;
    desc.inputElementCount = _countof(kSkinnedInputElements);
    desc.vsBytecode = skinnedVsBlob.Data();
    desc.vsSize = skinnedVsBlob.Size();

    m_skinnedPso = context.psoManager->GetOrCreate(desc, outError);
    return m_skinnedPso.IsValid();
}

bool EnhancedShadowPass::Initialize(const EnhancedFrameContext& context, std::string& outError)
{
    if (nullptr == context.resources || nullptr == context.psoManager ||
        nullptr == context.rootSignatures)
    {
        outError = "그림자 패스 컨텍스트가 불완전하다";
        return false;
    }

    return CreatePipeline(context, outError);
}

void EnhancedShadowPass::ComputeCascades(const EnhancedFrameContext& context)
{
    m_hasDirectionalLight = false;
    m_shadowData = EnhancedShadowData{};
    for (auto& cascade : m_cascades) cascade = Cascade{};

    if (nullptr == context.camera || nullptr == context.lights) return;

    // 방향광 하나만 그림자를 드리운다. 여러 방향광의 그림자는 맵이 그만큼
    // 필요하고, 실제로 둘 이상 쓰는 씬이 나왔을 때 정하는 것이 맞다.
    //
    // ★ 그 하나를 "가장 센 것"으로 고른다. 예전에는 배열의 첫 방향광이었고,
    //   그러면 등록 순서가 그림자의 주인을 정했다 — 보조 방향광을 나중에
    //   더하면 그림자가 그쪽으로 옮겨 가는 부류다. 뷰가 미는 목록은 이미
    //   방향광을 세기순으로 앞에 세우지만(EnhancedLightPacking.h), 그 순서에
    //   말없이 기대면 목록을 만드는 쪽이 바뀔 때 조용히 깨진다.
    float strongest = -1.f;
    for (const auto& light : *context.lights)
    {
        if (0 != static_cast<uint32_t>(light.position.w)) continue;

        const float intensity = light.color.w;
        if (intensity <= strongest) continue;

        strongest = intensity;
        m_lightDirection = Mathf::Vector3{ light.direction.x, light.direction.y,
            light.direction.z };
        m_hasDirectionalLight = true;
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
        m_slopeScale };

    m_shadowData.cascadeBlendBand = m_blendBand;

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
    m_sortedDraws.clear();
    m_bonePalettes.clear();
    m_boneOffsets.clear();
    m_lastDrawCount.store(0, std::memory_order_relaxed);
    m_lastCulledCount.store(0, std::memory_order_relaxed);
    m_lastBatchCount.store(0, std::memory_order_relaxed);
    m_lastSkinnedDrawCount.store(0, std::memory_order_relaxed);

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

    // 본 팔레트를 애니메이터별로 한 번씩 모은다(GBuffer와 같은 규칙).
    //
    // 두 패스가 각자 모으는 것이 낭비로 보이지만, 공유하려면 패스 사이에
    // 소유자를 두어야 하고 그건 실행 순서에 대한 가정을 하나 더 만든다.
    // 팔레트 복사는 애니메이터당 한 번이라 실측으로도 문제가 안 된다.
    for (const auto& draw : *context.draws)
    {
        if (nullptr == draw.mesh) continue;
        if (nullptr == draw.bonePalette || 0 == draw.boneCount) continue;
        if (m_boneOffsets.find(draw.animatorKey) != m_boneOffsets.end()) continue;

        const uint32_t offset = static_cast<uint32_t>(m_bonePalettes.size());
        m_bonePalettes.resize(offset + draw.boneCount);
        for (uint32_t i = 0; i < draw.boneCount; ++i)
        {
            m_bonePalettes[offset + i] = XMMatrixTranspose(draw.bonePalette[i]);
        }
        m_boneOffsets.emplace(draw.animatorKey, offset);
    }

    // 메시 기준으로 정렬해 둔다. 같은 메시가 붙어 있어야 Record에서 한 번의
    // 드로우로 묶인다.
    //
    // 그림자는 정렬을 망설일 이유가 없다. GBuffer에서는 '순서를 바꾸면 깊이
    // 테스트 결과가 달라질까'를 따져야 했지만(따져 보니 불투명·LESS라 무관했다),
    // 여기는 애초에 깊이만 쓰고 색을 안 쓴다 — 어느 순서로 그리든 최종 깊이는
    // 가장 가까운 값 하나다.
    //
    // 드로우 자체가 아니라 인덱스를 정렬한다. context.draws는 GBuffer도 같이
    // 보는 것이라 건드리면 안 된다.
    m_sortedDraws.reserve(context.draws->size());
    for (size_t index = 0; index < context.draws->size(); ++index)
    {
        if (nullptr != (*context.draws)[index].mesh) m_sortedDraws.push_back(index);
    }

    // 스킨드 여부를 첫 키로 둔다. PSO가 둘이므로 섞여 있으면 배치마다
    // 파이프라인을 갈아야 하고, 그것이 이 패스가 아끼려는 상태 변경이다.
    // 뭉쳐 두면 전환이 그룹 경계 한 번뿐이다.
    std::stable_sort(m_sortedDraws.begin(), m_sortedDraws.end(),
        [&](size_t a, size_t b)
        {
            const auto& drawA = (*context.draws)[a];
            const auto& drawB = (*context.draws)[b];

            const bool skinnedA = (nullptr != drawA.bonePalette) && (0 != drawA.boneCount);
            const bool skinnedB = (nullptr != drawB.bonePalette) && (0 != drawB.boneCount);
            if (skinnedA != skinnedB) return skinnedA < skinnedB;

            return drawA.mesh < drawB.mesh;
        });

    // 조각 수를 정하는 근거. 컬링 전 후보라 상한이지만, '몇 조각으로 나눌까'에는
    // 그것으로 충분하다 — 정확한 수는 그려 봐야 알고, 그때는 이미 늦다.
    m_lastCasterCandidates = static_cast<uint32_t>(m_sortedDraws.size());

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
    graph.AddSplitPass(GetName(), { { m_shadowMap, RHIResourceState::DepthWrite } },
        [this, &context](const EnhancedRenderGraph::ExecuteContext& executeContext,
            uint32_t slice, uint32_t sliceCount)
        {
            RHIEncoder& encoder = *executeContext.encoder;
            const RHITextureHandle shadowMap = executeContext.ResolveHandle(m_shadowMap);

            encoder.SetViewportAndScissor(kShadowMapSize, kShadowMapSize);

            const bool draws = m_hasDirectionalLight && nullptr != context.draws;
            if (draws)
            {
                // 캐스케이드마다 상태를 다시 걸지 않는다. 루트 시그니처는 셋이
                // 같으므로 바깥에서 한 번이면 된다.
                //
                // PSO는 배치가 스킨드인지에 따라 갈리므로 여기서 걸지 않는다 —
                // 아래 flushBatch가 필요할 때만 바꾼다.
                // ★ A-1 이후 "파이프라인 없이 루트만"이 표현 불가능하다.
                //   PSO 는 아래 flushBatch 가 배치마다 고르므로, 여기서는 기본
                //   경로(비스킨드)를 걸어 루트 시그니처를 세운다 — 레이아웃이
                //   둘 사이에 같아서 성립하고, 인코더가 중복 바인딩을 걸러 낸다.
                encoder.SetPipeline(RHIBindPoint::Graphics, m_pso);
                encoder.SetPrimitiveTopology(RHIPrimitiveTopology::TriangleList);

                // 본 팔레트는 조각당 한 번. 스킨드가 없어도 꽂는다 — 루트에
                // 선언된 슬롯을 비워 두면 검증 레이어가 경고한다.
                const uint64_t paletteBytes = m_bonePalettes.empty()
                    ? sizeof(Mathf::Matrix)
                    : sizeof(Mathf::Matrix) * static_cast<uint64_t>(m_bonePalettes.size());

                const auto paletteBuffer = context.resources->AllocateUpload(
                    paletteBytes, sizeof(Mathf::Matrix));
                if (paletteBuffer.IsValid())
                {
                    if (m_bonePalettes.empty())
                    {
                        const Mathf::Matrix identity = XMMatrixIdentity();
                        memcpy(paletteBuffer.cpuAddress, &identity, sizeof(identity));
                    }
                    else
                    {
                        memcpy(paletteBuffer.cpuAddress, m_bonePalettes.data(),
                            static_cast<size_t>(paletteBytes));
                    }
                    encoder.SetRootBuffer(RHIBindPoint::Graphics, 2, paletteBuffer);
                }
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
                // 캐스케이드 하나 = 배열 슬라이스 하나. 색 타깃 없이 깊이만 묶는다.
                const auto depthDesc = RHIDepthTargetDesc::DepthSlice(
                    shadowMap, kShadowFormat, index);
                const auto boundTargets = context.resources->CreateRenderTargets({}, &depthDesc);
                if (!boundTargets.IsValid()) continue;

                encoder.BindRenderTargets(boundTargets);

                // 클리어는 그 캐스케이드의 첫 조각에서만. 뒤 조각이 또 지우면
                // 앞 조각이 그린 것이 사라진다.
                if (0 == drawSlice)
                {
                    encoder.ClearDepthTarget(boundTargets, 1.f);
                }

                if (!draws) continue;

                const Cascade& cascade = m_cascades[index];

                // 광원 행렬은 캐스케이드마다 한 번만 올린다. 예전에는 드로우마다
                // 월드와 함께 올렸는데, 같은 값을 수백 번 복사하는 것이 CE 단계를
                // 늘리는 방향이다.
                ShadowConstants constants{};
                constants.lightViewProjection = XMMatrixTranspose(cascade.lightViewProjection);

                const auto cbAllocation = context.resources->AllocateUpload(
                    sizeof(ShadowConstants), DX12UploadRing::kConstantBufferAlignment);
                if (!cbAllocation.IsValid()) continue;

                memcpy(cbAllocation.cpuAddress, &constants, sizeof(constants));
                encoder.SetConstantBuffer(RHIBindPoint::Graphics, 0, cbAllocation);

                // 자기 몫의 드로우만 본다. 컬링 판정도 그 범위 안에서만 센다.
                const size_t drawCount = m_sortedDraws.size();
                const size_t drawBegin = drawCount * drawSlice / drawSliceCount;
                const size_t drawEnd = drawCount * (drawSlice + 1) / drawSliceCount;

                // ── 배치를 여기서 만드는 이유 ──
                //
                // GBuffer는 배치를 PrepareFrame에서 한 번 만들어 두고 Record는
                // 읽기만 한다. 그림자는 그럴 수 없다 — 컬링(CastsInto)이
                // 캐스케이드마다 다른 답을 내므로 어느 인스턴스가 살아남는지가
                // 캐스케이드마다 다르다. 미리 만든 배치를 그대로 쓰면 컬린 것까지
                // 그리게 되고, 그러면 컬링이 하는 일이 없어진다.
                //
                // 그래서 조각 안에서 모은다. 지역 변수인 것이 중요하다 —
                // 워커들이 동시에 도는 자리라 멤버에 모으면 서로 덮어쓴다.
                std::vector<ShadowInstance> instances;
                instances.reserve(drawEnd - drawBegin);

                Mesh* batchMesh = nullptr;
                bool  batchSkinned = false;

                // 지금 걸려 있는 PSO. 조각마다 상태를 새로 걸어야 하므로
                // 처음에는 아무것도 안 걸린 것으로 둔다.
                RHIPipelineHandle boundPso;

                // 모아 둔 인스턴스를 드로우 하나로 낸다.
                const auto flushBatch = [&]()
                {
                    if (instances.empty() || nullptr == batchMesh) return;

                    const auto found = m_drawGeometry.find(batchMesh);
                    if (found != m_drawGeometry.end() && found->second.entry.IsValid())
                    {
                        // 배치마다 링에서 자른다. dx12.bench11 실측으로 Allocate
                        // 호출당 ~175ns가 붙는 것을 알지만(GBuffer는 그래서 조각당
                        // 블록 하나로 바꿨다), 여기는 그대로 둔다 — 배치가 컬링
                        // 결과에 따라 즉석에서 만들어져 크기를 미리 모르고, 상한
                        // (조각의 후보 전부)으로 잡으면 컬링이 잘 될수록 링을
                        // 낭비한다. 배치 수가 고유 메시 수 × 캐스케이드로 묶여
                        // 있어 호출 수 자체가 작다는 것이 근거다.
                        const uint64_t instanceBytes =
                            sizeof(ShadowInstance) * static_cast<uint64_t>(instances.size());
                        const auto instanceBuffer = context.resources->AllocateUpload(
                            instanceBytes, sizeof(ShadowInstance));

                        if (instanceBuffer.IsValid())
                        {
                            // 스킨드 배치만 스킨드 PSO로 바꾼다. 정렬이 스킨드를
                            // 뭉쳐 두었으므로 전환은 그룹 경계에서 한 번뿐이다.
                            RHIPipelineHandle wanted =
                                batchSkinned ? m_skinnedPso : m_pso;
                            if (wanted != boundPso)
                            {
                                // 루트 시그니처는 위에서 이미 걸었다. 그대로 다시
                                // 넘기지만 인코더가 중복을 걸러 내므로 여기서
                                // 갈리는 것은 PSO 하나다.
                                encoder.SetPipeline(RHIBindPoint::Graphics,
                                    wanted);
                                boundPso = wanted;
                            }

                            memcpy(instanceBuffer.cpuAddress, instances.data(),
                                static_cast<size_t>(instanceBytes));
                            encoder.SetRootBuffer(RHIBindPoint::Graphics,
                                1, instanceBuffer);

                            encoder.SetVertexBuffer(found->second.entry.vertices, found->second.entry.vertexStride);
                            encoder.SetIndexBuffer(found->second.entry.indices, found->second.entry.indexFormat);
                            encoder.DrawIndexed(found->second.entry.indexCount,
                                static_cast<uint32_t>(instances.size()));

                            // 드로우 수는 인스턴스 수로 센다. 배치로 세면 병합이
                            // 늘수록 '캐스터가 줄었다'로 보여 컬링 단정이 흐려진다.
                            m_lastDrawCount.fetch_add(
                                static_cast<uint32_t>(instances.size()), std::memory_order_relaxed);
                            m_lastBatchCount.fetch_add(1, std::memory_order_relaxed);

                            if (batchSkinned)
                            {
                                m_lastSkinnedDrawCount.fetch_add(
                                    static_cast<uint32_t>(instances.size()),
                                    std::memory_order_relaxed);
                            }
                        }
                    }

                    instances.clear();
                };

                for (size_t sortedIndex = drawBegin; sortedIndex < drawEnd; ++sortedIndex)
                {
                    const auto& draw = (*context.draws)[m_sortedDraws[sortedIndex]];

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

                    const bool skinned =
                        (nullptr != draw.bonePalette) && (0 != draw.boneCount);

                    // 메시나 스킨드 여부가 바뀌면 지금까지 모은 것을 낸다.
                    // 정렬해 두었으므로 같은 것끼리는 이미 붙어 있다.
                    if (draw.mesh != batchMesh || skinned != batchSkinned)
                    {
                        flushBatch();
                        batchMesh = draw.mesh;
                        batchSkinned = skinned;
                    }

                    ShadowInstance instance{};
                    instance.world = XMMatrixTranspose(draw.worldMatrix);
                    instance.boneOffset = kNoSkinning;
                    if (skinned)
                    {
                        const auto offset = m_boneOffsets.find(draw.animatorKey);
                        if (offset != m_boneOffsets.end()) instance.boneOffset = offset->second;
                    }

                    instances.push_back(instance);
                }

                flushBatch();
            }
        },
        // 조각 수는 캐스터 수로 정한다. GBuffer와 같은 이유로 무조건 쪼개지
        // 않는다 — 조각마다 상태를 다시 걸어야 하고, 그 비용이 드로우 몇 개
        // 그리는 것보다 크면 손해다.
        ComputeSliceCount(),
        // 그림자 맵을 읽는 것은 Deferred다. 뿌리로 표시하지 않아도 컬링이
        // 살려야 하고, 그것이 3-5 컬링의 또 한 번의 확인이다.
        false,
        // 기록량은 배치 수다(GBuffer와 같은 기준). 캐스케이드마다 전부 다시
        // 그리므로 고유 메시 수의 세 배가 배치 수의 상한이다.
        //
        // 후보 수(캐스케이드당 드로우 수)를 쓰지 않는 이유는 GBuffer에서와
        // 같다 — 실측이 비용을 정하는 것은 배치라고 말했다.
        static_cast<uint32_t>(m_drawGeometry.size()) * kCascadeCount);
}

uint32_t EnhancedShadowPass::ComputeSliceCount() const
{
    // 캐스케이드가 셋이므로 최소 단위는 셋이다. 그 위로는 드로우 수를 보고
    // 캐스케이드마다 몇 조각으로 더 나눌지 정한다.
    //
    // 조각당 최소 드로우 수는 GBuffer와 같은 값을 쓴다. 실측으로 정한 경계이고,
    // 두 패스의 드로우당 기록 비용이 비슷하다(둘 다 상수 하나 올리고 드로우 하나).
    constexpr uint32_t kMinDrawsPerSlice = 32;

    // ★ 기준은 드로우가 아니라 '캐스케이드당 배치 수'다.
    //
    // 인스턴싱을 넣기 전에는 드로우 후보 수를 썼다. 지금 그대로 두면 손해가
    // 난다 — 드로우 704가 메시 11종이면 배치는 11개인데, 후보 704로 조각을
    // 계산하면 여덟 조각으로 나뉘고 각 조각이 자기 범위 안에서만 묶으므로
    // 배치가 도로 잘게 쪼개진다. 분할이 병합을 스스로 깨는 꼴이다.
    //
    // 배치 수는 Record에서 나오므로 Declare 시점에는 모른다. 대신 고유 메시
    // 수를 쓴다 — 정렬해서 같은 메시를 묶으므로 캐스케이드당 배치 수는
    // 정확히 그 값이 상한이다(컬링이 빼면 그보다 적어진다).
    const uint32_t drawCount = (std::min)(
        m_lastCasterCandidates, static_cast<uint32_t>(m_drawGeometry.size()));

    if (drawCount <= kMinDrawsPerSlice) return kCascadeCount;

    const uint32_t perCascade = (std::max)(1u, drawCount / kMinDrawsPerSlice);
    return (std::min)(DX12CommandListPool::kMaxWorkers, kCascadeCount * perCascade);
}

void EnhancedShadowPass::Shutdown()
{
    m_drawGeometry.clear();
    m_bonePalettes.clear();
    m_boneOffsets.clear();
    m_pso = {};
    m_skinnedPso = {};
}

#endif
