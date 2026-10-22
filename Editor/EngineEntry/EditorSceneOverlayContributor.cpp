#include "EditorSceneOverlayContributor.h"

#include "EnhancedGizmoSceneBinding.h"
#include "GizmoRenderer.h"
#include "Render/Core/EnhancedLivePipelineDesc.h"
#include "Render/Passes/Editor/EnhancedGridPass.h"
#include "Render/Passes/Editor/EnhancedWireFramePass.h"
#include "Render/Passes/Editor/EnhancedGizmoIconPass.h"
#include "Render/Passes/Editor/EnhancedGizmoLinePass.h"

#include "../../Engine/Utility_Framework/StandardLibrary.h"

namespace
{
    // 그리드가 만드는 깊이. 와이어프레임이 같은 깊이로 가려져야 한다.
    // 이 슬롯은 기여 노드끼리만 잇는다 — Core 어휘(LiveSlots)에 두지 않는다.
    constexpr const char* kGizmoDepthSlot = "Gizmo.Depth";
    constexpr LiveResourceSlot<LiveResourceSemantic::Depth> kGizmoDepth{kGizmoDepthSlot, RHIFormat::D32Float,
                                                                        LiveResourceExtent::Display, true};

    /// 한 뷰 실행 슬롯에 딸린 패스 묶음. 노드 람다들이 shared_owner로
    /// 붙들어 desc와 수명을 같이한다 — desc가 노드를 놓으면(ShutdownAll 뒤
    /// Clear) 묶음도 함께 사라진다.
    struct EditorOverlayPassBundle
    {
        EnhancedGridPass      grid;
        EnhancedWireFramePass wireframe;
        EnhancedGizmoIconPass gizmoIcon;
        EnhancedGizmoLinePass gizmoLine;
        FrameCameraSnapshot displayCamera;
        EnhancedFrameContext displayContext;
        bool wireframeEnabled{};

        const EnhancedFrameContext& DisplayContext(const EnhancedFrameContext& input)
        {
            displayContext = input;
            displayContext.width = input.displayWidth ? input.displayWidth : input.width;
            displayContext.height = input.displayHeight ? input.displayHeight : input.height;
            if (input.camera)
            {
                displayCamera = *input.camera;
                // Reconstruction removes raster jitter. Authoring overlays must
                // use the same stable projection as the reconstructed image.
                if (input.temporalFrame.realFrameId && input.width && input.height)
                {
                    for (unsigned row = 0; row < 4; ++row)
                    {
                        displayCamera.projection.m[row][0] -= 2.f * input.temporalFrame.jitterX / float(input.width) * input.camera->projection.m[row][3];
                        displayCamera.projection.m[row][1] += 2.f * input.temporalFrame.jitterY / float(input.height) * input.camera->projection.m[row][3];
                    }
                    displayCamera.inverseProjection = math::inverse(displayCamera.projection);
                }
                displayContext.camera = &displayCamera;
            }
            return displayContext;
        }
    };

    struct EditorOverlayPassStorage
    {
        std::vector<own::shared_owner<EditorOverlayPassBundle>> views;

        EditorOverlayPassBundle& Get(uint32_t viewIndex) const { return *views.at(viewIndex); }
    };
}

void EditorSceneOverlayContributor::Contribute(LivePipelineDesc& pipeline,
    const RenderFeatureContext& context)
{
    if (!context.viewCount)
    {
        throw std::invalid_argument("Editor overlays require at least one view execution slot.");
    }
    own::shared_owner<EditorOverlayPassStorage> bundles = own::make_shared<EditorOverlayPassStorage>();
    bundles->views.reserve(context.viewCount);
    for (uint32_t view = 0; view < context.viewCount; ++view)
    {
        own::shared_owner<EditorOverlayPassBundle> bundle = own::make_shared<EditorOverlayPassBundle>();
        bundle->grid.SetOutputFormat(context.ldrFormat);
        bundle->wireframe.SetOutputFormat(context.ldrFormat);
        bundle->gizmoIcon.SetOutputFormat(context.ldrFormat);
        bundle->gizmoLine.SetOutputFormat(context.ldrFormat);
        bundles->views.push_back(std::move(bundle));
    }
    const EnhancedGizmoSceneData* fallbackGizmoScene = context.gizmoScene;
    const auto gizmoSceneForView = context.gizmoSceneForView;

    // ── 기즈모 체인 — dx12.gizmoscene과 같은 Grid → WireFrame → Icon → Line 순서.
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
        node.perView = true;
        node.requiredViewFlags = LiveViewFlags::kSceneOverlay;
        node.name = "Grid";
        node.prepare = [bundles](const EnhancedFrameContext& ctx, std::string& err, uint32_t viewIndex)
        {
            EditorOverlayPassBundle* bundle = &bundles->Get(viewIndex);
            return bundle->grid.PrepareFrame(bundle->DisplayContext(ctx), err);
        };
        node.instance = [bundles](uint32_t viewIndex) -> EnhancedRenderPass*
        {
            EditorOverlayPassBundle* bundle = &bundles->Get(viewIndex);
            return &bundle->grid;
        };
        node.Inputs({LiveResources::kGBufferDepth});
        node.Updates({LiveResources::kDisplayLdr});
        node.Outputs({kGizmoDepth});
        node.declare = [bundles](LiveBlackboard& bb, EnhancedRenderGraph& graph, const EnhancedFrameContext& ctx,
                                 const LiveFrameBinding& binding)
        {
            EditorOverlayPassBundle* bundle = &bundles->Get(binding.viewIndex);

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
        node.perView = true;
        node.requiredViewFlags = LiveViewFlags::kSceneOverlay;
        node.name = "WireFrame";
        node.prepare = [bundles](const EnhancedFrameContext& ctx, std::string& err, uint32_t viewIndex) -> bool
        {
            EditorOverlayPassBundle* bundle = &bundles->Get(viewIndex);
            auto input = bundle->DisplayContext(ctx);
            const auto* gizmoRenderer = GizmoRenderer::GetActive();
            bundle->wireframeEnabled = gizmoRenderer && gizmoRenderer->IsWireFrameEnabled();
            if (!bundle->wireframeEnabled)
            {
                input.graphSceneInput.reset();
                input.draws = nullptr;
                input.forwardDraws = nullptr;
            }
            return bundle->wireframe.PrepareFrame(input, err);
        };
        node.instance = [bundles](uint32_t viewIndex) -> EnhancedRenderPass*
        {
            EditorOverlayPassBundle* bundle = &bundles->Get(viewIndex);
            return &bundle->wireframe;
        };
        node.Updates({LiveResources::kDisplayLdr, kGizmoDepth});
        node.declare = [bundles](LiveBlackboard& bb, EnhancedRenderGraph& graph, const EnhancedFrameContext& ctx,
                                 const LiveFrameBinding& binding)
        {
            EditorOverlayPassBundle* bundle = &bundles->Get(binding.viewIndex);

            // 그리드가 이 뷰에서 깊이를 발행하지 않았으면(비활성 등)
            // 와이어도 서지 않는다.
            if (!bundle->wireframeEnabled || !bb.Get(kGizmoDepthSlot).IsValid())
            {
                return;
            }

            EnhancedWireFramePass::Inputs inputs{};
            inputs.color = bb.Get(LiveSlots::kDisplayLdr);
            inputs.depth = bb.Get(kGizmoDepthSlot);
            bundle->wireframe.SetInputs(inputs);
            bundle->wireframe.Declare(graph, ctx);
            if (bundle->wireframe.GetOutput().IsValid())
            {
                bb.Set(LiveSlots::kDisplayLdr, bundle->wireframe.GetOutput());
            }
            if (bundle->wireframe.GetDepth().IsValid())
            {
                bb.Set(kGizmoDepthSlot, bundle->wireframe.GetDepth());
            }
        };
        pipeline.AddNode(std::move(node));
    }

    {
        LivePassNode node;
        node.perView = true;
        node.requiredViewFlags = LiveViewFlags::kSceneOverlay;
        node.name = "GizmoIcon";
        node.instance = [bundles](uint32_t viewIndex) -> EnhancedRenderPass*
        {
            EditorOverlayPassBundle* bundle = &bundles->Get(viewIndex);
            return &bundle->gizmoIcon;
        };
        node.Updates({LiveResources::kDisplayLdr});
        // 프레임마다 갱신되는 아이콘을 준비 직전에 물린다 — 예전
        // PreparePipelineFrame의 feed와 같은 시점(PrepareAll)이다.
        node.prepare = [bundles, gizmoSceneForView, fallbackGizmoScene](const EnhancedFrameContext& ctx,
                                                                        std::string& err, uint32_t viewIndex) -> bool
        {
            EditorOverlayPassBundle* bundle = &bundles->Get(viewIndex);
            const EnhancedGizmoSceneData* gizmoScene =
                gizmoSceneForView ? gizmoSceneForView(viewIndex) : fallbackGizmoScene;
            bundle->gizmoIcon.SetIcons(gizmoScene ? &gizmoScene->icons : nullptr);
            return bundle->gizmoIcon.PrepareFrame(bundle->DisplayContext(ctx), err);
        };
        node.declare = [bundles](LiveBlackboard& bb, EnhancedRenderGraph& graph, const EnhancedFrameContext& ctx,
                                 const LiveFrameBinding& binding)
        {
            EditorOverlayPassBundle* bundle = &bundles->Get(binding.viewIndex);

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
        node.perView = true;
        node.requiredViewFlags = LiveViewFlags::kSceneOverlay;
        node.name = "GizmoLine";
        node.instance = [bundles](uint32_t viewIndex) -> EnhancedRenderPass*
        {
            EditorOverlayPassBundle* bundle = &bundles->Get(viewIndex);
            return &bundle->gizmoLine;
        };
        node.Updates({LiveResources::kDisplayLdr});
        node.prepare = [bundles, gizmoSceneForView, fallbackGizmoScene](const EnhancedFrameContext& ctx,
                                                                        std::string& err, uint32_t viewIndex) -> bool
        {
            EditorOverlayPassBundle* bundle = &bundles->Get(viewIndex);
            const EnhancedGizmoSceneData* gizmoScene =
                gizmoSceneForView ? gizmoSceneForView(viewIndex) : fallbackGizmoScene;
            if (gizmoScene)
            {
                bundle->gizmoLine.SetVertices(gizmoScene->lineVertices);
            }
            else
            {
                bundle->gizmoLine.SetVertices({});
            }
            return bundle->gizmoLine.PrepareFrame(bundle->DisplayContext(ctx), err);
        };
        node.declare = [bundles](LiveBlackboard& bb, EnhancedRenderGraph& graph, const EnhancedFrameContext& ctx,
                                 const LiveFrameBinding& binding)
        {
            EditorOverlayPassBundle* bundle = &bundles->Get(binding.viewIndex);

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
