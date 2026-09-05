// LC6 (PHASE 14.5) — Diagnostics 도메인 명령.
//
// `mem.*` · `gc.*` · `gpu.*` · `profile.*` · `dump.*` · `crash.*` · `pix.*` ·
// `shadermeta.*`. 프로세스와 런타임을 관측한다(§9 의 Test/diagnostic probe).
//
// ── `crash.*` 는 예외 경계를 통과한다 ───────────────────────────────────
//
// 이 도메인에는 **일부러 프로세스를 죽이는 것이 일인 명령**이 있다. 등록이
// `reg.Escaping` 인 이유가 그것이고, 실측된 회귀에서 나온 구분이다 — `Execute`
// 에 예외 경계를 두자 죽는 것이 일인 명령이 죽지 않게 됐고, 크래시 덤프 회귀가
// 프로세스 종료를 기다리다 타임아웃 났다. 덤프 검증은 크래시가 나야만 도는
// 검사라 그대로 사각지대가 될 뻔했다. 예외 경계는 좋은 기본값이지만 기본값에는
// 예외가 있어야 한다.
//
// ★ 그래서 이 도메인은 **라이브 서비스로 부르면 안 되는 명령을 품고 있다.**
//   `crash.test throw` 는 에디터를 죽인다 — HTTP 로 그것을 부른 호출자는 응답을
//   못 받는다. 재시작 축과 별개로 표기가 필요한 자리이고, descriptor 작업에서
//   다룬다.
//
// ── 이 이동에서 바꾸지 않은 것 ──────────────────────────────────────────
//
// 핸들러 본문은 한 글자도 손대지 않았고 서명도 그대로다. 이동의 증거는
// `verify-cli-registry-golden.ps1` 이 한 글자도 안 변하는 것 하나뿐이라,
// 기능 변경을 같은 커밋에 섞어 그 증거를 버리지 않는다(§12.3).
//
// ── include 를 이 TU 가 직접 소유한다 ───────────────────────────────────
//
// 유니티 빌드에서 빠져 있다(`IncludeInUnityFile=false`). 다른 파일이 앞서
// 들여온 헤더에 기대면 평상시 빌드에서 바로 깨진다.

#include "CommandRegistrar.h"
#include "CommandSupport.h"

#include "CommandBaseline.h"            // LC0(PHASE 14.5): 등록 표·프레임·왕복 지연 계측
#include "CommandCore/CommandSession.h" // LC1: 결과 누적과 process exit code
#include "CommandCore/CommandParser.h"
#include "CommandCore/CommandRegistry.h"       // LC3: descriptor snapshot
#include "Commands/CommandRegistrar.h"          // LC6: 도메인 TU 등록 창구
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

// ★ LC6: `mem.*` 전용이라 함께 옮겨 왔다(사용 25회 대 1회).
// C0: CRT 할당 호출 계수. _CrtMemCheckpoint(live 블록)로는 프레임 내
// alloc→free churn이 0으로 보여서, 정작 재려는 비용이 안 보인다.
namespace CrtAllocProbe
{
#if defined(_DEBUG)
    inline std::atomic<size_t> g_count{ 0 };
    inline std::atomic<size_t> g_bytes{ 0 };
    inline std::atomic<bool>   g_enabled{ false };
    inline std::atomic<bool>   g_stackMode{ false };
    inline _CRT_ALLOC_HOOK     g_prev = nullptr;

    // 정의는 아래 "호출 스택 귀속" 절에 있다 — Hook이 먼저 와야 해서 전방 선언만.
    inline void RecordStack(size_t size);

    // ── 크기 히스토그램 (C0 Tier 1) ────────────────────────────────────────
    //
    // mem.bench가 밝힌 것: mimalloc의 이득이 크기에 크게 갈린다(<=256B는 22~75ns,
    // 4KB는 1,900ns). 그래서 "프레임당 272건"이라는 총량만으로는 판정할 수 없고
    // **그 272건이 어느 크기 구간에 있는지**가 있어야 한다.
    //
    // 버킷 b는 (8 << b) 바이트 이하를 뜻한다: 0=≤8, 1=≤16, 2=≤32 ...
    // 훅에서 원자 증가 하나뿐이라 스택 귀속보다 훨씬 싸다 — 항상 켜 둘 수 있다.
    constexpr int kSizeBuckets = 16;   // ≤8 … ≤256K
    inline std::atomic<size_t> g_sizeHist[kSizeBuckets];

    inline int SizeBucket(size_t n)
    {
        int b = 0;
        size_t cap = 8;
        while (cap < n && b < kSizeBuckets - 1) { cap <<= 1; ++b; }
        return b;
    }

    inline int Hook(int allocType, void* userData, size_t size, int blockType,
        long requestNumber, const unsigned char* filename, int lineNumber)
    {
        // _CRT_BLOCK은 CRT 자신의 내부 할당이라 우리 코드의 비용이 아니다.
        // 여기서 할당하면 무한 재진입이므로 원자 연산만 한다.
        if (_CRT_BLOCK != blockType &&
            (_HOOK_ALLOC == allocType || _HOOK_REALLOC == allocType))
        {
            g_count.fetch_add(1, std::memory_order_relaxed);
            g_bytes.fetch_add(size, std::memory_order_relaxed);
            g_sizeHist[SizeBucket(size)].fetch_add(1, std::memory_order_relaxed);
            if (g_stackMode.load(std::memory_order_relaxed)) { RecordStack(size); }
        }
        return (nullptr != g_prev)
            ? g_prev(allocType, userData, size, blockType, requestNumber, filename, lineNumber)
            : TRUE;
    }

    inline void Enable()
    {
        if (g_enabled.exchange(true)) { return; }
        g_count.store(0, std::memory_order_relaxed);
        g_bytes.store(0, std::memory_order_relaxed);
        g_prev = _CrtSetAllocHook(&Hook);
    }

    inline void Disable()
    {
        if (!g_enabled.exchange(false)) { return; }
        _CrtSetAllocHook(g_prev);
        g_prev = nullptr;
    }

    inline void Reset()
    {
        g_count.store(0, std::memory_order_relaxed);
        g_bytes.store(0, std::memory_order_relaxed);
        for (auto& h : g_sizeHist) { h.store(0, std::memory_order_relaxed); }
    }

    inline void Read(size_t* outCount, size_t* outBytes)
    {
        if (nullptr != outCount) { *outCount = g_count.load(std::memory_order_relaxed); }
        if (nullptr != outBytes) { *outBytes = g_bytes.load(std::memory_order_relaxed); }
    }

    inline bool IsEnabled() { return g_enabled.load(std::memory_order_relaxed); }

    // ── 호출 스택 귀속 ─────────────────────────────────────────────────────
    //
    // "프레임당 1,530건"만으로는 무엇을 고칠지 모른다. 어디서 나오는지가 있어야
    // C2의 첫 소비자를 코드 추측이 아니라 측정으로 고를 수 있다.
    //
    // 훅 안에서는 절대 할당하지 않는다 — 그래서 고정 크기 표에 open addressing으로
    // 넣고, 심볼 해석은 보고 시점(훅을 끈 뒤)에 한다. CaptureStackBackTrace는
    // 할당하지 않으므로 훅 안에서 안전하다.
    // CRT의 할당 계층(operator new → malloc → malloc_dbg → recalloc_dbg → 훅)이
    // 대여섯 겹이라 얕게 잡으면 전부 CRT 내부만 보인다(실측). 깊게 잡고,
    // 보고할 때 CRT 프레임을 건너뛴다 — 경로마다 겹 수가 달라 고정 skip으로는
    // 정확히 못 맞춘다.
    constexpr int    kStackDepth = 24;
    constexpr int    kSkipFrames = 2;
    // ★ 1024는 모자랐다 — 실측에서 표가 넘쳤고, 넘치면 **총계 자체가 못 믿을
    // 값이 된다.** 한 지점을 고쳐 자리가 비면 그동안 버려지던 다른 지점이
    // 그 자리를 채워서, 실제로는 줄었는데 총계는 그대로로 보인다(실제로
    // 프로파일러 수정 전후 비교에서 그 함정에 걸렸다).
    constexpr size_t kMaxSites   = 8192;

    struct Site
    {
        std::atomic<ULONG>  hash{ 0 };   // 0 = 빈 슬롯
        std::atomic<size_t> count{ 0 };
        std::atomic<size_t> bytes{ 0 };
        void*  stack[kStackDepth]{};
        USHORT depth{ 0 };
    };
    inline Site                g_sites[kMaxSites];
    inline std::atomic<size_t> g_siteOverflow{ 0 };

    inline void RecordStack(size_t size)
    {
        void* stack[kStackDepth]{};
        ULONG hash = 0;
        const USHORT depth = CaptureStackBackTrace(kSkipFrames, kStackDepth, stack, &hash);
        if (0 == depth) { return; }
        if (0 == hash) { hash = 1; }   // 0은 빈 슬롯 표식이라 피한다

        const size_t start = hash % kMaxSites;
        for (size_t i = 0; i < kMaxSites; ++i)
        {
            Site& s = g_sites[(start + i) % kMaxSites];
            const ULONG cur = s.hash.load(std::memory_order_relaxed);
            if (cur == hash)
            {
                s.count.fetch_add(1, std::memory_order_relaxed);
                s.bytes.fetch_add(size, std::memory_order_relaxed);
                return;
            }
            if (0 == cur)
            {
                ULONG expected = 0;
                if (s.hash.compare_exchange_strong(expected, hash, std::memory_order_relaxed))
                {
                    std::memcpy(s.stack, stack, sizeof(void*) * depth);
                    s.depth = depth;
                    s.count.fetch_add(1, std::memory_order_relaxed);
                    s.bytes.fetch_add(size, std::memory_order_relaxed);
                    return;
                }
                // 경쟁에 졌다 — 같은 해시가 들어갔으면 그쪽에 더한다
                if (s.hash.load(std::memory_order_relaxed) == hash)
                {
                    s.count.fetch_add(1, std::memory_order_relaxed);
                    s.bytes.fetch_add(size, std::memory_order_relaxed);
                    return;
                }
            }
        }
        g_siteOverflow.fetch_add(1, std::memory_order_relaxed);   // 표가 꽉 찼다
    }

    inline void ResetSites()
    {
        for (auto& s : g_sites)
        {
            s.hash.store(0, std::memory_order_relaxed);
            s.count.store(0, std::memory_order_relaxed);
            s.bytes.store(0, std::memory_order_relaxed);
            s.depth = 0;
        }
        g_siteOverflow.store(0, std::memory_order_relaxed);
    }
#endif
}

namespace
{
    // ★ LC6: 핸들러와 함께 옮겨 왔다.
    //
    //   `pix.*` 만 쓴다(사용 6회 대 선언 1회). 파일 지역 전역은 함수만 본
    //   의존성 측정의 사각지대였고, 빌드가 잡았다.
    // PIX가 주입된 실행에서만 DXGIGetDebugInterface1로 얻어진다. Begin/End는
    // DX12 커맨드 스트림에 원시 이벤트 payload를 쓰지 않는 정식 캡처 경계다.
    Microsoft::WRL::ComPtr<IDXGraphicsAnalysis> g_pixGraphicsAnalysis;
}

namespace ConsoleCmd
{
	static void Cmd_shadermeta_probe(const ConsoleCommandContext& ctx)
	{
		(void)ctx;

		auto propertyTypeName = [](ShaderPropertyType type) -> const char*
		{
			switch (type)
			{
			case ShaderPropertyType::Float:     return "float";
			case ShaderPropertyType::Float2:    return "float2";
			case ShaderPropertyType::Float3:    return "float3";
			case ShaderPropertyType::Float4:    return "float4";
			case ShaderPropertyType::Int:       return "int";
			case ShaderPropertyType::Bool:      return "bool";
			case ShaderPropertyType::Float4x4:  return "float4x4";
			case ShaderPropertyType::Texture2D: return "texture2d";
			}
			return "?";
		};
		// 기본값은 **어떤 대안이 채워졌는지**를 찍는다. 값까지 찍으면 부동소수 표기가
		// 게이트를 부서지게 만들고, 여기서 재려는 것은 "타입별 해석이 갈리지 않는가"다.
		auto defaultKind = [](const ShaderPropertyDefault& value) -> const char*
		{
			switch (value.index())
			{
			case 0: return "none";
			case 1: return "float";
			case 2: return "float2";
			case 3: return "float3";
			case 4: return "float4";
			case 5: return "int";
			case 6: return "bool";
			case 7: return "float4x4";
			case 8: return "guid";
			}
			return "?";
		};
		auto queueName = [](ShaderPassQueue queue) -> const char*
		{
			switch (queue)
			{
			case ShaderPassQueue::Opaque:      return "opaque";
			case ShaderPassQueue::Transparent: return "transparent";
			case ShaderPassQueue::Shadow:      return "shadow";
			case ShaderPassQueue::Compute:     return "compute";
			}
			return "?";
		};
		auto fillName = [](RHIFillMode mode) -> const char*
		{
			return RHIFillMode::Wireframe == mode ? "wireframe" : "solid";
		};
		auto cullName = [](RHICullMode mode) -> const char*
		{
			if (RHICullMode::None == mode) return "none";
			if (RHICullMode::Front == mode) return "front";
			return "back";
		};
		auto blendName = [](ShaderBlendMode mode) -> const char*
		{
			if (ShaderBlendMode::Alpha == mode) return "alpha";
			if (ShaderBlendMode::Additive == mode) return "additive";
			return "off";
		};
		auto depthName = [](RHICompareOp op) -> const char*
		{
			if (RHICompareOp::None == op) return "off";
			if (RHICompareOp::LessEqual == op) return "lessEqual";
			return "less";
		};
		auto topologyName = [](RHITopologyType type) -> const char*
		{
			if (RHITopologyType::Line == type) return "line";
			if (RHITopologyType::Point == type) return "point";
			return "triangle";
		};
		auto entryOf = [](const std::optional<ShaderStageEntry>& stage) -> std::string
		{
			return stage.has_value() ? stage->entry : std::string("-");
		};

		const file::path assetRoot = PathFinder::Relative();
		std::vector<file::path> metaPaths;
		std::error_code walkError;
		for (file::recursive_directory_iterator it(assetRoot, walkError), end;
			it != end && !walkError; it.increment(walkError))
		{
			if (!it->is_regular_file()) continue;
			if (it->path().extension() != ".shadermeta") continue;
			metaPaths.push_back(it->path());
		}
		// 순서를 고정한다. 디렉터리 순회 순서는 파일 시스템이 정하므로 그대로 두면
		// 게이트가 재실행마다 다른 순서를 본다.
		std::sort(metaPaths.begin(), metaPaths.end());

		std::uint32_t parsed = 0;
		for (const file::path& metaPath : metaPaths)
		{
			std::string relative = file::relative(metaPath, assetRoot).generic_string();

			// GUID는 파싱 의미론의 일부가 아니지만 nil이면 로더가 거부한다. 카탈로그가
			// 아는 것을 쓰되, 모르는 파일(자가 검증 fixture 등)은 sentinel로 대신한다 —
			// 그 구분을 함께 찍어 "sidecar가 없어서 통과했다"를 감출 수 없게 한다.
			FileGuid guid = DataSystems->GetFileGuid(metaPath);
			const bool fromCatalog = (guid != FileGuid{});
			if (!fromCatalog) guid = FileGuid::CreateRandomV4();

			std::ifstream input(metaPath, std::ios::binary);
			std::ostringstream buffer;
			buffer << input.rdbuf();
			const std::string text = buffer.str();

			ShaderMeta meta;
			std::string error;
			const bool ok = ShaderMetaLoader::Parse(text, metaPath, guid, meta, error);
			std::printf("[shadermeta.probe] file=%s ok=%d guidFromCatalog=%d"
				" name=%s source=%s props=%zu keywords=%zu passes=%zu err=%s\n",
				relative.c_str(), ok ? 1 : 0, fromCatalog ? 1 : 0,
				ok ? meta.name.c_str() : "-",
				ok ? meta.source.generic_string().c_str() : "-",
				meta.properties.size(), meta.keywords.size(), meta.passes.size(),
				ok ? "-" : error.c_str());
			if (!ok) continue;
			++parsed;

			for (const ShaderPropertyDesc& property : meta.properties)
			{
				std::printf("[shadermeta.probe] prop=%s|%s|%s|%s|%s\n",
					relative.c_str(), property.name.c_str(),
					property.label.c_str(), propertyTypeName(property.type),
					defaultKind(property.defaultValue));
			}
			for (const ShaderKeywordAxis& axis : meta.keywords)
			{
				std::string values;
				for (const std::string& value : axis.values)
				{
					if (!values.empty()) values += ",";
					values += value;
				}
				std::printf("[shadermeta.probe] axis=%s|%s|%s\n",
					relative.c_str(), axis.name.c_str(), values.c_str());
			}
			for (const ShaderPassDesc& pass : meta.passes)
			{
				std::printf("[shadermeta.probe] pass=%s|%s|vs=%s|ps=%s|cs=%s|queue=%s"
					"|fill=%s|cull=%s|blend=%s|depthWrite=%d|depthTest=%s|topology=%s\n",
					relative.c_str(), pass.name.c_str(),
					entryOf(pass.vertex).c_str(), entryOf(pass.pixel).c_str(),
					entryOf(pass.compute).c_str(), queueName(pass.queue),
					fillName(pass.state.fillMode), cullName(pass.state.cullMode),
					blendName(pass.state.blendMode), pass.state.depthWrite ? 1 : 0,
					depthName(pass.state.depthTest), topologyName(pass.state.topologyType));
			}
		}

		// ── 거절 계약 ────────────────────────────────────────────────────────────
		//
		// 실자산 코퍼스는 전부 유효하다. 여기가 없으면 "무엇이든 통과시키는 파서"가
		// 만점을 받는다. 사유 문자열까지 대조하는 이유는, 다른 이유로 실패한 것을
		// "거부했다"로 읽으면 계약이 아니라 우연을 재기 때문이다.
		struct RejectCase
		{
			const char* name;
			const char* text;
			const char* reason;
		};
		// 기준 경로는 실재하는 fixture 옆이어야 한다 — `source` 해소가 파일 존재를
		// 확인하므로, 존재하지 않는 디렉터리를 쓰면 전부 "source 없음"으로 거부되어
		// 정작 재려던 사유가 가려진다.
		const file::path rejectOrigin = assetRoot
			/ "Shaders" / "DefaultPassShader" / "SelfTest" / "RejectProbe.shadermeta";
		static const RejectCase kRejects[] = {
			{ "duplicate-property",
			  "schema: 1\nname: RejectDuplicate\nsource: ShaderMetaFixture.hlsl\n"
			  "properties:\n  - { name: v, type: float, default: 0.0 }\n"
			  "  - { name: v, type: float, default: 1.0 }\n"
			  "passes:\n  - { name: Main, vs: { entry: VSMain }, ps: { entry: PSMain },"
			  " queue: opaque }\n",
			  "중복" },
			{ "unknown-field",
			  "schema: 1\nname: RejectUnknown\nsource: ShaderMetaFixture.hlsl\n"
			  "passes:\n  - name: Main\n    vs: { entry: VSMain }\n"
			  "    ps: { entry: PSMain }\n    state: { depthWriet: false }\n"
			  "    queue: opaque\n",
			  "알 수 없는 field" },
			{ "escaping-source",
			  "schema: 1\nname: RejectPath\nsource: ../ShaderMetaFixture.hlsl\n"
			  "passes:\n  - { name: Main, vs: { entry: VSMain }, ps: { entry: PSMain },"
			  " queue: opaque }\n",
			  "상위 이동 없는 상대" },
			// ★ YAML 1.1 bool 표. `1`은 bool이 아니다 — 스칼라 파리티(D3-b-2b-0)가
			//   두 backend에서 갈리는 것으로 실측한 부류라 상시로 밟는다.
			{ "numeric-bool",
			  "schema: 1\nname: RejectBool\nsource: ShaderMetaFixture.hlsl\n"
			  "passes:\n  - name: Main\n    vs: { entry: VSMain }\n"
			  "    ps: { entry: PSMain }\n    state: { depthWrite: 1 }\n"
			  "    queue: opaque\n",
			  "true|false" },
			// ★ 없는 키. ryml `operator[]`는 여기서 **abort** 한다 — 어댑터가
			//   `find_child`로 흡수하는 것이 맞는지 실제로 밟아 확인한다.
			{ "missing-source",
			  "schema: 1\nname: RejectMissing\n"
			  "passes:\n  - { name: Main, vs: { entry: VSMain }, ps: { entry: PSMain },"
			  " queue: opaque }\n",
			  "필수 scalar 'source'" },
			// ★ 루트가 map이 아닌 경우. 시퀀스 루트에 map 연산을 걸면 backend마다
			//   반응이 다르다.
			{ "sequence-root",
			  "- schema: 1\n- name: RejectRoot\n",
			  "map이어야 한다" },
		};

		std::uint32_t rejected = 0;
		for (const RejectCase& item : kRejects)
		{
			ShaderMeta ignored;
			std::string error;
			const bool accepted = ShaderMetaLoader::Parse(
				item.text, rejectOrigin, FileGuid::CreateRandomV4(), ignored, error);
			const bool reasonMatched = !accepted
				&& std::string::npos != error.find(item.reason);
			if (reasonMatched) ++rejected;
			std::printf("[shadermeta.probe] reject=%s accepted=%d reasonMatched=%d err=%s\n",
				item.name, accepted ? 1 : 0, reasonMatched ? 1 : 0,
				accepted ? "-" : error.c_str());
		}

		std::printf("[shadermeta.probe] files=%zu parsed=%u rejectCases=%zu rejected=%u\n",
			metaPaths.size(), parsed,
			sizeof(kRejects) / sizeof(kRejects[0]), rejected);
	}

    static void Cmd_pix_capture(const ConsoleCommandContext& ctx)
    {
        const std::vector<std::string>& parts = ctx.parts;

        const std::string mode = (parts.size() >= 2) ? parts[1] : "status";
        if (mode == "begin")
        {
            if (g_pixGraphicsAnalysis)
            {
                std::printf("[CLI] pix.capture — 이미 캡처 중\n");
            }
            else
            {
                Microsoft::WRL::ComPtr<IDXGraphicsAnalysis> analysis;
                const HRESULT hr = DXGIGetDebugInterface1(0, IID_PPV_ARGS(&analysis));
                if (FAILED(hr))
                {
                    std::printf("[CLI] pix.capture begin 실패 0x%08X — PIX/pixtool로 실행했는지 확인\n",
                        static_cast<unsigned>(hr));
                }
                else
                {
                    analysis->BeginCapture();
                    g_pixGraphicsAnalysis = std::move(analysis);
                    std::printf("[CLI] pix.capture begin\n");
                }
            }
        }
        else if (mode == "end")
        {
            if (!g_pixGraphicsAnalysis)
            {
                std::printf("[CLI] pix.capture end — 진행 중인 캡처 없음\n");
            }
            else
            {
                // PIX는 EndCapture 전에 캡처 범위의 GPU 작업 완료를 권장한다.
                EnhancedSceneRenderer::WaitForLiveGpu();
                g_pixGraphicsAnalysis->EndCapture();
                g_pixGraphicsAnalysis.Reset();
                std::printf("[CLI] pix.capture end\n");
            }
        }
        else
        {
            std::printf("[CLI] pix.capture — %s\n",
                g_pixGraphicsAnalysis ? "캡처 중" : "대기");
        }
    }

    static void Cmd_profile_selftest(const ConsoleCommandContext& ctx)
    {
        // 현행 CPU 프로파일러의 계약을 못박는 특성화 검사(PHASE 14 P0).
        // 프레임 경계를 스스로 넘으므로 라이브 캡처를 교란한다 — 성능을 재는
        // 도중에 부르지 말 것. 게임 스레드에서 도는 것은 Pump()가 보장한다.
        std::string log;
        const bool passed = RunProfilerSelfTest(log);

        std::printf("%s", log.c_str());
        if (passed)
        {
            Debug->LogWarning(std::string("[profile.selftest] 통과\n") + log);
        }
        else
        {
            // 실패는 반드시 로그 파일에도 남긴다 — 회귀 스크립트가 stdout
            // 리다이렉트를 놓쳐도 판정 근거가 남아야 한다.
            Debug->LogError(std::string("[profile.selftest] 실패\n") + log);
        }
        std::printf("[CLI] profile.selftest %s\n", passed ? "통과" : "실패");
    }

    static void Cmd_profile_stats(const ConsoleCommandContext& ctx)
    {
        // 프로파일러 자신의 비용과 용량 소진. 프레임을 넘기지 않으므로
        // 라이브 캡처를 건드리지 않는다 — 측정 중에 불러도 안전하다.
        const std::string report = GetProfilerStatsReport();
        std::printf("%s", report.c_str());
        Debug->LogWarning(report);
    }

    // ★ `dump.crash` 를 지웠다(2026-09-05). `crash.test` 와 같은 네 분기를 가진
    //   중복이었고, 호출자가 없었으며, **죽지 않았다.**
    //
    //   `reg.Escaping` 이 아니라 `reg.Legacy` 로 등록되어 있어서 `dump.crash throw`
    //   가 ConsoleCommandSystem 의 핸들러 예외 포집에 잡혀 `internal_error
    //   (command.exception)` 로 바뀌었다. 실측:
    //
    //     dump.crash throw   죽지 않음 · quit 실행됨 · Finalize 완료 · exit 5
    //     crash.test  throw  죽음 · quit 미실행 · Finalize 미도달 · exit 3
    //
    //   일부러 죽어서 덤프 경로를 검증하는 것이 존재 이유인데 죽지 않으므로,
    //   덤프도 남지 않았다. descriptor 는 `terminates_process` 라고 적고 있었다.
    //   검증하지 못하는 검증 명령은 없는 것만 못하다.

    static void Cmd_dump_list(const ConsoleCommandContext& ctx)
    {
        const std::vector<std::string>& parts = ctx.parts;
        const std::string& cmd = ctx.cmd;

        // 크래시가 나면 덤프(.dmp)와 요약(.txt)이 같은 이름으로 함께 남는다.
        // dump.list는 목록만, dump.show는 가장 최근 요약의 내용까지 찍는다.
        const file::path dumpDir = PathFinder::DumpPath();
        std::printf("[CLI] 덤프 경로: %ls\n", dumpDir.c_str());

        std::error_code ec{};
        if (!file::exists(dumpDir, ec))
        {
            std::printf("[CLI] 덤프 폴더가 없습니다\n");
            return;
        }

        // 최신순으로 보여 준다 — 방금 난 크래시가 맨 위에 있어야 쓸모가 있다.
        std::vector<file::directory_entry> dumps;
        for (const auto& entry : file::directory_iterator(dumpDir, ec))
        {
            if (entry.is_regular_file(ec) && entry.path().extension() == ".dmp") dumps.push_back(entry);
        }

        std::sort(dumps.begin(), dumps.end(), [](const auto& a, const auto& b)
        {
            std::error_code cmpEc{};
            return file::last_write_time(a, cmpEc) > file::last_write_time(b, cmpEc);
        });

        if (dumps.empty())
        {
            Debug->LogWarning("[CLI] 덤프 없음");
            std::printf("[CLI] 덤프 없음\n");
            return;
        }

        // 콘솔은 별도 창이라 스크립트로 돌린 실행에서는 눈에 띄지 않는다.
        // 로그에도 남겨야 CI나 사후 확인에서 쓸 수 있다.
        const size_t limit = (parts.size() > 1) ? static_cast<size_t>(std::max(1, std::atoi(parts[1].c_str()))) : 10;
        for (size_t i = 0; i < dumps.size() && i < limit; ++i)
        {
            std::error_code sizeEc{};
            const auto bytes = file::file_size(dumps[i], sizeEc);

            char entry[512]{};
            std::snprintf(entry, sizeof(entry), "[CLI] 덤프 [%zu] %s (%.1f MB)",
                i, dumps[i].path().filename().string().c_str(),
                static_cast<double>(bytes) / (1024.0 * 1024.0));

            Debug->LogWarning(entry);
            std::printf("%s\n", entry);
        }

        if (cmd == "dump.show")
        {
            file::path reportPath = dumps.front().path();
            reportPath.replace_extension(".txt");

            std::ifstream report(reportPath);
            if (!report)
            {
                Debug->LogWarning("[CLI] 요약 파일 없음: " + reportPath.string());
                std::printf("[CLI] 요약 파일 없음: %ls\n", reportPath.c_str());
                return;
            }

            Debug->LogWarning("[CLI] 크래시 요약 " + reportPath.filename().string());
            std::string line;
            while (std::getline(report, line))
            {
                Debug->LogWarning("  " + line);
                std::printf("%s\n", line.c_str());
            }
        }
    }

    static void Cmd_gpu_baseline(const ConsoleCommandContext& ctx)
    {
        GpuDiagnostics::ResetBaseline();
        std::printf("[CLI] 기준선 초기화 (이후 gpu.delta는 이 시점과 비교)\n");
    }

    static void Cmd_gpu_census(const ConsoleCommandContext& ctx)
    {
        const std::vector<std::string>& parts = ctx.parts;
        const std::string& cmd = ctx.cmd;

        const std::string label = (parts.size() > 1) ? parts[1] : std::string("CLI 요청");
        // 실행 중에는 VRAM만 남는다. 타입별 집계는 디버그 레이어를 망가뜨려
        // 이후 렌더에서 죽으므로 종료 시점 리포트로만 얻을 수 있다.
        if (cmd == "gpu.delta") GpuDiagnostics::LogDelta(label);
        else                    GpuDiagnostics::LogCensus(label);

        std::printf("[CLI] GPU %s 기록: %s (VRAM 기준, 타입별 집계는 종료 리포트 참조)\n",
            (cmd == "gpu.delta") ? "증감" : "집계", label.c_str());
    }

    static void Cmd_gc_stats(const ConsoleCommandContext& ctx)
    {
        const std::vector<std::string>& parts = ctx.parts;
        const std::string& cmd = ctx.cmd;

        // 관리 힙 지표(PHASE 9-7). 씬 churn 벤치의 판정 기준을 네이티브에서
        // "네이티브·관리 양쪽 모두 기준선 복귀"로 넓히기 위한 것이다 —
        // gpu.delta만 보면 평탄성의 절반만 본다.
        static ClrHost::ScriptGcStats s_baseline{};
        static bool s_hasBaseline = false;

        ClrHost::ScriptGcStats gc{};
        if (!ClrHost::Get().GetManagedGcStats(gc))
        {
            std::printf("[CLI] 관리 힙 지표 없음 — 스크립트 계층 비활성\n");
            return;
        }

        const std::string label = (parts.size() > 1) ? parts[1] : std::string("CLI 요청");
        constexpr double kBytesPerMB = 1024.0 * 1024.0;

        char line[512]{};
        if (cmd == "gc.delta" && s_hasBaseline)
        {
            std::snprintf(line, sizeof(line),
                "[gc.delta] %s — gen0 %+d · gen1 %+d · gen2 %+d · 힙 %+.1f MB (현재 %.1f MB)",
                label.c_str(),
                gc.gen0Collections - s_baseline.gen0Collections,
                gc.gen1Collections - s_baseline.gen1Collections,
                gc.gen2Collections - s_baseline.gen2Collections,
                (gc.heapSizeBytes - s_baseline.heapSizeBytes) / kBytesPerMB,
                gc.heapSizeBytes / kBytesPerMB);
        }
        else
        {
            // 기준선이 없으면 delta 요청이어도 집계로 남기고 기준선을 세운다.
            // 조용히 0을 찍으면 "변화 없음"으로 오독된다.
            s_baseline = gc;
            s_hasBaseline = true;
            std::snprintf(line, sizeof(line),
                "[gc.stats] %s — gen0 %d · gen1 %d · gen2 %d · 힙 %.1f MB · 단편화 %.1f MB · GC 점유 %.2f%%",
                label.c_str(),
                gc.gen0Collections, gc.gen1Collections, gc.gen2Collections,
                gc.heapSizeBytes / kBytesPerMB,
                gc.fragmentedBytes / kBytesPerMB,
                gc.pauseTimePercentageX100 / 100.0);
        }

        std::printf("[CLI] %s\n", line);
        Debug->LogWarning(line);
    }

    static void Cmd_mem_stats(const ConsoleCommandContext& ctx)
    {
        const std::vector<std::string>& parts = ctx.parts;
        const std::string& cmd = ctx.cmd;

        // ★ mem.* 넷을 한 분기에 몰아 둔다 — else-if 사슬을 하나 더 늘리면
        // MSVC의 블록 중첩 한계(C1061)에 걸린다(실측). 이 파일의 명령 사슬은
        // 이미 그 한계에 붙어 있으므로 새 명령은 기존 분기에 합쳐 넣을 것.
        if (cmd == "mem.hook")
        {
        // CRT 할당 **churn** 계측 (ContainerLibraryDesign §5 C0).
        //
        // _CrtMemCheckpoint는 그 순간 살아 있는 블록만 센다. 그래서 "프레임마다
        // 할당했다가 그 프레임에 해제하는" 패턴이 0으로 보인다 — 그런데 그것이
        // 바로 ce::dynamic_array가 없애려는 비용이다. 순증만 보고 "할당이 없다"고
        // 판단하면 정확히 반대 결론에 이른다(실측으로 그 함정을 만났다).
        //
        // 그래서 할당 훅으로 호출 횟수 자체를 센다. _CRT_BLOCK(CRT 내부 할당)은
        // 제외한다 — 우리 코드의 할당이 아니다. 훅은 할당 경로에서 불리므로
        // 그 안에서 절대 할당하지 않는다(재진입).
#if defined(_DEBUG)
        const std::string mode = (parts.size() >= 2) ? parts[1] : "status";
        if (mode == "on" || mode == "stack")
        {
            // stack 모드는 할당마다 스택을 걷는다 — 훨씬 느리다. 귀속이 필요할
            // 때만 켠다. 순수 계수만 필요하면 on을 쓴다.
            CrtAllocProbe::ResetSites();
            CrtAllocProbe::g_stackMode.store(mode == "stack", std::memory_order_relaxed);
            CrtAllocProbe::Enable();
            std::printf("[CLI] mem.hook %s — CRT 할당 %s 시작 (디버그 CRT 전용)\n",
                mode.c_str(), (mode == "stack") ? "호출 계수 + 스택 귀속" : "호출 계수");
        }
        else if (mode == "off")
        {
            CrtAllocProbe::Disable();
            CrtAllocProbe::g_stackMode.store(false, std::memory_order_relaxed);
            std::printf("[CLI] mem.hook off\n");
        }
        else if (mode == "top")
        {
            // 심볼 해석은 힙을 쓴다 — 훅을 끄고 한다(재진입 방지).
            const bool wasOn = CrtAllocProbe::IsEnabled();
            CrtAllocProbe::Disable();

            const int limit = (parts.size() >= 3) ? std::max(1, std::atoi(parts[2].c_str())) : 12;

            std::vector<size_t> order;
            order.reserve(CrtAllocProbe::kMaxSites);
            for (size_t i = 0; i < CrtAllocProbe::kMaxSites; ++i)
            {
                if (0 != CrtAllocProbe::g_sites[i].hash.load(std::memory_order_relaxed))
                {
                    order.push_back(i);
                }
            }
            std::sort(order.begin(), order.end(), [](size_t a, size_t b)
                {
                    return CrtAllocProbe::g_sites[a].count.load(std::memory_order_relaxed)
                         > CrtAllocProbe::g_sites[b].count.load(std::memory_order_relaxed);
                });

            size_t total = 0;
            for (size_t i : order) { total += CrtAllocProbe::g_sites[i].count.load(std::memory_order_relaxed); }

            const HANDLE proc = GetCurrentProcess();
            SymSetOptions(SYMOPT_DEFERRED_LOADS | SYMOPT_UNDNAME | SYMOPT_LOAD_LINES);
            SymInitialize(proc, nullptr, TRUE);

            alignas(SYMBOL_INFO) char symBuf[sizeof(SYMBOL_INFO) + 512]{};
            auto* sym = reinterpret_cast<SYMBOL_INFO*>(symBuf);
            sym->SizeOfStruct = sizeof(SYMBOL_INFO);
            sym->MaxNameLen = 511;

            // ★ _Container_proxy 분리 집계 (C0-2 좌초의 대안).
            //
            // _ITERATOR_DEBUG_LEVEL>=1이면 MSVC가 컨테이너 인스턴스마다
            // _Container_proxy를 힙에 따로 잡는다. 그 몫을 빼야 "실제 코드가
            // 하는 할당"이 보인다. physx가 트리플릿 플래그를 무시해 IDL=0
            // 전환이 막혔으므로(Directory.Build.props 주석), 빌드를 바꾸는 대신
            // 보고에서 갈라 읽는다. 성능 이득은 못 얻지만 측정은 살아난다.
            size_t proxyCount = 0, proxyBytes = 0;
            for (size_t idx : order)
            {
                const auto& s = CrtAllocProbe::g_sites[idx];
                bool isProxy = false;
                for (USHORT f = 0; f < s.depth && f < 8 && !isProxy; ++f)
                {
                    DWORD64 disp = 0;
                    if (SymFromAddr(proc, reinterpret_cast<DWORD64>(s.stack[f]), &disp, sym))
                    {
                        isProxy = (nullptr != std::strstr(sym->Name, "_Container_proxy"));
                    }
                }
                if (isProxy)
                {
                    proxyCount += s.count.load(std::memory_order_relaxed);
                    proxyBytes += s.bytes.load(std::memory_order_relaxed);
                }
            }
            const size_t realCount = (total > proxyCount) ? (total - proxyCount) : 0;

            std::printf("[CLI] mem.top — 서로 다른 호출 지점 %zu개 / 총 %zu건%s\n",
                order.size(), total,
                (0 != CrtAllocProbe::g_siteOverflow.load(std::memory_order_relaxed)) ? " ★ 표 넘침(수치 과소)" : "");
            std::printf("[CLI]   ├ _Container_proxy(이터레이터 검사 부산물) %zu건 (%.1f%%) / %.2f MB\n",
                proxyCount, (0 != total) ? (100.0 * proxyCount / total) : 0.0,
                proxyBytes / (1024.0 * 1024.0));
            std::printf("[CLI]   └ 실제 코드 할당 %zu건 (%.1f%%) ← IDL=0이면 이 값만 남는다\n",
                realCount, (0 != total) ? (100.0 * realCount / total) : 0.0);

            // ── 크기 분포 (C0 Tier 1의 판정 입력) ──────────────────────────
            //
            // _Container_proxy는 전부 16바이트다(실측: 1.46MB / 95,901건 = 15.9B).
            // 그래서 16B 버킷에서 프록시 건수를 빼면 "실제 코드"의 분포가 된다 —
            // 훅에서 스택을 걷지 않고도 분리할 수 있는 이유다.
            //
            // 오른쪽 열은 mem.bench의 Release 실측을 대입한 것이다. 크기별
            // 절감(ns)은 ≤256B ≈ 40, 512B~2KB ≈ 선형 보간, ≥4KB ≈ 1,700으로 잡는다 —
            // 4KB 절벽이 어디서 시작하는지는 재지 않았으므로 그 사이는 근사다.
            std::printf("[CLI]   ── 크기 분포 (프록시 제외) ──\n");
            size_t frames = 120;   // mem_top 시나리오의 측정 구간
            double gainNsTotal = 0.0;
            size_t realTotal = 0;
            for (int b = 0; b < CrtAllocProbe::kSizeBuckets; ++b)
            {
                size_t c = CrtAllocProbe::g_sizeHist[b].load(std::memory_order_relaxed);
                if (1 == b) { c = (c > proxyCount) ? (c - proxyCount) : 0; }   // 16B에서 프록시 제거
                if (0 == c) { continue; }

                const size_t cap = static_cast<size_t>(8) << b;
                const double saveNs = (cap <= 256) ? 40.0
                                    : (cap >= 4096) ? 1700.0
                                    : (40.0 + (cap - 256) * (1700.0 - 40.0) / (4096.0 - 256.0));
                realTotal += c;
                gainNsTotal += saveNs * static_cast<double>(c);

                std::printf("[CLI]     <=%6zuB  %8zu건 (%5.1f%%)  절감가정 %6.0f ns\n",
                    cap, c, (0 != realCount) ? (100.0 * c / realCount) : 0.0, saveNs);
            }
            if (0 != realTotal)
            {
                const double perFrameUs = (gainNsTotal / static_cast<double>(frames)) / 1000.0;
                std::printf("[CLI]   ⇒ 가중 이득 %.1f us/frame (%.3f%% of 16.67ms) — 실제코드 %zu건/%zu프레임 = %.0f건/frame\n",
                    perFrameUs, (perFrameUs / 16666.7) * 100.0,
                    realTotal, frames, static_cast<double>(realTotal) / frames);
            }

            const int shown = static_cast<int>(std::min<size_t>(order.size(), static_cast<size_t>(limit)));
            for (int k = 0; k < shown; ++k)
            {
                const auto& s = CrtAllocProbe::g_sites[order[k]];
                const size_t c = s.count.load(std::memory_order_relaxed);
                const size_t b = s.bytes.load(std::memory_order_relaxed);
                std::printf("[CLI] #%d  %zu건 (%.1f%%) / %.2f MB\n",
                    k + 1, c, (0 != total) ? (100.0 * c / total) : 0.0, b / (1024.0 * 1024.0));

                // CRT 할당 계층을 건너뛰고 실제 호출자부터 보여 준다.
                int  printed = 0;
                bool reachedCaller = false;
                for (USHORT f = 0; f < s.depth && printed < 6; ++f)
                {
                    DWORD64 disp = 0;
                    const char* name = SymFromAddr(proc, reinterpret_cast<DWORD64>(s.stack[f]), &disp, sym)
                        ? sym->Name : "(?)";
                    IMAGEHLP_LINE64 li{}; li.SizeOfStruct = sizeof(li);
                    DWORD lineDisp = 0;
                    const bool hasLine = (0 != SymGetLineFromAddr64(
                        proc, reinterpret_cast<DWORD64>(s.stack[f]), &lineDisp, &li));

                    if (!reachedCaller)
                    {
                        // CRT 소스에서 온 프레임이거나 알려진 할당 진입점이면 건너뛴다.
                        const bool isCrtFile = hasLine && (nullptr != std::strstr(li.FileName, "vctools\\crt"));
                        const bool isCrtName =
                            (nullptr != std::strstr(name, "malloc")) ||
                            (nullptr != std::strstr(name, "calloc")) ||
                            (nullptr != std::strstr(name, "realloc")) ||
                            (nullptr != std::strstr(name, "operator new"));
                        if (isCrtFile || isCrtName) { continue; }
                        reachedCaller = true;
                    }

                    if (hasLine) { std::printf("[CLI]      %s  (%s:%lu)\n", name, li.FileName, li.LineNumber); }
                    else         { std::printf("[CLI]      %s\n", name); }
                    ++printed;
                }
            }
            SymCleanup(proc);
            if (wasOn) { CrtAllocProbe::Enable(); }
        }
        else
        {
            size_t c = 0, b = 0;
            CrtAllocProbe::Read(&c, &b);
            std::printf("[CLI] mem.hook %s%s — 누계 %zu건 / %.2f MB\n",
                CrtAllocProbe::IsEnabled() ? "on" : "off",
                CrtAllocProbe::g_stackMode.load(std::memory_order_relaxed) ? "(stack)" : "",
                c, b / (1024.0 * 1024.0));
        }
#else
        std::printf("[CLI] mem.hook — 디버그 CRT가 아니라 사용할 수 없다\n");
#endif
            return;
        }

        // 힙 사용량 (ContainerLibraryDesign §5 C0).
        //
        // 이 프로세스의 네이티브 힙은 이제 하나다 — CRT. 예전에는 둘이었고
        // (mimalloc/ManagedHeap이 meta::polymorphic 파생만 받았다) 이 명령은
        // 그 비율을 재기 위해 있었다. 그 비율이 곧 답이었다: 0.031%.
        // 그래서 mimalloc을 걷어냈고, 남은 것은 CRT 한 축이다.
        //
        // CRT 수치는 디버그 CRT에서만 나온다. Release에서는 0으로 찍히므로
        // 없는 것과 0인 것을 구분해 적는다.
        // 기준선은 아래 정적 변수들이 들고 있다 — mem.reset이 누계만 지우고
        // 기준선을 남기면 다음 delta가 음수로 나온다(실측으로 걸렸다).
        static size_t s_crtBlocks = 0, s_crtBytes = 0;
        static bool   s_hasBaseline = false;

        if (cmd == "mem.reset")
        {
#if defined(_DEBUG)
            CrtAllocProbe::Reset();
#endif
            s_hasBaseline = false;
            std::printf("[CLI] mem — churn 누계와 기준선 초기화 (CRT live 블록은 CRT 소유라 못 지운다)\n");
            return;
        }

        size_t crtBlocks = 0, crtBytes = 0;
        bool   crtAvailable = false;
#if defined(_DEBUG)
        _CrtMemState st{};
        _CrtMemCheckpoint(&st);
        for (int i = 0; i < _MAX_BLOCKS; ++i)
        {
            crtBlocks += st.lCounts[i];
            crtBytes  += st.lSizes[i];
        }
        crtAvailable = true;
#endif

        const std::string label = (parts.size() > 1) ? parts[1] : std::string("CLI 요청");
        constexpr double kBytesPerMB = 1024.0 * 1024.0;

        char line[640]{};
        if (cmd == "mem.delta" && s_hasBaseline)
        {
            std::snprintf(line, sizeof(line),
                "[mem.delta] %s — CRT %+lld블록 / %+.2f MB%s",
                label.c_str(),
                static_cast<long long>(crtBlocks) - static_cast<long long>(s_crtBlocks),
                (static_cast<double>(crtBytes) - static_cast<double>(s_crtBytes)) / kBytesPerMB,
                crtAvailable ? "" : " (CRT 미집계 — 디버그 CRT 아님)");
        }
        else
        {
            s_crtBlocks = crtBlocks; s_crtBytes = crtBytes;
            s_hasBaseline = true;

            // live 블록만으로는 "프레임마다 잡았다 놓는" 양이 보이지 않는다.
            // 그 양은 mem.hook의 churn 줄이 따로 적는다 — 성질이 다르므로 섞지 않는다.
            std::snprintf(line, sizeof(line),
                "[mem.stats] %s — CRT 현재 %zu블록 / %.2f MB%s",
                label.c_str(),
                crtBlocks, crtBytes / kBytesPerMB,
                crtAvailable ? "" : " (CRT 미집계 — 디버그 CRT 아님)");
        }

        std::printf("[CLI] %s\n", line);

        // churn은 live 블록과 성질이 완전히 달라서 같은 줄에 섞지 않는다.
#if defined(_DEBUG)
        if (CrtAllocProbe::IsEnabled())
        {
            size_t churnCount = 0, churnBytes = 0;
            CrtAllocProbe::Read(&churnCount, &churnBytes);
            std::printf("[CLI]   CRT churn(할당 호출 누계) %zu건 / %.2f MB — mem.reset 이후\n",
                churnCount, churnBytes / (1024.0 * 1024.0));
        }
        else
        {
            std::printf("[CLI]   CRT churn 미집계 — mem.hook on 을 먼저 부를 것\n");
        }
#endif
        Debug->LogWarning(line);
    }

    static void Cmd_gc_collect(const ConsoleCommandContext& ctx)
    {
        // 씬 전환이 자동으로 부르는 것과 같은 경로를 손으로 부른다.
        // 벤치에서 "전환 없이도 회수되는가"를 가르는 데 쓴다.
        ClrHost::Get().CollectManagedHeap();
        std::printf("[CLI] 관리 힙 확정 수집 요청\n");
    }

    static void Cmd_crash_status(const ConsoleCommandContext& ctx)
    {
        // 이번 실행이 덤프를 남길 수 있는 상태인지 확인한다.
        // 크래시가 난 뒤에 '덤프가 없네'로 알게 되는 일이 없도록 하는 것이 목적.
        const bool ready = Log::HasCrashDumpWriter();
        std::printf("[CLI] 크래시 덤프 기록자: %s\n", ready ? "등록됨" : "미등록");
        std::printf("[CLI] 덤프 경로: %ls\n", PathFinder::DumpPath().c_str());
        std::printf("[CLI] 무인 모드: %s\n", CoreWindow::IsUnattended() ? "예(대화상자 없음)" : "아니오");
    }

    static void Cmd_crash_test(const ConsoleCommandContext& ctx)
    {
        const std::vector<std::string>& parts = ctx.parts;

        // 크래시 핸들러 자체를 검증하는 수단.
        //
        // 덤프 경로는 크래시가 나야만 실행되므로 평소에는 검증이 안 되고,
        // 검증되지 않은 채로 조용히 망가져 있었다(로그에 CRASH 줄만 남고 .dmp 없음).
        // 종류별로 일부러 죽여서 각 경로가 .dmp와 .txt를 남기는지 본다.
        const std::string kind = (parts.size() > 1) ? parts[1] : std::string("av");

        std::printf("[CLI] crash.test %s - 의도적으로 프로세스를 죽인다\n", kind.c_str());
        Debug->LogWarning("[crash.test] 의도적 크래시: " + kind);
        Log::FlushNow();

        if (kind == "av")
        {
            // 널 역참조. volatile이라 최적화로 사라지지 않는다.
            volatile int* nullPointer = nullptr;
            *nullPointer = 1;
        }
        else if (kind == "abort")
        {
            std::abort();
        }
        else if (kind == "terminate")
        {
            std::terminate();
        }
        else if (kind == "throw")
        {
            // 미처리 C++ 예외 → terminate 경로.
            throw std::runtime_error("crash.test throw");
        }
        else
        {
            std::printf("[CLI] 알 수 없는 종류: %s (av|abort|terminate|throw)\n", kind.c_str());
        }
    }

    // 이미 별도 함수로 빠져 있던 진단·벤치 명령들.

    // 표의 값이 함수 포인터 하나에서 둘 중 하나를 든 entry 로 바뀌었다(LC1).
    //
    // std::function 을 쓰지 않는다 — 205 개 entry 에서 비용이 문제여서가 아니라,
    // "어느 쪽 서명인가"가 타입에 드러나야 이행 진행도를 셀 수 있기 때문이다.
    // legacy 가 0 이 되는 날이 LC9 의 완료 조건 중 하나다.

    void RegisterDiagnosticsCommands(Registrar& reg)
    {
        reg.Legacy({ "shadermeta.probe" }, &Cmd_shadermeta_probe);
        reg.Legacy({ "pix.capture" }, &Cmd_pix_capture);
        reg.Legacy({ "profile.selftest" }, &Cmd_profile_selftest);
        reg.Legacy({ "profile.stats" }, &Cmd_profile_stats);
        // ★ 별칭이 아니라 **다른 동사**라 descriptor 를 갈랐다(2026-09-06).
        //   `dump.list` 는 목록만, `dump.show` 는 가장 최근 요약의 내용까지 찍는다
        //   (`Cmd_dump_list` 안에서 `cmd == "dump.show"` 로 갈린다). 한 descriptor 를
        //   공유하면 `commands.list`·help·`GET /commands` 가 둘을 같은 명령으로
        //   보여 주고 요약도 하나만 실린다. 핸들러는 그대로 하나다.
        reg.Legacy({ "dump.list" }, &Cmd_dump_list);
        reg.Legacy({ "dump.show" }, &Cmd_dump_list);
        reg.Legacy({ "gpu.baseline" }, &Cmd_gpu_baseline);
        reg.Legacy({ "gpu.census", "gpu.delta" }, &Cmd_gpu_census);
        reg.Legacy({ "gc.stats", "gc.delta" }, &Cmd_gc_stats);
        // ★ 넷 다 **다른 동사**라 descriptor 를 갈랐다(2026-09-06).
        //
        //   `Cmd_mem_stats` 는 `cmd` 로 갈린다 — `mem.reset` 은 기준선을 0 으로,
        //   `mem.delta` 는 기준선 대비 증감, `mem.hook` 은 CRT 할당 훅
        //   (`on|stack|off|top|status`, 147 줄짜리 하위 도구), `mem.stats` 는 현재
        //   live 블록. 이것들이 한 descriptor 를 공유하는 바람에 **요약이 엉뚱한
        //   동사를 설명하고 있었다** — `mem.stats` 의 seed 요약이 "churn 누계와
        //   기준선을 0으로", 즉 `mem.reset` 의 설명이었다.
        //
        //   상태를 바꾸는 `mem.reset`·`mem.hook` 이 조회 descriptor 뒤에 숨어
        //   있으면 mutating 명령을 registry 로 셀 수 없다는 문제도 같이 붙는다.
        //
        //   핸들러가 넷으로 갈리지 않고 하나로 남아 있는 것은 **MSVC 의 C1061
        //   중첩 한계** 때문이다(이 파일 안 주석에 그 경위가 있다). 그것은 구현
        //   제약이지 이 넷이 한 명령이라는 뜻이 아니다.
        reg.Legacy({ "mem.stats" }, &Cmd_mem_stats);
        reg.Legacy({ "mem.delta" }, &Cmd_mem_stats);
        reg.Legacy({ "mem.reset" }, &Cmd_mem_stats);
        reg.Legacy({ "mem.hook"  }, &Cmd_mem_stats);
        reg.Legacy({ "gc.collect" }, &Cmd_gc_collect);
        reg.Legacy({ "crash.status" }, &Cmd_crash_status);
        reg.Escaping({ "crash.test" }, &Cmd_crash_test);
    }
}
