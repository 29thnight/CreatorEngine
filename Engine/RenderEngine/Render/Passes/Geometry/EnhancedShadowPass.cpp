#include "EnhancedShadowPass.h"
#include "../../../RHI/RHIEncoder.h"
#include "../../Graph/ShadowCasterBounds.h"

#include <mathematics/transform.hpp>
#include "../../../../Utility_Framework/StandardLibrary.h"

bool EnhancedShadowPass::Initialize(const EnhancedFrameContext& context, std::string& outError)
{
    // 캐스터 PSO는 재질 그래프 호스트가 소유한다. 이 패스는 맵과 클리어만 낸다.
    if (nullptr == context.resources)
    {
        outError = "그림자 패스 컨텍스트가 불완전하다";
        return false;
    }
    return true;
}

void EnhancedShadowPass::ComputeCascades(const EnhancedFrameContext& context)
{
    m_hasDirectionalLight = false;
    m_shadowData = EnhancedShadowData{};
    for (auto& cascade : m_cascades)
    {
        cascade = Cascade{};
    }

    if (nullptr == context.camera || nullptr == context.lights)
    {
        return;
    }
    // 방향광 하나만 그림자를 드리운다. 여러 방향광의 그림자는 맵이 그만큼
    // 필요하고, 실제로 둘 이상 쓰는 씬이 나왔을 때 정하는 것이 맞다.
    //
    // ★ 그 하나를 "가장 센 것"으로 고른다. 예전에는 배열의 첫 방향광이었고,
    //   그러면 등록 순서가 그림자의 주인을 정했다 — 보조 방향광을 나중에
    //   더하면 그림자가 그쪽으로 옮겨 가는 부류다. 뷰가 미는 목록은 이미
    //   방향광을 세기순으로 앞에 세우지만(EnhancedLightPacking.h), 그 순서에
    //   말없이 기대면 목록을 만드는 쪽이 바뀔 때 조용히 깨진다.
    float strongest = -1.f;
    uint32_t selectedLight = 0;
    for (const auto& light : *context.lights)
    {
        if (0 != static_cast<uint32_t>(light.position.w))
        {
            continue;
        }
        const float intensity = light.color.a;
        if (intensity <= strongest)
        {
            continue;
        }
        strongest = intensity;
        selectedLight = static_cast<uint32_t>(&light - context.lights->data());
        m_lightDirection = math::vector3{ light.direction.x, light.direction.y,
            light.direction.z };
        m_hasDirectionalLight = true;
    }
    if (!m_hasDirectionalLight)
    {
        return;
    }
    if (math::length_sq(m_lightDirection) < 1e-6f)
    {
        m_lightDirection = { 0.f, -1.f, 0.f };
    }
    m_lightDirection = math::normalize(m_lightDirection);

    const auto receivers = shadow_math::ReceiverCascades(*context.camera, m_lightDirection,
        m_shadowDistance, m_blendBand, kShadowMapSize);
    for (uint32_t index = 0; index < kCascadeCount; ++index)
    {
        const auto& receiver = receivers[index];
        const auto center = receiver.bounds.center;
        const float radius = receiver.bounds.radius;
        const float sliceFar = receiver.split;
        const float worldTexel = radius * 2.f / float(kShadowMapSize);
        float backOff = radius * 2.f;
        for (const auto& bounds : m_casterBounds)
        {
            const auto offset = bounds.center - center;
            const float along = math::dot(offset, m_lightDirection);
            const auto perpendicular = offset - m_lightDirection * along;
            if (math::length(perpendicular) <= radius + bounds.radius)
            {
                backOff = (std::max)(backOff, -along + bounds.radius + worldTexel);
            }
        }
        const math::vector3 lightPosition = center - m_lightDirection * backOff;

        // 광원이 정확히 위나 아래를 볼 때 up이 평행해지는 것을 피한다.
        const math::vector3 up = (std::fabs(m_lightDirection.y) > 0.99f)
            ? math::vector3{ 0.f, 0.f, 1.f }
            : math::vector3{ 0.f, 1.f, 0.f };

        const math::matrix4x4 lightView = math::look_at_lh(lightPosition, center, up);
        const math::matrix4x4 lightProjection = math::orthographic_off_center_lh(
            -radius, radius, -radius, radius, 0.f, backOff + radius * 2.f);

        Cascade& cascade = m_cascades[index];
        cascade.lightViewProjection = lightView * lightProjection;
        cascade.center = center;
        cascade.radius = radius;
        cascade.depthSpan = backOff + radius * 2.f;
        cascade.splitDepth = sliceFar;

        m_shadowData.lightViewProjection[index] = cascade.lightViewProjection;
    }

    // 셰이더가 읽는 형태로 옮긴다. 분할 지점과 편향을 float4 하나씩에 담고
    // 있으므로 캐스케이드가 셋을 넘으면 담는 방식부터 바꿔야 한다.
    static_assert(3 == kCascadeCount, "splitDepths·bias가 float4 하나에 셋을 담는다");

    m_shadowData.splitDepths = math::vector4{ m_cascades[0].splitDepth,
        m_cascades[1].splitDepth, m_cascades[2].splitDepth, float(selectedLight + 1) };

    // 먼 캐스케이드는 텍셀 하나가 덮는 월드 범위가 넓다. 같은 편향을 쓰면
    // 그쪽에만 여드름이 남으므로 반지름 비만큼 키운다.
    // Bias is expressed in shadow texels, then converted exactly once to light depth.
    const auto bias = [&](uint32_t i) {
        return m_baseBias * (2.f * m_cascades[i].radius / float(kShadowMapSize))
            / m_cascades[i].depthSpan;
    };
    m_shadowData.bias = math::vector4{bias(0), bias(1), bias(2), m_slopeScale};

    m_shadowData.cascadeBlendBand = m_blendBand;

    const math::vector3& cameraForward = context.camera->forward;
    const uint32_t packedOptions = static_cast<uint32_t>(m_debugView)
        | (static_cast<uint32_t>(m_filter) << 4u);
    m_shadowData.cameraForward = math::vector4{
        cameraForward.x, cameraForward.y, cameraForward.z, static_cast<float>(packedOptions) };
    m_shadowData.lightDirection = math::vector4{ m_lightDirection.x, m_lightDirection.y,
        m_lightDirection.z, 0.f };
    m_shadowData.mapResolution = kShadowMapSize;
    m_shadowData.enabled = true;
}

EnhancedShadowPass::DebugStats EnhancedShadowPass::GetDebugStats() const
{
    DebugStats stats;
    stats.hasDirectionalLight = m_hasDirectionalLight;
    stats.lightIndex = (std::max)(1u, static_cast<uint32_t>(m_shadowData.splitDepths.w)) - 1u;
    stats.lightDirection = m_lightDirection;
    stats.shadowDistance = m_shadowDistance;
    stats.slopeScale = m_slopeScale;
    stats.casterCandidates = m_lastCasterCandidates;
    if (!m_hasDirectionalLight)
    {
        return stats;
    }
    for (uint32_t index = 0; index < kCascadeCount; ++index)
    {
        const Cascade& cascade = m_cascades[index];
        CascadeStats& out = stats.cascades[index];
        out.splitDepth = cascade.splitDepth;
        out.radius = cascade.radius;
        out.worldTexel = cascade.radius * 2.f / float(kShadowMapSize);
        out.depthSpan = cascade.depthSpan;
        // ComputeCascades 의 편향은 깊이 범위로 나눈 값이다 — 되곱해 월드로 돌린다.
        out.constantBias = m_shadowData.bias[static_cast<int>(index)] * cascade.depthSpan;
    }
    return stats;
}

bool EnhancedShadowPass::PrepareGraphFrame(const EnhancedFrameContext& context, std::string& outError)
{
    m_casterBounds.clear();
    const auto* casters = context.shadowDraws;
    if (!context.resources || !casters)
    {
        outError = "Graph shadow requires device services and the identified caster stream.";
        return false;
    }
    for (const auto& draw : *casters)
    {
        if (!draw.GraphInstance())
        {
            outError = "Graph shadow caster has no material graph instance.";
            return false;
        }
        m_casterBounds.push_back(shadow_math::WorldBounds(draw));
    }
    m_lastCasterCandidates = static_cast<uint32_t>(casters->size());
    ComputeCascades(context);
    outError.clear();
    return true;
}

void EnhancedShadowPass::DeclareGraphTargets(EnhancedRenderGraph& graph, const EnhancedFrameContext& context)
{
    RGTextureDesc desc{};
    desc.width = kShadowMapSize;
    desc.height = kShadowMapSize;
    desc.arraySize = kCascadeCount;
    desc.format = kShadowFormat;
    desc.allowDepthStencil = true;
    desc.name = "Shadow.Cascades";
    m_shadowMap = graph.CreateTexture(desc);
    if (graph.GetSchedulingMode() == RGSchedulingMode::ExplicitVersioned)
    {
        m_shadowMap = graph.Write(m_shadowMap);
    }
    const auto access = graph.GetSchedulingMode() == RGSchedulingMode::DeclarationOrder
        ? RGAccessMode::LegacyState : RGAccessMode::Write;
    graph.AddPass("Shadow.Clear", {{m_shadowMap, RHIResourceState::DepthWrite, access}},
        [this, &context](const EnhancedRenderGraph::ExecuteContext& execution)
        {
            auto& encoder = *execution.encoder;
            for (uint32_t cascade = 0; cascade < kCascadeCount; ++cascade)
            {
                const auto depth = RHIDepthTargetDesc::DepthSlice(
                    execution.ResolveHandle(m_shadowMap), kShadowFormat, cascade);
                const auto targets = context.resources->CreateRenderTargets(
                    std::span<const RHITextureHandle>{}, &depth);
                if (!targets.IsValid())
                {
                    throw std::runtime_error("Graph shadow clear target is unavailable.");
                }
                encoder.BindRenderTargets(targets);
                encoder.ClearDepthTarget(targets, 1.f);
            }
        });
}
