#include "EditorSceneOverlayContributor.h"

#include "EnhancedGizmoSceneBinding.h"
#include "GizmoRenderer.h"
#include "Render/Core/EnhancedLivePipelineDesc.h"
#include "Render/Passes/Editor/EnhancedGridPass.h"
#include "Render/Passes/Editor/EnhancedWireFramePass.h"
#include "Render/Passes/Editor/EnhancedGizmoIconPass.h"
#include "Render/Passes/Editor/EnhancedGizmoLinePass.h"

#include <memory>
#include <utility>

namespace
{
    // 그리드가 만드는 깊이. 와이어프레임이 같은 깊이로 가려져야 한다.
    // 이 슬롯은 기여 노드끼리만 잇는다 — Core 어휘(LiveSlots)에 두지 않는다.
    constexpr const char* kGizmoDepthSlot = "Gizmo.Depth";

    /// 한 파이프라인 조립에 딸린 패스 묶음. 노드 람다들이 shared_ptr로
    /// 붙들어 desc와 수명을 같이한다 — desc가 노드를 놓으면(ShutdownAll 뒤
    /// Clear) 묶음도 함께 사라진다.
    struct EditorOverlayPassBundle
    {
        EnhancedGridPass      grid;
        EnhancedWireFramePass wireframe;
        EnhancedGizmoIconPass gizmoIcon;
        EnhancedGizmoLinePass gizmoLine;
    };
}

void EditorSceneOverlayContributor::Contribute(LivePipelineDesc& pipeline,
    const RenderFeatureContext& context)
{
    auto bundle = std::make_shared<EditorOverlayPassBundle>();

    // 포스트 체인 LDR 위에 직접 그리는 구조라 PSO의 RTV 포맷을 표적에
    // 맞춰야 한다(어긋나면 커맨드 리스트 무효 → 디바이스 제거, 실측).
    bundle->grid.SetOutputFormat(context.ldrFormat);
    bundle->wireframe.SetOutputFormat(context.ldrFormat);
    bundle->gizmoIcon.SetOutputFormat(context.ldrFormat);
    bundle->gizmoLine.SetOutputFormat(context.ldrFormat);

    const EnhancedGizmoSceneData* gizmoScene = context.gizmoScene;

    // ── 기즈모 체인 — dx12.gizmoscene의 배선 그대로(DX11
    // GizmoRenderer::OnDrawGizmos 순서): Grid → WireFrame → Icon → Line.
    // 포스트 체인 LDR 위에 얹고 GBuffer 깊이로 가린다.
    //
    // ★ 네 노드 전부 씬 오버레이 뷰에서만 선다(kSceneOverlay). 저작
    //   보조물이라 게임 뷰(플레이어 화면)에 나오면 안 되는데, 노드 목록이
    //   뷰 공용이라 무조건 그려지고 있었다(2026-08-09 실측 — 게임 뷰에
    //   그리드와 광원 아이콘이 떴다). declare에서 건너뛰면 LDR 슬롯이
    //   그대로 남아 그 뷰는 이전 결과를 그대로 표시한다 — modifies 규약
    //   (꺼지면 슬롯 유지)이 그대로 성립한다.
    {
        LivePassNode node;
        node.name = "Grid";
        node.instance = [bundle](uint32_t) -> EnhancedRenderPass*
        {
            return &bundle->grid;
        };
        node.reads = { LiveSlots::kGBufferDepth };
        node.modifies = { LiveSlots::kDisplayLdr };
        node.writes = { kGizmoDepthSlot };
        node.declare = [bundle](LiveBlackboard& bb, EnhancedRenderGraph& graph,
            const EnhancedFrameContext& ctx, const LiveFrameBinding& binding)
        {
            if (0 == (binding.viewFlags & LiveViewFlags::kSceneOverlay)) return;

            EnhancedGridPass::Inputs inputs{};
            inputs.color = bb.Get(LiveSlots::kDisplayLdr);
            inputs.depth = bb.Get(LiveSlots::kGBufferDepth);
            bundle->grid.SetInputs(inputs);
            bundle->grid.Declare(graph, ctx);
            if (bundle->grid.GetOutput().IsValid())
            {
                bb.Set(LiveSlots::kDisplayLdr, bundle->grid.GetOutput());
            }
            bb.Set(kGizmoDepthSlot, bundle->grid.GetDepth());
        };
        pipeline.AddNode(std::move(node));
    }

    // ★ 와이어프레임은 모드일 때만 그린다(DX11 GizmoRenderer.cpp:67의
    //   if (m_buseWireFrame)와 같은 조건). 무조건 그렸더니 초록 와이어가
    //   씬 전체를 덮었다 — 에디터 오버레이는 '언제 그리는가'가 패스의
    //   일부다. 꺼지면 LDR 슬롯이 그대로 남아 아이콘이 그리드 결과를
    //   이어받는다.
    {
        LivePassNode node;
        node.name = "WireFrame";
        node.instance = [bundle](uint32_t) -> EnhancedRenderPass*
        {
            return &bundle->wireframe;
        };
        node.reads = { kGizmoDepthSlot };
        node.modifies = { LiveSlots::kDisplayLdr };
        node.active = []()
        {
            const GizmoRenderer* gizmoRenderer = GizmoRenderer::GetActive();
            return (nullptr != gizmoRenderer) && gizmoRenderer->IsWireFrameEnabled();
        };
        node.declare = [bundle](LiveBlackboard& bb, EnhancedRenderGraph& graph,
            const EnhancedFrameContext& ctx, const LiveFrameBinding& binding)
        {
            if (0 == (binding.viewFlags & LiveViewFlags::kSceneOverlay)) return;

            // 그리드가 이 뷰에서 깊이를 발행하지 않았으면(비활성 등)
            // 와이어도 서지 않는다.
            if (!bb.Get(kGizmoDepthSlot).IsValid()) return;

            EnhancedWireFramePass::Inputs inputs{};
            inputs.color = bb.Get(LiveSlots::kDisplayLdr);
            inputs.depth = bb.Get(kGizmoDepthSlot);
            bundle->wireframe.SetInputs(inputs);
            bundle->wireframe.Declare(graph, ctx);
            if (bundle->wireframe.GetOutput().IsValid())
            {
                bb.Set(LiveSlots::kDisplayLdr, bundle->wireframe.GetOutput());
            }
        };
        pipeline.AddNode(std::move(node));
    }

    {
        LivePassNode node;
        node.name = "GizmoIcon";
        node.instance = [bundle](uint32_t) -> EnhancedRenderPass*
        {
            return &bundle->gizmoIcon;
        };
        node.modifies = { LiveSlots::kDisplayLdr };
        // 프레임마다 갱신되는 아이콘을 준비 직전에 물린다 — 예전
        // PreparePipelineFrame의 feed와 같은 시점(PrepareAll)이다.
        node.prepare = [bundle, gizmoScene](const EnhancedFrameContext& ctx,
            std::string& err, uint32_t) -> bool
        {
            if (nullptr != gizmoScene)
            {
                bundle->gizmoIcon.SetIcons(&gizmoScene->icons);
            }
            return bundle->gizmoIcon.PrepareFrame(ctx, err);
        };
        node.declare = [bundle](LiveBlackboard& bb, EnhancedRenderGraph& graph,
            const EnhancedFrameContext& ctx, const LiveFrameBinding& binding)
        {
            if (0 == (binding.viewFlags & LiveViewFlags::kSceneOverlay)) return;

            EnhancedGizmoIconPass::Inputs inputs{};
            inputs.color = bb.Get(LiveSlots::kDisplayLdr);
            bundle->gizmoIcon.SetInputs(inputs);
            bundle->gizmoIcon.Declare(graph, ctx);
            if (bundle->gizmoIcon.GetOutput().IsValid())
            {
                bb.Set(LiveSlots::kDisplayLdr, bundle->gizmoIcon.GetOutput());
            }
        };
        pipeline.AddNode(std::move(node));
    }

    {
        LivePassNode node;
        node.name = "GizmoLine";
        node.instance = [bundle](uint32_t) -> EnhancedRenderPass*
        {
            return &bundle->gizmoLine;
        };
        node.modifies = { LiveSlots::kDisplayLdr };
        node.prepare = [bundle, gizmoScene](const EnhancedFrameContext& ctx,
            std::string& err, uint32_t) -> bool
        {
            if (nullptr != gizmoScene)
            {
                bundle->gizmoLine.SetVertices(gizmoScene->lineVertices);
            }
            return bundle->gizmoLine.PrepareFrame(ctx, err);
        };
        node.declare = [bundle](LiveBlackboard& bb, EnhancedRenderGraph& graph,
            const EnhancedFrameContext& ctx, const LiveFrameBinding& binding)
        {
            if (0 == (binding.viewFlags & LiveViewFlags::kSceneOverlay)) return;

            EnhancedGizmoLinePass::Inputs inputs{};
            inputs.color = bb.Get(LiveSlots::kDisplayLdr);
            bundle->gizmoLine.SetInputs(inputs);
            bundle->gizmoLine.Declare(graph, ctx);
            if (bundle->gizmoLine.GetOutput().IsValid())
            {
                bb.Set(LiveSlots::kDisplayLdr, bundle->gizmoLine.GetOutput());
            }
        };
        pipeline.AddNode(std::move(node));
    }
}
