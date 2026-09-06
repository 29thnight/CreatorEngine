// LC6 (PHASE 14.5) — Renderer 라이브 조회·조정 명령.
//
// 도는 렌더러의 상태를 읽거나 바꾼다. 격리 씬을 세우지 않고, 산출물을 남기지
// 않고, 켜져 있는 에디터에 붙어 그 자리에서 답한다.
//
//   dx12.live          EnhancedRenderer 런타임 상태
//   pipeline.nodes     라이브 파이프라인의 노드 조립 결과
//   render.rtinfo      창·뷰포트·추종 텍스처 크기
//   render.shadowinfo  그림자 캐스케이드 계산 결과
//   render.matmode     활성 씬 재질의 렌더링 모드를 **바꾼다**
//   render.backend     부팅 시 고정된 RHI 조회(변경은 Settings — 재시작이 필요하다)
//
// ── 왜 이름 접두사가 아니라 여기서 갈랐는가 ─────────────────────────────
//
// 처음에는 `dx12.*`·`vk.*`·`render.*` 를 접두사로 묶어 한 파일에 넣었다. 그것은
// **효과가 어디서 나느냐**와 아무 상관이 없는 축이라, 활성 씬의 Material 을
// 바꾸는 `render.matmode` 와 격리 씬을 세워 PNG 를 남기는 `dx12.gbuffer` 가 같은
// 파일에 앉았다.
//
// 실측으로 갈랐다. 켜져 있는 에디터에 HTTP 로 64 개를 붙여 본 결과 61 개가
// 200 이었고(연속 호출), 코드 모양도 같은 선에서 갈렸다 — 56 개는
// `const bool passed = RunXTest(log)` 형태의 검증 프로브였고, 그 형태를 벗어난
// 8 개가 정확히 이 파일의 것들이다. 의미로 나눈 선과 코드 모양으로 나눈 선이
// 같았다.
//
// ── ★ `render.backend` 가 읽기 전용인 이유 ──────────────────────────────
//
// RHI 는 부팅 때 고정되고 변경은 Settings 를 거쳐 **재시작해야** 반영된다.
// 그래서 이 명령은 조회만 한다. 그 사실이 주석에만 있으면 호출자는 알 길이
// 없으므로 descriptor 의 재시작 축에 적는다.
//
// ── ★★ `render.post` 는 **제거했다** ────────────────────────────────────
//
// 본문이 "비활성화됨" 한 줄을 찍고 끝나는데 등록은 살아 있어 help 와
// `commands.list` 에 정상 명령으로 실려 있었다. 소비자는 그것이 동작한다고
// 믿을 근거를 표에서 얻는다.
//
// ★ 실제로 두 하네스가 믿고 있었다. `Tools/featuretest/run-featuretests.ps1`
//   과 `Tools/showcase/Start-MaterialShowcase.ps1` 이 캡처 직전에
//   `render.post fog off` 를 걸고 있었고, README 는 "포그를 끈다"고 적었으며
//   끄고 싶지 않은 사람을 위한 `-KeepFog` 플래그까지 있었다. 아무것도 안
//   꺼지고 있었다 — 포그가 최종 색의 85% 를 덮은 채로 기능 확인용 그림이
//   전부 찍혀 왔다는 뜻이다.
//
//   명령을 지우면서 그 호출부와 문서도 사실에 맞췄다. 런타임 포그 토글은
//   Enhanced PostChain 튜닝 API 가 생겨야 가능하고, 그것은 이 슬라이스가
//   할 일이 아니다. 없는 기능을 있는 것처럼 적어 두지 않는 것이 지금 할 일이다.

#include "CommandRegistrar.h"
#include "CommandSupport.h"
#include "EditorObjectOperations.h"
#include "CommandCore/CommandSession.h" // LC1: 결과 누적과 process exit code
#include "CommandCore/CommandParser.h"
#include "CommandCore/CommandRegistry.h"       // LC3: descriptor snapshot
#include "CommandCore/CommandDescriptorSeeds.h"
#include "EditorCommandServiceHost.h"        // LC4: 로컬 HTTP/JSON 서비스  // LC2: 토크나이저와 소유형 invocation
#include "EditorCameraRig.h"
#include "EditorSessionState.h"
#include "EngineBootstrap.h"
#include "GameBuilderSystem.h"
#include "EditorAssetDatabase.h"
#include "Interfaces/AssetAuthoringPort.h"
#include "Interfaces/FoliageInstance.h"
#include <mathematics/color.hpp>
#include "SceneManager.h"
#include "Scene.h"
#include "CameraComponent.h"
#include "CameraSystem.h"
#include "ClrHost.h"
#include "ScriptComponent.h"
#include "PrefabUtility.h"
#include "ComponentFactory.h"
#include "ModelSceneInstantiation.h" // MBC9: generation 씬 인스턴스화
#include "ModelConsumptionDiagnostics.h" // MBC10: 읽기 전용 소비 스냅샷
#include "Material.h"
#include "Mesh.h"
#include "Assets/ModelAssetGeneration.h"
#include "Assets/ModelVertexLayout.h"    // MBC9: skinbounds typed 정점 디코드
#include "Assets/ModelAnimationSampler.h" // MBC9: editorsurface frame 축(CountUniqueKeyTimes)
#include "Assets/ModelAssetAuthoringTransaction.h" // MBC11: assets.modelbench author 모드
#include "RHI/IRHIDeviceResources.h"                // MBC11: VRAM 계측
#include "LifecycleTrace.h"
#include "LifecycleRegistry.h"
#include "Animator.h"
#include "Socket.h" // X7 transform bulk probe
#include "BoneRegion.h" // MAX_BONES
#include "Experiment/Model.h" // I5-D4e-1: experiment.animtick 패리티
#include "RenderScene.h"      // I5-D4e-1: GetAnimationJob
#include "AvatarMask.h"       // I5-D4e-3: experiment.animmask A/B 대조
#include "FoliageComponent.h"      // I5-D5a: experiment.foliage 게이트
#include "Terrain.h"               // D4 Terrain YAML authoring round-trip
#include "Experiment/MaterialInstance.h"      // I5-D5c1: experiment.matruntime
#include "Experiment/MaterialAuthoringCodec.h" // I5-D5c1: 값 인코딩 대조
#include "ExperimentMaterialMigration.h"      // I5-D5c1: legacy 왕복 축
#include "Experiment/Cooked/CookedAssetCatalog.h"  // I7-C1
#include "ExperimentMaterialResolveBinding.h"       // I7-C1: 제품 resolver
#include "StandardMaterialProperty.h"              // I7-C1: probe property
#include "Experiment/MaterialPropertyBlock.h"  // I5-D5c2-1: packing 바이트 축
#include "MaterialPropertyPacker.h"           // I5-D5c2-1: 합성 layout
#include "PrimitiveRenderProxy.h"           // I5-D5c2-2: 프록시 축
#include "MaterialScriptBinding.h"          // I5-D5c3: 실물 편집 창구
#include "ProxyCommandQueue.h"             // I5-D5c3: 갱신 커맨드 소비
#include "Render/Scene/ExperimentMaterialSealing.h" // I5-D5c3-2: texture 축
#include "PrimitiveRenderProxy.h"  // I5-D5a: FoliageRenderProxy 실물 사슬
#include "RHI/IRenderDeviceServices.h" // RHIModelMeshView·BuildRHIModelMeshView
#include "ConditionParameter.h"
#include "UIManager.h"
#include "Canvas.h"
#include "ImageComponent.h"
#include "MeshRenderer.h" // X8 render proxy dirty probe
#include "RectTransformComponent.h"
#include "BoneComponent.h" // E7-b: scene.traversalbench 0 모드의 마커 보유 수 진단
#include "UIButton.h"
#include "TextComponent.h"
#include "SpriteSheetComponent.h"
#include "StateMachineComponent.h"
#include "AIManager.h"
#include "DataSystem.h"
#include "GpuDiagnostics.h"
#include "LogSystem.h"
#include "PathFinder.h"
#include "RuntimeSettings.h"
#include "AuthoringNodeEquality.h" // D3-a-1: 저작 노드 구조 비교
#include "AuthoringNodeViewAccess.h" // D3-a-5b
#include "AuthoringParsedDocument.h"
#include "AuthoringRymlErrorPolicy.h" // D3-b-1: ryml abort → 예외 정책
#include "SerializationProfiler.h" // D0(SerializationPlan): 직렬화 기준선 계측
#include "CoreWindow.h"
#include "Render/Scene/EnhancedSceneRenderer.h"
#include "RHI/DX12/Tests/DX12SelfTest.h"
#include "RHI/Vulkan/VulkanSelfTest.h"
#include "RHI/IImGuiHost.h"
#include "ProfilerSelfTest.h"
#include "ExperimentParity/ExperimentVertexLayoutSelfTest.h"
#include "AssetIdentity/AssetIdentitySelfTest.h"
#include "AssetIdentity/AssetSidecarSchemaSelfTest.h"
#include "AssetIdentity/ModelAssetGenerationSelfTest.h"
#include "AssetIdentity/SceneModelGenerationSelfTest.h"
#include "ExperimentParity/ExperimentSamplerSelfTest.h"
#include "ExperimentParity/ExperimentCookedSelfTest.h"
#include "ExperimentParity/ExperimentWeldSelfTest.h"
#include "ExperimentParity/ExperimentCacheOptSelfTest.h"
#include "ExperimentParity/ExperimentTextureCookSelfTest.h"
#include "ShaderMeta.h"
#include "ExperimentParity/ExperimentShaderMetaCookSelfTest.h"
#include "ExperimentParity/ExperimentMaterialCookSelfTest.h"
#include "ExperimentParity/ExperimentMaterialInstanceSelfTest.h"
#include "ExperimentParity/ExperimentMaterialSealSelfTest.h"
#include "ExperimentParity/ExperimentMaterialCodecSelfTest.h"
#include "ExperimentParity/ExperimentSceneCookSelfTest.h"
#include "ExperimentParity/ExperimentResolverSelfTest.h"
#include "ExperimentParity/ExperimentCatalogSelfTest.h"
#include "RHI/ScreenSizedResource.h"
#include "ReflectionYml.h"
#include "ReflectionUndo.h"
#include "GameObjectCommand.h"
#include "StringHelper.h"
#include "BlackBoard.h"
#include "TagManager.h"
#include <Windows.h>
#include <psapi.h> // MBC11: assets.modelbench peak working set
#include <crtdbg.h>
#include <algorithm>
#include <atomic>
#include <cctype>
#include <limits>
#include <random>
#include <stdexcept>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <DbgHelp.h>
#include <DXProgrammableCapture.h>
#include <chrono>
#include <dxgidebug.h>
#include <wrl/client.h>
#include <algorithm>
#include <cstring>
#include <cstdio>
#include <fstream>
#include <functional>
#include "../../Engine/SceneRuntime/MeshRenderer.h"
#include "../../Engine/RenderEngine/Material.h"
#include <unordered_set>
#include <iostream>
#include <sstream>
#include <stdexcept>

namespace ConsoleCmd
{
	CommandCore::CommandResult Cmd_light_proxy(const ConsoleCommandContext& ctx)
	{
		using namespace CommandCore;
		if (ctx.parts.size() != 1) return InvalidArguments("light.proxy");
		auto data = CommandData::Object();
		auto rows = CommandData::Array();

		RenderScene* renderScene = SceneManagers->GetRenderScene();
		if (nullptr == renderScene)
		{
			std::printf("[light.proxy] renderScene=none\n");
			return PreconditionFailed("scene.missing", "No render scene");
		}

		const auto proxies = renderScene->GetLightProxySnapshot();

		// 커밋 누계를 함께 찍는다. 값이 안 바뀌었을 때 "발행이 없었나(publish=0)"와
		// "발행은 됐는데 안 실렸나(publish>0 · committed 그대로)"를 갈라 준다.
		// 앞은 writer가 dirty를 안 냈다는 뜻이고, 뒤는 등록이 없어 stale로
		// 버려졌다는 뜻이라 고칠 자리가 다르다.
		// 커밋 누계와 프록시 커맨드 큐를 함께 찍는다. 값이 안 바뀌었을 때
		// 세 자리를 갈라 준다.
		//
		//   publish=0                      writer가 dirty를 안 냈다
		//   publish>0 · committed 그대로    등록이 없어 stale로 버려졌다
		//   committed 늘고 applied 안 늘면  커맨드가 소비되지 않았다
		//                                  (렌더 스레드가 안 도는 헤드리스가 그렇다)
		//
		// 셋은 고칠 자리가 전혀 다르다. 값만 보고 첫째로 단정하기 쉬운데,
		// 실제로 물린 것은 셋째였다.
		if (Scene* scene = SceneManagers->GetActiveScene())
		{
			const RenderProxyCommitMetrics m = scene->GetRenderProxyCommitMetrics();
			const auto q = ProxyCommandQueue->GetStats();
			data.Set("publish", CommandData::Int(m.publishCalls));
			data.Set("committed", CommandData::Int(m.committed));
			data.Set("pending", CommandData::Int(m.pending));
			data.Set("queued", CommandData::Int(q.enqueued));
			data.Set("applied", CommandData::Int(q.applied));
			data.Set("dropped", CommandData::Int(q.dropped));
			data.Set("stale", CommandData::Int(q.staleEpoch));
			data.Set("superseded", CommandData::Int(q.superseded));

			std::printf("[light.proxy] count=%zu publish=%llu committed=%llu pending=%llu"
				" queued=%llu applied=%llu dropped=%llu stale=%llu superseded=%llu\n",
				proxies.size(),
				(unsigned long long)m.publishCalls,
				(unsigned long long)m.committed,
				(unsigned long long)m.pending,
				(unsigned long long)q.enqueued,
				(unsigned long long)q.applied,
				(unsigned long long)q.dropped,
				(unsigned long long)q.staleEpoch,
				(unsigned long long)q.superseded);
		}
		else
		{
			std::printf("[light.proxy] count=%zu (활성 씬 없음 — 커밋 누계 생략)\n",
				proxies.size());
		}

		size_t index = 0;
		for (const auto& proxy : proxies)
		{
			if (nullptr == proxy) { ++index; continue; }

			std::printf(
				"[light.proxy] #%zu intensity=%.3f range=%.3f spot=%.3f type=%d status=%d"
				" color=(%.3f,%.3f,%.3f,%.3f)\n",
				index, proxy->m_intensity, proxy->m_range, proxy->m_spotLightAngle,
				proxy->m_lightType, proxy->m_lightStatus,
				proxy->m_color.r, proxy->m_color.g, proxy->m_color.b, proxy->m_color.a);
            auto row = CommandData::Object();
            row.Set("index", CommandData::Int(index));
            row.Set("intensity", CommandData::Double(proxy->m_intensity));
            row.Set("range", CommandData::Double(proxy->m_range));
            row.Set("spot", CommandData::Double(proxy->m_spotLightAngle));
            row.Set("type", CommandData::Int(proxy->m_lightType));
            row.Set("status", CommandData::Int(proxy->m_lightStatus));
            auto color = CommandData::Array();
            for (float value : {proxy->m_color.r, proxy->m_color.g, proxy->m_color.b, proxy->m_color.a}) color.Append(CommandData::Double(value));
            row.Set("color", std::move(color)); rows.Append(std::move(row));
            ++index;
		}
        data.Set("count", CommandData::Int(proxies.size()));
        data.Set("lights", std::move(rows));
        return Ok("Light proxy snapshot", std::move(data));
	}

    static CommandCore::CommandResult Cmd_render_matmode(const ConsoleCommandContext& ctx)
    {
        using namespace CommandCore;
        if (ctx.parts.size() != 3 || (ctx.parts[2] != "opaque" && ctx.parts[2] != "transparent"))
            return InvalidArguments("render.matmode <object> <opaque|transparent>");
        EntityHandle target;
        auto result = EditorObjectOperations::ResolveTarget(ctx.parts[1], target);
        if (!result.IsSuccess()) return result;
        return EditorObjectOperations::MaterialMode(target, ctx.parts[2] == "opaque" ? MaterialRenderingMode::Opaque : MaterialRenderingMode::Transparent);
    }

    static CommandCore::CommandResult Cmd_render_backend(const ConsoleCommandContext& ctx)
    {
        using namespace CommandCore;
        if (ctx.parts.size() > 2) return InvalidArguments("render.backend [status]");
        if (ctx.parts.size() == 2 && ctx.parts[1] != "status")
            return InvalidArguments("Backend is fixed at startup; configure Settings and restart", "render.backend_fixed");
        auto data = CommandData::Object();
        data.Set("configured", CommandData::String(RenderBackendName(RuntimeSettings::Get().GetRenderBackend())));
        data.Set("scene", CommandData::String(EnhancedLiveBackend::Vulkan == EnhancedSceneRenderer::GetLiveBackend() ? "vulkan" : "dx12"));
        data.Set("imgui", CommandData::String(GetImGuiHost().GetBackendName()));
        data.Set("status", CommandData::String(EnhancedSceneRenderer::GetLiveStatus()));
        return Ok({}, std::move(data));
    }

    static CommandCore::CommandResult Cmd_dx12_live(const ConsoleCommandContext& ctx)
    {
        using namespace CommandCore;
        const std::string mode = ctx.parts.size() >= 2 ? ctx.parts[1] : "status";
        if (ctx.parts.size() > 2 || (mode != "on" && mode != "status"))
            return InvalidArguments("dx12.live [on|status]; the main renderer cannot be disabled");
        if (mode == "on") EnhancedSceneRenderer::EnableLive();
        const auto snapshot = EnhancedSceneRenderer::GetLiveDebugSnapshot();
        auto data = CommandData::Object();
        data.Set("enabled", CommandData::Bool(snapshot.enabled));
        data.Set("ready", CommandData::Bool(snapshot.pipelineReady));
        data.Set("backend", CommandData::String(snapshot.backend == EnhancedLiveBackend::Vulkan ? "vulkan" : "dx12"));
        data.Set("status", CommandData::String(EnhancedSceneRenderer::GetLiveStatus()));
        return Ok(mode == "on" ? "Renderer enable requested" : "", std::move(data));
    }

    static CommandCore::CommandResult Cmd_render_rtinfo(const ConsoleCommandContext& ctx)
    {
        using namespace CommandCore;
        if (ctx.parts.size() != 1) return InvalidArguments("This command takes no arguments");
        auto data = CommandData::Object();

        // 화면 크기와 그것을 따라가는 텍스처들을 나란히 찍는다.
        //
        // ★ 예전에는 '클라이언트 · 뷰포트 · 버스' 셋을 비교했다. 셋이 어긋나는
        //   것이 '화면이 구석에 몰린다'의 정체였기 때문인데, 앞의 둘은 DX11
        //   전역이었고 D4에서 사라졌다. 이제 출처가 버스 하나라 어긋날 자리가
        //   없다 - 비교 대신 아래 명부(따라가야 하는데 안 따라간 텍스처)가
        //   같은 질문에 답한다.
        std::string report;
        {
            char line[224]{};
            std::snprintf(line, sizeof(line), "화면 %ux%u\n",
                ScreenResizeBus::Get().GetWidth(), ScreenResizeBus::Get().GetHeight());
            report += line;
        }

        // ★ 카메라별 DX11 렌더타깃·깊이 크기 리포트를 걷었다 (T6, 2026-08-08).
        //   RenderPassData가 들던 그 둘의 마지막 소비자가 이 진단이었고,
        //   실제로 그리는 쪽은 이미 0이었다(EffectSystem이 마지막이었는데
        //   PHASE 10-0에서 사라졌다). 아래 명부가 화면 추종 텍스처 전부를
        //   훑으므로 관측이 줄지도 않는다.

        // 화면 추종을 선언한 텍스처 전부. 카메라 렌더 타깃만 보면 GBuffer나
        // 포스트 체인이 어긋난 것을 놓친다 — 그것들은 중간 결과라 화면에
        // 직접 보이지 않는다.
        const uint32_t screenWidth = ScreenResizeBus::Get().GetWidth();
        const uint32_t screenHeight = ScreenResizeBus::Get().GetHeight();

        const auto entries = ScreenSizedRegistry::Get().Snapshot();
        auto textures = CommandData::Array();
        uint32_t mismatched = 0;
        std::string mismatchReport;

        for (const auto& entry : entries)
        {
            if (!entry.querySize) continue;

            const auto [width, height] = entry.querySize();

            // 1/N 버퍼도 있으므로 '화면 크기와 다르다'만으로는 못 잡는다.
            // 화면을 정수로 나눈 값 중 하나면 정상으로 본다.
            bool plausible = false;
            // SSGI가 1/16까지 쓴다(ssratio 4의 4배). 상한을 그보다 낮게 잡으면
            // 정상인 것을 어긋난 것으로 센다.
            for (uint32_t divisor = 1; divisor <= 16; ++divisor)
            {
                if (width == (screenWidth / divisor) && height == (screenHeight / divisor))
                {
                    plausible = true;
                    break;
                }
            }

            auto item = CommandData::Object();
            item.Set("name", CommandData::String(entry.name)); item.Set("width", CommandData::Int(width)); item.Set("height", CommandData::Int(height)); item.Set("plausible", CommandData::Bool(plausible));
            textures.Append(std::move(item));
            if (!plausible)
            {
                ++mismatched;
                char line[224]{};
                std::snprintf(line, sizeof(line), "    %-34s %ux%u\n",
                    entry.name.c_str(), width, height);
                mismatchReport += line;
            }
        }

        {
            char line[160]{};
            std::snprintf(line, sizeof(line),
                "  추종 선언 텍스처 %zu개 · 화면과 어긋난 것 %u개\n",
                entries.size(), mismatched);
            report += line;
        }
        report += mismatchReport;

        Debug->LogWarning("[렌더 타깃]\n" + report);
        std::printf("[CLI] 렌더 타깃\n%s", report.c_str());
            data.Set("width", CommandData::Int(screenWidth)); data.Set("height", CommandData::Int(screenHeight));
        data.Set("textures", std::move(textures)); data.Set("registered", CommandData::Int(entries.size())); data.Set("mismatched", CommandData::Int(mismatched));
        return Ok({}, std::move(data));
    }

    // ★ `render.exposure` 를 지웠다(2026-09-05). 몸통이 **printf 한 줄**이었다 —
    //   "DX11 레거시 진단은 비활성화됨" 이라는 안내만 하고 아무것도 재지 않았다.
    //   그런데 registry 요약은 "자동 노출이 무엇을 재고 무엇을 결정했는지" 라고
    //   적혀 있어, `commands.list` 를 읽는 쪽에는 계측 명령으로 보였다.
    //
    //   DX12 계측이 붙으면 그때 명령을 새로 만든다. 그때까지 "있는데 아무것도
    //   안 하는 것" 보다 "없는 것" 이 정직하다 — 앞서 같은 이유로 지운
    //   `render.post` 와 같은 처분이다.

    static CommandCore::CommandResult Cmd_pipeline_nodes(const ConsoleCommandContext& ctx)
    {
        using namespace CommandCore;
        if (ctx.parts.size() != 1) return InvalidArguments("pipeline.nodes takes no arguments");
        const auto snapshot = EnhancedSceneRenderer::GetLiveDebugSnapshot();
        if (!snapshot.enabled) return PreconditionFailed("render.unavailable", "Renderer is disabled");
        auto data = CommandData::Object();
        auto nodes = CommandData::Array();
        const auto strings = [](const auto& values) { auto a = CommandData::Array(); for (const auto& v : values) a.Append(CommandData::String(v)); return a; };
        for (const auto& node : snapshot.pipelineNodes)
        {
            auto d = CommandData::Object();
            d.Set("name", CommandData::String(node.name));
            d.Set("conditional", CommandData::Bool(node.conditional)); d.Set("active", CommandData::Bool(node.active));
            d.Set("perView", CommandData::Bool(node.perView));
            d.Set("reads", strings(node.reads)); d.Set("writes", strings(node.writes)); d.Set("modifies", strings(node.modifies));
            nodes.Append(std::move(d));
            std::printf("[pipeline.node] %s|%s\n", node.name.c_str(), node.conditional ? (node.active ? "active" : "inactive") : "always");
        }
        data.Set("nodes", std::move(nodes));
        data.Set("count", CommandData::Int(snapshot.pipelineNodes.size()));
        data.Set("valid", CommandData::Bool(snapshot.pipelineDescriptionValid));
        data.Set("ready", CommandData::Bool(snapshot.pipelineReady));
        return Ok({}, std::move(data));
    }

    // ── Undo/선택 프로브 (E3-2+3 게이트용) ──
    //
    // 이 셋이 없어서 "재생 진입이 Undo 이력을 실제로 비웠는가", "정지가 선택을
    // 어떻게 하는가"를 잴 수 없었다. 세트 전체에 selection/undo 단정이 0건이었다.
    //
    // ⚠ 편집 스택과 게임 스택을 **따로** 찍는다. UndoManager가 어느 쪽에 넣을지
    //   고르는 기준인 m_isGameMode는 이름과 달리 "에디터 UI의 Play 버튼을 눌렀는가"라
    //   저장소 전체에서 MenuBarWindow 한 줄만 쓴다 — CLI로 재생하면 영원히 false다.
    //   "지금 유효한 스택" 하나만 찍으면 CLI 게이트가 편집 스택을 보면서 게임 스택을
    //   검사한다고 착각한다. 그 착각이 곧 아무것도 검증하지 않는 게이트다.

    static CommandCore::CommandResult Cmd_render_shadowinfo(const ConsoleCommandContext& ctx)
    {
        using namespace CommandCore;
        if (ctx.parts.size() != 1) return InvalidArguments("This command takes no arguments");
        auto data = CommandData::Object();
        // 카메라 입력은 RenderPassData가 아니라 프레임 패킷의 값 스냅샷이다.
        // 이 명령은 활성 씬의 저작 카메라를 같은 방식으로 밀봉해 입력을 확인한다.
        auto cameraData = CommandData::Array();
        char line[512]{};
        std::string report;
        Scene* activeScene = SceneManagers->GetActiveScene();

        const std::vector<CameraComponent*>* cameras = nullptr != activeScene
            ? &activeScene->Cameras().GetRegisteredCameras() : nullptr;
        if (nullptr != cameras) for (CameraComponent* camera : *cameras)
        {
            if (nullptr == camera || nullptr == camera->GetOwner()) continue;
            if (camera->GetOwner()->GetScene() != activeScene) continue;

            const FrameCameraSnapshot snapshot = camera->CaptureFrameSnapshot();
            auto item = CommandData::Object();
            item.Set("componentId", CommandData::Int(camera->GetInstanceID())); item.Set("primary", CommandData::Bool(camera->IsPrimary()));
            const auto vector = [](const auto& v) { auto a = CommandData::Array(); a.Append(CommandData::Double(v.x)); a.Append(CommandData::Double(v.y)); a.Append(CommandData::Double(v.z)); return a; };
            item.Set("position", vector(snapshot.eyePosition)); item.Set("forward", vector(snapshot.forward));
            item.Set("fov", CommandData::Double(snapshot.fov)); item.Set("near", CommandData::Double(snapshot.nearPlane)); item.Set("far", CommandData::Double(snapshot.farPlane)); item.Set("orthographic", CommandData::Bool(snapshot.isOrthographic));
            std::snprintf(line, sizeof(line),
                "camera component %llu%s\n"
                "  snapshot: eye(%.6f %.6f %.6f) fwd(%.6f %.6f %.6f)"
                " fov %.6f near %.6f far %.6f ortho %d\n",
                static_cast<unsigned long long>(camera->GetInstanceID()),
                camera->IsPrimary() ? " primary" : "",
                snapshot.eyePosition.x, snapshot.eyePosition.y, snapshot.eyePosition.z,
                snapshot.forward.x, snapshot.forward.y, snapshot.forward.z,
                snapshot.fov, snapshot.nearPlane, snapshot.farPlane,
                static_cast<int>(snapshot.isOrthographic));
            report += line;

            const char* matrixNames[4] = { "view", "proj", "invView", "invProj" };
            const math::matrix4x4 matrices[4] = {
                snapshot.view, snapshot.projection,
                snapshot.inverseView, snapshot.inverseProjection };

            for (int m = 0; m < 4; ++m)
            {
                report += "  ";
                report += matrixNames[m];
                auto matrix = CommandData::Array();
                for (int r = 0; r < 4; ++r)
                {
                    for (int c = 0; c < 4; ++c)
                    {
                        std::snprintf(line, sizeof(line), " %.6f", matrices[m].m[r][c]);
                        report += line;
                        matrix.Append(CommandData::Double(matrices[m].m[r][c]));
                    }
                }
                report += "\n";
                item.Set(matrixNames[m], std::move(matrix));
            }
            cameraData.Append(std::move(item));
        }

        if (report.empty()) report = "(활성 씬 카메라 없음)\n";
        report += "shadow cascades: EnhancedShadowPass render-owned state\n";
        std::printf("[shadowinfo]\n%s", report.c_str());
        std::fflush(stdout);
        Debug->LogWarning("[shadowinfo]\n" + report);
            data.Set("cameras", std::move(cameraData));
        return Ok({}, std::move(data));
    }

    void RegisterRenderDebugCommands(Registrar& reg)
    {
        reg.Result({ "light.proxy" }, &Cmd_light_proxy);
        reg.Result({ "render.matmode" }, &Cmd_render_matmode);
        reg.Result({ "render.backend" }, &Cmd_render_backend);
        reg.Result({ "dx12.live" }, &Cmd_dx12_live);
        reg.Result({ "render.rtinfo" }, &Cmd_render_rtinfo);
        reg.Result({ "pipeline.nodes" }, &Cmd_pipeline_nodes);
        reg.Result({ "render.shadowinfo" }, &Cmd_render_shadowinfo);
    }
}
