// LC6 (PHASE 14.5) — Renderer 라이브 조회·조정 명령.
//
// 도는 렌더러의 상태를 읽거나 바꾼다. 격리 씬을 세우지 않고, 산출물을 남기지
// 않고, 켜져 있는 에디터에 붙어 그 자리에서 답한다.
//
//   dx12.live          EnhancedRenderer 런타임 상태
//   pipeline.nodes     라이브 파이프라인의 노드 조립 결과
//   render.exposure    자동 노출이 무엇을 재고 무엇을 결정했는지
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
#include "CommandBaseline.h"            // LC0(PHASE 14.5): 등록 표·프레임·왕복 지연 계측
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
#include "ExperimentParity/ExperimentMaterialParitySelfTest.h"
#include "ExperimentParity/ExperimentMaterialResolveSelfTest.h"
#include "ExperimentParity/ExperimentMaterialInstanceSelfTest.h"
#include "ExperimentParity/ExperimentMaterialSealSelfTest.h"
#include "ExperimentParity/ExperimentMaterialCodecSelfTest.h"
#include "ExperimentParity/ExperimentMaterialMigrateSelfTest.h"
#include "ExperimentParity/ExperimentMaterialScriptSelfTest.h"
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
    static void Cmd_render_matmode(const ConsoleCommandContext& ctx)
    {
        const std::vector<std::string>& parts = ctx.parts;

        // render.matmode <오브젝트> <opaque|transparent>
        //
        // 재질의 렌더링 모드를 바꾼다. 이것이 프록시가 deferred 큐로 가느냐
        // forward 큐로 가느냐를 정한다(RenderPassData가 이 값 하나로 나눈다).
        //
        // 전용 명령을 만든 이유: object.property는 컴포넌트의 반사 필드를
        // 설정하는데, m_renderingMode는 컴포넌트가 아니라 그 아래 Material의
        // 필드라 경로가 닿지 않는다. 그리고 이 값 없이는 Forward+ 경로를
        // 실제 씬에서 한 번도 실행해 볼 수 없다 — 씬에 투명 재질이 없으면
        // forward 큐가 늘 비고, 그러면 '되는지 안 되는지 모르는' 상태가 된다.
        if (parts.size() < 3)
        {
            std::printf("[CLI] 사용법: render.matmode <오브젝트> <opaque|transparent>\n");
            return;
        }

        Scene* scene = SceneManagers->GetActiveScene();
        if (!scene) { std::printf("[CLI] 활성 씬 없음\n"); return; }

        auto object = scene->GetEntity(parts[1]);
        if (!object)
        {
            std::printf("[CLI] 오브젝트를 찾을 수 없음: %s\n", parts[1].c_str());
            return;
        }

        const bool transparent = ("transparent" == parts[2]);
        if (!transparent && "opaque" != parts[2])
        {
            std::printf("[CLI] 모드는 opaque 또는 transparent여야 한다: %s\n",
                parts[2].c_str());
            return;
        }

        // 자식까지 훑는다. 모델 하나가 메시 여러 개로 들어오는 것이 보통이라
        // 루트만 바꾸면 큐가 그대로 비어 있고, 그건 '명령이 안 먹었다'와
        // 구분되지 않는다.
        //
        // ★ 재질은 모델 단위로 공유된다. 같은 모델을 두 번 배치한 뒤 하나만
        //   바꾸려 해도 둘 다 바뀐다 — Material 객체가 하나이기 때문이다.
        //   '불투명 하나 + 투명 하나' 배치를 만들려다 이것으로 한 번 헛돌았다.
        //   그렇게 하려면 재질 복제가 먼저 필요하고, 그건 이 명령의 몫이 아니다.
        uint32_t changed = 0;
        std::function<void(Entity*)> apply = [&](Entity* node)
        {
            if (nullptr == node) return;
            for (const auto& component : node->m_components)
            {
                auto* renderer = dynamic_cast<MeshRenderer*>(component.get());
                if (nullptr == renderer || nullptr == renderer->m_Material) continue;

                renderer->m_Material->m_renderingMode = transparent
                    ? MaterialRenderingMode::Transparent
                    : MaterialRenderingMode::Opaque;
                ++changed;
            }
            for (auto child : node->GetChildrenIndices())
            {
                apply(node->OwnerSceneFindIndex(child));
            }
        };
		apply(object);

        Debug->LogWarning("[CLI] 렌더링 모드 " + parts[2] + " — 재질 "
            + std::to_string(changed) + "개");
        std::printf("[CLI] 렌더링 모드 %s — 재질 %u개\n", parts[2].c_str(), changed);
    }

    static void Cmd_render_backend(const ConsoleCommandContext& ctx)
    {
        const std::vector<std::string>& parts = ctx.parts;

        const std::string backend = (parts.size() >= 2) ? parts[1] : "status";
        if (backend == "dx12" || backend == "enhanced" ||
            backend == "vulkan" || backend == "vk")
        {
            std::printf("[CLI] render.backend %s 거부 — backend는 부팅 고정이다. Editor는 Settings, Player는 Build Settings에서 저장한 뒤 새 프로세스로 실행한다\n",
                backend.c_str());
        }
        else if (backend == "dx11")
        {
            std::printf("[CLI] render.backend dx11 — 지원하지 않음: SceneRenderer는 dead code다\n");
        }
        else
        {
            const char* active = EnhancedLiveBackend::Vulkan ==
                EnhancedSceneRenderer::GetLiveBackend() ? "enhanced-vulkan" : "enhanced-dx12";
            std::printf("[CLI] render.backend — configured: %s · scene: %s · ImGui: %s (부팅 고정)\n",
				RenderBackendName(RuntimeSettings::Get().GetRenderBackend()),
                active, GetImGuiHost().GetBackendName());
            const std::string status = EnhancedSceneRenderer::GetLiveStatus();
            std::printf("%s\n", status.c_str());
            Debug->LogWarning(std::string("[render.backend] scene=") + active +
                " imgui=" + GetImGuiHost().GetBackendName() + " · " + status);
        }
    }

    static void Cmd_dx12_live(const ConsoleCommandContext& ctx)
    {
        const std::vector<std::string>& parts = ctx.parts;

        const std::string mode = (parts.size() >= 2) ? parts[1] : "status";

        if (mode == "on")
        {
            EnhancedSceneRenderer::EnableLive();
            std::printf("[CLI] dx12.live 켜짐 — EnhancedRenderer가 메인 렌더러다\n");
        }
        else if (mode == "off")
        {
            std::printf("[CLI] dx12.live off — 지원하지 않음: 단독 메인 렌더러는 끌 수 없다\n");
        }
        else
        {
            const std::string status = EnhancedSceneRenderer::GetLiveStatus();
            std::printf("%s\n", status.c_str());
            Debug->LogWarning("[dx12.live] " + status);
        }
    }

    static void Cmd_render_rtinfo(const ConsoleCommandContext& ctx)
    {

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
    }

    static void Cmd_render_exposure(const ConsoleCommandContext& ctx)
    {
        // 기존 구현은 SceneRenderer가 갱신하는 DX11 ToneMapPass의 정적 값을
        // 읽었다. 단독 모드에서 그 값을 출력하면 정상처럼 보이는 오래된 0값을
        // 진단값으로 오인하게 되므로 새 DX12 계측이 붙기 전까지 차단한다.
        std::printf("[CLI] render.exposure — DX11 레거시 진단은 비활성화됨; PIX의 Enhanced PostChain을 확인한다\n");
    }

    static void Cmd_pipeline_nodes(const ConsoleCommandContext& ctx)
    {
        (void)ctx;
        const EnhancedLiveDebugSnapshot snapshot =
            EnhancedSceneRenderer::GetLiveDebugSnapshot();

        if (!snapshot.enabled)
        {
            std::printf("[pipeline.nodes] 러너 비활성\n");
            return;
        }

        // Dump()는 "  <i>. <이름>  [active|inactive]" 형태의 여러 줄을 낸다.
        // 게이트가 정규식 하나로 읽도록 노드 줄만 접두어를 붙여 다시 낸다.
        size_t nodeCount = 0;
        std::istringstream stream(snapshot.pipelineDescription);
        std::string line;
        while (std::getline(stream, line))
        {
            const size_t dot = line.find(". ");
            if (std::string::npos == dot) continue;
            if (line.find_first_not_of(" \t") != 2) continue; // 노드 줄은 2칸 들여쓰기

            std::string name = line.substr(dot + 2);
            const size_t bracket = name.find("  [");
            std::string state = "always";
            if (std::string::npos != bracket)
            {
                state = name.substr(bracket + 3);
                if (!state.empty() && ']' == state.back()) state.pop_back();
                name = name.substr(0, bracket);
            }
            std::printf("[pipeline.node] %s|%s\n", name.c_str(), state.c_str());
            ++nodeCount;
        }

        std::printf("[pipeline.nodes] 합계 %zu · valid=%d · ready=%d\n",
            nodeCount, snapshot.pipelineDescriptionValid ? 1 : 0,
            snapshot.pipelineReady ? 1 : 0);
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

    static void Cmd_render_shadowinfo(const ConsoleCommandContext& ctx)
    {
        // 카메라 입력은 RenderPassData가 아니라 프레임 패킷의 값 스냅샷이다.
        // 이 명령은 활성 씬의 저작 카메라를 같은 방식으로 밀봉해 입력을 확인한다.
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
                for (int r = 0; r < 4; ++r)
                {
                    for (int c = 0; c < 4; ++c)
                    {
                        std::snprintf(line, sizeof(line), " %.6f", matrices[m].m[r][c]);
                        report += line;
                    }
                }
                report += "\n";
            }
        }

        if (report.empty()) report = "(활성 씬 카메라 없음)\n";
        report += "shadow cascades: EnhancedShadowPass render-owned state\n";
        std::printf("[shadowinfo]\n%s", report.c_str());
        std::fflush(stdout);
        Debug->LogWarning("[shadowinfo]\n" + report);
    }

    void RegisterRenderDebugCommands(Registrar& reg)
    {
        reg.Legacy({ "render.matmode" }, &Cmd_render_matmode);
        reg.Legacy({ "render.backend" }, &Cmd_render_backend);
        reg.Legacy({ "dx12.live" }, &Cmd_dx12_live);
        reg.Legacy({ "render.rtinfo" }, &Cmd_render_rtinfo);
        reg.Legacy({ "render.exposure" }, &Cmd_render_exposure);
        reg.Legacy({ "pipeline.nodes" }, &Cmd_pipeline_nodes);
        reg.Legacy({ "render.shadowinfo" }, &Cmd_render_shadowinfo);
    }
}
