#include "ConsoleCommandSystem.h"
#include "EditorCameraRig.h"
#include "EditorSessionState.h"
#include "EngineBootstrap.h"
#include "GameBuilderSystem.h"
#include "EditorAssetDatabase.h"
#include "Interfaces/AssetAuthoringPort.h"
#include "Interfaces/FoliageInstance.h"

#include <mathematics/color.hpp>

#include "SceneManager.h"
// SceneManager.h는 Scene을 전방 선언만 한다. 여기서는 씬의 멤버를 훑으므로
// 완전한 형이 필요하다 — 유니티 빌드에서는 앞선 파일이 공급했다.
#include "Scene.h"
#include "CameraComponent.h"
#include "CameraSystem.h"
#include "ClrHost.h"
#include "ScriptComponent.h"
#include "PrefabUtility.h"
#include "ComponentFactory.h"
#include "Model.h"
#include "LifecycleTrace.h"
#include "LifecycleRegistry.h"
#include "Animator.h"
#include "Skeleton.h" // E7-b: 벤치 진단이 m_bones 크기를 읽는다
#include "Experiment/Model.h" // I5-D4e-1: experiment.animtick 패리티
#include "RenderScene.h"      // I5-D4e-1: GetAnimationJob
#include "AvatarMask.h"       // I5-D4e-3: experiment.animmask A/B 대조
#include "FoliageComponent.h"      // I5-D5a: experiment.foliage 게이트
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
#include "RHI/IRenderDeviceServices.h" // I5-D5a: RHIExperimentVertexView
#include "ConditionParameter.h"
#include "UIManager.h"
#include "Canvas.h"
#include "ImageComponent.h"
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
#include "AuthoringParserProbe.h" // D3-b-0
#include "AuthoringRymlErrorPolicy.h" // D3-b-1: ryml abort → 예외 정책
#include "AuthoringScalarParityProbe.h" // D3-b-2: 스칼라 변환 파리티
#include "AuthoringAdapterParityProbe.h" // D3-b-2b-1b-3a: 어댑터 파리티
#include "SerializationProfiler.h" // D0(SerializationPlan): 직렬화 기준선 계측
#include "CoreWindow.h"
#include "Render/Scene/EnhancedSceneRenderer.h"
#include "RHI/DX12/Tests/DX12SelfTest.h"
#include "RHI/Vulkan/VulkanSelfTest.h"
#include "RHI/IImGuiHost.h"
#include "ProfilerSelfTest.h"
#include "ExperimentParity/ExperimentModelParitySelfTest.h"
#include "ExperimentParity/ExperimentModelBridgeSelfTest.h"
#include "ExperimentParity/ExperimentVertexLayoutSelfTest.h"
#include "ExperimentParity/ExperimentAnimationPlayback.h"
#include "ExperimentParity/ExperimentImportPathSelfTest.h"
#include "ExperimentParity/ExperimentGltfImportSelfTest.h"
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
// Undo/선택 프로브(E3-2 게이트)가 쓴다. Reflection 사슬이 ReflectionUndo.h를
// 전이로 물어 주지만 그 사슬에 기대지 않는다 — 바로 아래 StringHelper.h가
// 같은 실수로 비유니티 빌드에서 깨진 적이 있다.
#include "ReflectionUndo.h"
#include "GameObjectCommand.h"
// StringToWstring. 유니티 빌드에서는 같은 청크의 EditorAssetDatabase.cpp가
// 대신 물어 줘서 보이지 않던 누락이라, 비유니티 빌드에서만 드러났다.
#include "StringHelper.h"
#include "BlackBoard.h"
#include "TagManager.h"

#include <Windows.h>
#include <crtdbg.h>
#include <algorithm>
#include <atomic>
#include <cctype>
#include <random>
#include <unordered_map>
#include <DbgHelp.h>
#pragma comment(lib, "dbghelp.lib")

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

namespace
{
    // PIX가 주입된 실행에서만 DXGIGetDebugInterface1로 얻어진다. Begin/End는
    // DX12 커맨드 스트림에 원시 이벤트 payload를 쓰지 않는 정식 캡처 경계다.
    Microsoft::WRL::ComPtr<IDXGraphicsAnalysis> g_pixGraphicsAnalysis;

    // camera.editor follow 의 상태. 게임 스레드(App 프레임 경계와 CLI Pump)
    // 에서만 만지므로 원자성이 필요 없다 — 둘 다 같은 스레드다.
    bool g_editorCameraFollowsGame = false;

    // 앞뒤 공백 제거
    std::string TrimLine(const std::string& s)
    {
        const auto begin = s.find_first_not_of(" \t\r\n");
        if (begin == std::string::npos) return {};
        const auto end = s.find_last_not_of(" \t\r\n");
        return s.substr(begin, end - begin + 1);
    }

    /// 공백으로 쪼개되 큰따옴표로 묶은 구간은 한 토큰으로 본다.
    ///
    /// 이름에 공백이 들어가는 경우가 실제로 있다 — 기본 씬의 카메라가
    /// "Main Camera"라서 object.transform이 이 오브젝트를 영영 못 찾았다
    /// (parts[1]이 `"Main`이 된다). 따옴표를 안 쓰면 동작이 예전과 같으므로
    /// 기존 스크립트는 그대로 돈다.
    std::vector<std::string> Split(const std::string& line)
    {
        std::vector<std::string> parts;
        std::string token;
        bool inQuotes = false;
        // 빈 따옴표("")도 '값을 비웠다'는 뜻이라 토큰으로 남긴다 —
        // 길이만 보면 그것을 버리게 된다.
        bool hasToken = false;

        const auto flush = [&parts, &token, &hasToken]
        {
            if (!hasToken) return;
            parts.push_back(token);
            token.clear();
            hasToken = false;
        };

        for (const char c : line)
        {
            if ('"' == c) { inQuotes = !inQuotes; hasToken = true; continue; }
            if (!inQuotes && (' ' == c || '\t' == c || '\r' == c || '\n' == c))
            {
                flush();
                continue;
            }
            token.push_back(c);
            hasToken = true;
        }
        flush();
        return parts;
    }

    // "1,2,3" 또는 "1 2 3"을 성분으로 쪼갠다. 두 형태를 다 받는 이유는
    // 벡터를 한 토큰으로 쓰는 편이 스크립트에서 읽기 쉽지만, 손으로 칠 때는
    // 공백이 더 자연스러워서다.
    std::vector<float> ParseNumbers(const std::string& raw)
    {
        std::vector<float> numbers;
        std::string buffer = raw;
        for (char& c : buffer) { if (',' == c) c = ' '; }

        std::istringstream iss(buffer);
        float value = 0.f;
        while (iss >> value) numbers.push_back(value);
        return numbers;
    }

    float NumberAt(const std::vector<float>& numbers, size_t index, float fallback)
    {
        return (index < numbers.size()) ? numbers[index] : fallback;
    }

    // 리플렉션으로 프로퍼티 하나를 설정한다.
    //
    // 인스펙터(ReflectionImGuiHelper)가 하는 일과 같은 목록을 훑는다. 컴포넌트마다
    // 전용 CLI를 만들지 않는 이유가 그것이다 — 종류가 늘 때마다 두 곳을 고치게 된다.
    //
    // 부모 타입까지 올라간다. 컴포넌트 프로퍼티는 상속 계층에 흩어져 있고
    // (m_isEnabled는 Component에, m_lightType은 LightComponent에) 인스펙터도
    // 재귀로 훑는다.
    bool ApplyReflectedProperty(void* instance, const Meta::Type* type,
        const std::string& field, const std::string& raw)
    {
        if (nullptr == type) return false;

        for (const auto& prop : type->properties)
        {
            if (nullptr == prop.name || field != prop.name) continue;
            if (!prop.setter) return false;

            const HashedGuid hash = prop.typeID;
            const auto numbers = ParseNumbers(raw);

            if (hash == GUIDCreator::GetTypeID<float>())
            {
                prop.setter(instance, NumberAt(numbers, 0, 0.f));
                return true;
            }
            if (hash == GUIDCreator::GetTypeID<int>())
            {
                prop.setter(instance, static_cast<int>(NumberAt(numbers, 0, 0.f)));
                return true;
            }
            if (hash == GUIDCreator::GetTypeID<unsigned int>() || prop.typeName == "UINT")
            {
                prop.setter(instance, static_cast<unsigned int>(NumberAt(numbers, 0, 0.f)));
                return true;
            }
            if (hash == GUIDCreator::GetTypeID<bool>() || prop.typeName == "bool32")
            {
                prop.setter(instance, raw == "true" || raw == "1");
                return true;
            }
            if (hash == GUIDCreator::GetTypeID<std::string>())
            {
                prop.setter(instance, raw);
                return true;
            }
            // ★ 자산 참조(FileGuid) — 2026-08-20 추가.
            //
            // 없는 동안 **CLI로는 자산을 참조하는 컴포넌트를 저작할 수 없었다.**
            // BehaviorTreeComponent의 m_BehaviorTreeGuid·m_BlackBoardGuid가 그렇고
            // (실측: "지원하지 않는 프로퍼티 타입 ... (FileGuid)" -> 트리 0개),
            // 머티리얼·메시 참조도 같은 타입이다. 즉 BT만의 문제가 아니라 자산을
            // 가리키는 모든 필드에 걸리던 구멍이다.
            //
            // FileGuid는 문자열 생성자를 갖고(TypeTrait.h) FromString은 못 읽으면
            // 던진다 — 잘못된 값을 조용히 널 guid로 삼키지 않도록 여기서 잡아
            // 실패로 돌려준다(널 guid는 "자산 없음"이라 조용히 넘기면 컴포넌트가
            // 초기화에 실패하고 그 이유가 로그에 안 남는다).
            if (prop.typeName == "FileGuid")
            {
                try
                {
                    prop.setter(instance, FileGuid(raw));
                    return true;
                }
                catch (const std::exception&)
                {
                    std::printf("[CLI] guid 형식이 아니다: %s = '%s'\n",
                        prop.name, raw.c_str());
                    return false;
                }
            }
            if (hash == GUIDCreator::GetTypeID<math::vector2>())
            {
                prop.setter(instance, math::vector2{
                    NumberAt(numbers, 0, 0.f), NumberAt(numbers, 1, 0.f) });
                return true;
            }
            if (hash == GUIDCreator::GetTypeID<math::vector3>())
            {
                prop.setter(instance, math::vector3{
                    NumberAt(numbers, 0, 0.f), NumberAt(numbers, 1, 0.f),
                    NumberAt(numbers, 2, 0.f) });
                return true;
            }
            if (hash == GUIDCreator::GetTypeID<math::vector4>())
            {
                prop.setter(instance, math::vector4{
                    NumberAt(numbers, 0, 0.f), NumberAt(numbers, 1, 0.f),
                    NumberAt(numbers, 2, 0.f), NumberAt(numbers, 3, 1.f) });
                return true;
            }
            if (hash == GUIDCreator::GetTypeID<math::color>())
            {
                prop.setter(instance, math::color{
                    NumberAt(numbers, 0, 1.f), NumberAt(numbers, 1, 1.f),
                    NumberAt(numbers, 2, 1.f), NumberAt(numbers, 3, 1.f) });
                return true;
            }

            // 열거형은 이름으로도 숫자로도 받는다. 이름 쪽이 스크립트를 읽을 때
            // 무슨 뜻인지 바로 보인다(Directional vs 0). 열거형 점검(8-17):
            // 이름 키 등록소 조회 → 프로퍼티가 직접 든 enum 표. 등록 안 된
            // 열거형이 조용히 "지원하지 않는 타입"으로 빠지던 구멍도 함께 닫힌다.
            if (const Meta::EnumType* enumType = prop.enumType)
            {
                for (const auto& entry : enumType->values)
                {
                    if (nullptr != entry.name && raw == entry.name)
                    {
                        prop.setter(instance, entry.value);
                        return true;
                    }
                }

                if (!numbers.empty())
                {
                    prop.setter(instance, static_cast<int>(numbers[0]));
                    return true;
                }
            }

            std::printf("[CLI] 지원하지 않는 프로퍼티 타입: %s (%s)\n",
                prop.name, prop.typeName.c_str());
            return false;
        }

        return ApplyReflectedProperty(instance, type->parent, field, raw);
    }

    bool ApplyReflectedProperty(Component* component, const std::string& field,
        const std::string& raw)
    {
        if (nullptr == component) return false;
        return ApplyReflectedProperty(component, Meta::Find(component->GetTypeID().m_ID_Data), field, raw);
    }

    // 콘솔을 확보한다(GUI 앱이라 기본적으로 없다).
    //
    // 터미널에서 실행한 경우에는 그 터미널에 그대로 붙는다. Windows Terminal에서
    // 띄우면 별도 conhost 창이 뜨지 않고 그 탭에 출력된다.
    // 부모 콘솔이 없을 때만(탐색기에서 더블클릭 등) 새 콘솔을 만드는데, 이때
    // 어떤 터미널이 열리는지는 Windows 11의 "기본 터미널 앱" 설정을 따른다.
    void EnsureConsole()
    {
        if (::GetConsoleWindow() != nullptr) return;

        if (!::AttachConsole(ATTACH_PARENT_PROCESS))
        {
            if (!::AllocConsole()) return;
        }

        // 이미 파일이나 파이프로 리다이렉트된 스트림은 건드리지 않는다.
        //
        // CONOUT$로 무조건 다시 여는 코드가 `Academy_4Q.exe --exec ... > out.txt`를
        // 조용히 무력화하고 있었다 — 명령은 돌고 출력은 콘솔 창으로만 가서
        // 파일에는 아무것도 남지 않았다. 자동 검증에서는 그 출력이 결과 전부라,
        // '통과했는지 알 수 없음'과 '실패'가 구분되지 않는 상태였다.
        const auto redirected = [](DWORD stdHandle)
        {
            const HANDLE handle = ::GetStdHandle(stdHandle);
            if (nullptr == handle || INVALID_HANDLE_VALUE == handle) return false;
            const DWORD type = ::GetFileType(handle);
            return FILE_TYPE_DISK == type || FILE_TYPE_PIPE == type;
        };

        FILE* dummy = nullptr;
        if (!redirected(STD_INPUT_HANDLE))  freopen_s(&dummy, "CONIN$", "r", stdin);
        if (!redirected(STD_OUTPUT_HANDLE)) freopen_s(&dummy, "CONOUT$", "w", stdout);
        if (!redirected(STD_ERROR_HANDLE))  freopen_s(&dummy, "CONOUT$", "w", stderr);
        std::ios::sync_with_stdio(true);

        // ★ 버퍼링을 끈다.
        //
        // 씬 로드가 멈추는 것을 쫓다가 출력이 0바이트인 실행을 만났다.
        // 프로세스를 죽여도 아무것도 안 남아서, 어디까지 갔는지조차 알 수
        // 없었다 — 멈춘 자리를 찾는 일에 로그가 없는 것이 가장 나쁘다.
        //
        // 버퍼링을 끄면 느려지지만, 이 경로는 진단용 CLI라 그 대가가 싸다.
        setvbuf(stdout, nullptr, _IONBF, 0);
        setvbuf(stderr, nullptr, _IONBF, 0);

        // 로그와 명령 출력이 한글을 쓰므로 UTF-8로 맞춘다.
        // (기본 코드페이지 949에서는 소스의 UTF-8 문자열이 깨진다)
        ::SetConsoleOutputCP(CP_UTF8);
        ::SetConsoleCP(CP_UTF8);
    }
}

ConsoleCommandSystem& ConsoleCommandSystem::Get()
{
    static ConsoleCommandSystem instance;
    return instance;
}

void ConsoleCommandSystem::InitializeFromCommandLine()
{
    int argc = 0;
    LPWSTR* argv = ::CommandLineToArgvW(::GetCommandLineW(), &argc);
    if (!argv) return;

    bool wantConsole = false;

    // 표준 입력을 읽는 스레드는 --console에서만 띄운다.
    //
    // --script / --exec는 명령이 파일과 인자에서 오므로 타이핑할 사람이 없다.
    // 그런데도 예전에는 세 경우 모두 리더를 띄웠고, 그 스레드는 종료 시점에
    // getline에 갇혀 있다가 detach됐다 — 회귀 세트(전부 --script)에서만 나타난
    // 종료 구간 힙 손상(0xC0000374)의 구조적 원인이다.
    // 콘솔 자체(출력)는 세 경우 모두 필요하므로 wantConsole과는 분리한다.
    bool wantStdinReader = false;

    auto toUtf8 = [](const wchar_t* w) -> std::string
    {
        if (!w) return {};
        const int need = ::WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
        std::string s(need > 0 ? need - 1 : 0, '\0');
        if (need > 1) ::WideCharToMultiByte(CP_UTF8, 0, w, -1, s.data(), need, nullptr, nullptr);
        return s;
    };

    for (int i = 1; i < argc; ++i)
    {
        const std::string arg = toUtf8(argv[i]);

        if (arg == "--exec" && i + 1 < argc)
        {
            Enqueue(toUtf8(argv[++i]));
            wantConsole = true;
        }
        else if (arg == "--script" && i + 1 < argc)
        {
            LoadScriptFile(toUtf8(argv[++i]));
            wantConsole = true;
        }
        else if (arg == "--console")
        {
            wantConsole = true;
            wantStdinReader = true;
        }
        else if (arg == "--heapcheck")
        {
            EnableHeapValidation();
        }
    }
    ::LocalFree(argv);

    if (wantConsole)
    {
        // 스크립트로 돌리는 실행에서는 크래시 때 대화상자를 띄우지 않는다 —
        // 답할 사람이 없어 그대로 멈춰 있다가 덤프도 없이 죽는다.
        CoreWindow::SetUnattended(true);
        SuppressCrtDialogs();

        EnsureConsole();

        if (wantStdinReader)
        {
            std::printf("[CLI] 콘솔 명령 사용 가능. 'help' 입력.\n");
            StartStdinReader();
        }
    }

    // 스크립트를 못 열었는데 타이핑할 사람도 없다면, 이 실행은 아무것도 하지
    // 못한다. 계속 돌게 두면 하네스가 타임아웃으로 죽여야 하고 원인도 안 보인다.
    if (m_scriptLoadFailed && !wantStdinReader)
    {
        std::fputs("[CLI] 실행할 명령이 없어 종료한다.\n", stderr);
        std::fflush(stderr);
        m_quitRequested.store(true, std::memory_order_release);
    }
}

void ConsoleCommandSystem::StartStdinReader()
{
    if (m_running.exchange(true)) return;

    // 표준 입력은 블로킹이므로 별도 스레드에서 읽고 큐에만 넣는다.
    // 실제 실행은 Pump()가 게임 스레드에서 수행한다.
    m_stdinDone = std::promise<void>{};
    m_stdinDoneFuture = m_stdinDone.get_future();

    m_stdinThread = std::thread([this]
    {
        // 어떤 경로로 빠져나가든 종료 사실을 알린다. Shutdown이 이걸 기다린다.
        struct DoneSignal
        {
            std::promise<void>& promise;
            ~DoneSignal() { promise.set_value(); }
        } signal{ m_stdinDone };

        std::string line;
        while (m_running.load(std::memory_order_acquire) && std::getline(std::cin, line))
        {
            Enqueue(line);
        }
    });
}

void ConsoleCommandSystem::LoadScriptFile(const std::string& path)
{
    std::ifstream file(path);
    if (!file)
    {
        // 로그에만 남기면 아무도 못 본다. 실행 인자를 준 쪽은 대개 자동화라
        // 콘솔을 보고 있지 않고, 명령이 하나도 없는 에디터는 quit도 받지 못한 채
        // 그냥 계속 돈다 — 하네스가 타임아웃으로 죽을 때까지. 실제로 겪었다.
        std::fprintf(stderr, "[CLI] 스크립트를 열 수 없습니다: %s\n", path.c_str());
        std::fflush(stderr);
        Debug->LogError("[CLI] 스크립트를 열 수 없습니다: " + path);

        m_scriptLoadFailed = true;
        return;
    }

    std::string line;
    while (std::getline(file, line))
    {
        const std::string trimmed = TrimLine(line);
        if (trimmed.empty() || trimmed[0] == '#') continue;   // 주석/빈 줄 무시
        Enqueue(trimmed);
    }
}

void ConsoleCommandSystem::Enqueue(std::string command)
{
    std::lock_guard<std::mutex> guard(m_mutex);
    m_pending.push_back(std::move(command));
}

void ConsoleCommandSystem::Pump()
{
    // 생명주기 기록기의 프레임 경계(PHASE 9-0).
    //
    // 여기에 두는 이유는 이 함수가 이미 "게임 스레드에서 프레임마다 정확히 한 번"이고,
    // 그 성질을 가진 자리를 새로 만들면 엔진 루프에 진단용 호출이 하나 더 늘기 때문이다.
    // 아래 조기 반환들보다 앞이어야 한다 — wait 중이거나 씬 로딩 중인 프레임도
    // 프레임이고, 그 사이에 일어난 Awake/OnDestroy가 어느 프레임 것인지 알아야 한다.
    Lifecycle::Trace::BeginFrame();

    // wait 명령으로 보류 중이면 프레임만 소모한다.
    if (m_waitFrames > 0)
    {
        --m_waitFrames;
        return;
    }

    // 씬 로딩이 끝나기 전에는 다음 명령을 실행하지 않는다.
    // (전환 중 측정하면 중간값이 섞인다)
    if (SceneManagers->IsSceneLoading()) return;

    std::string command;
    {
        std::lock_guard<std::mutex> guard(m_mutex);
        if (m_pending.empty()) return;
        command = std::move(m_pending.front());
        m_pending.pop_front();
    }

    Execute(TrimLine(command));
}

namespace
{
    // 리플렉션 골든 덤프(PHASE 18 CT0). 등록된 전 타입을 기본 생성해
    // Meta::Serialize 출력을 한 문서로 쓴다 — 컴파일타임 전환(CT4~CT5) 동안
    // "직렬화 출력이 한 글자도 안 변했다"를 diff 0으로 증명하는 자다.
    // 씬·프리팹 콘텐츠에 기대지 않으므로 게임 데이터가 바뀌어도 흔들리지 않는다.
    void HandleReflectGolden(const std::vector<std::string>& parts)
    {
        const std::string outPath = (parts.size() > 1) ? parts[1] : std::string("reflect_golden.yaml");

        auto names = Meta::Registry::GetInstance()->GetAllTypeNames();
        std::sort(names.begin(), names.end()); // unordered_map 순회 순서를 고정한다

        MetaYml::Node root;
        int serialized = 0;
        int noFactory = 0;
        int failed = 0;
        for (const auto& name : names)
        {
            // CT11-b: 이름은 정본 Type::name의 view — yaml 키로 쓸 때만 문자열화.
            const std::string key(name);
            const Meta::Type* type = Meta::Registry::GetInstance()->Find(name);
            if (nullptr == type)
            {
                continue;
            }

            // CT11: 팩토리 접합 — Type이 생성 함수를 직접 든다.
            void* instance = type->create ? type->create() : nullptr;
            if (nullptr == instance)
            {
                // 팩토리 미등록(자동 등록 경로 밖에서 Reflect만 가진 중첩 구조체 등).
                // 누락이 아니라 커버리지 한계다 — 목록으로 남겨 diff 대상에 포함한다.
                root["__no_factory__"].push_back(key);
                ++noFactory;
                continue;
            }

            try
            {
                root[key] = Meta::Serialize(instance, *type);
                ++serialized;
            }
            catch (const std::exception& e)
            {
                root["__failed__"][key] = e.what();
                ++failed;
            }
            // instance는 의도적으로 해제하지 않는다 — Type::create에 void*
            // 파괴 경로가 없고, 이 명령은 종료 직전 시나리오에서만 쓰인다.
        }

        std::ofstream out(outPath, std::ios::binary);
        if (!out)
        {
            std::printf("[CLI] reflect.golden: 출력 파일을 열 수 없음: %s\n", outPath.c_str());
            return;
        }
        out << "# reflect.golden — 등록 전 타입 default-Serialize 덤프 (PHASE 18 CT0)\n"
            << MetaYml::Dump(root) << "\n";
        out.close();

        char line[320]{};
        std::snprintf(line, sizeof(line),
            "[reflect.golden] 타입 %zu · 직렬화 %d · 팩토리없음 %d · 실패 %d -> %s",
            names.size(), serialized, noFactory, failed, outPath.c_str());
        std::printf("[CLI] %s\n", line);
        Debug->LogWarning(line);
    }

    // 리플렉션 기준선 계측(PHASE 18 CT0). ① 활성 씬 전체 Meta::Serialize
    // (씬 저장·Instantiate·프리팹 시딩이 모두 타는 경로), ② InstantiatePrefab
    // (스폰마다 무는 역직렬화 전체 경로) 반복 시간. CT6 이후 같은 명령으로
    // 재측정해 개선을 CT0 기준선 대비 수치로 보고한다.
    void HandlePerfReflect(const std::vector<std::string>& parts)
    {
        const std::string prefabName = (parts.size() > 1) ? parts[1] : std::string("BTProbe");
        const int iterations = (parts.size() > 2) ? std::max(1, std::atoi(parts[2].c_str())) : 50;

        Scene* scene = SceneManagers->GetActiveScene();
        if (nullptr == scene)
        {
            std::printf("[CLI] perf.reflect: 활성 씬 없음\n");
            return;
        }

        using PerfClock = std::chrono::steady_clock;

        size_t objectCount = 0;
        const auto serializeStart = PerfClock::now();
        for (int i = 0; i < iterations; ++i)
        {
            objectCount = 0;
            for (const auto& obj : scene->m_Entities)
            {
                if (nullptr == obj) continue;
                MetaYml::Node node = Meta::Serialize(obj.get(), Meta::TypeOf<Entity>());
                ++objectCount;
            }
        }
        const double serializeMs =
            std::chrono::duration<double, std::milli>(PerfClock::now() - serializeStart).count()
            / iterations;

        double instantiateMs = -1.0;
        if (Prefab* prefab = PrefabUtilitys->LoadPrefab(prefabName))
        {
            const auto instantiateStart = PerfClock::now();
            for (int i = 0; i < iterations; ++i)
            {
                PrefabUtilitys->InstantiatePrefab(prefab, prefabName + "_perf" + std::to_string(i));
            }
            instantiateMs =
                std::chrono::duration<double, std::milli>(PerfClock::now() - instantiateStart).count()
                / iterations;
        }

        char line[320]{};
        std::snprintf(line, sizeof(line),
            "[perf.reflect] 반복 %d · 씬 Serialize %.3fms/회(오브젝트 %zu개) · Instantiate(%s) %.3fms/회",
            iterations, serializeMs, objectCount, prefabName.c_str(), instantiateMs);
        std::printf("[CLI] %s\n", line);
        Debug->LogWarning(line);
    }

    // 트랜스폼 값 다이제스트 (SceneGraphRedesignPlan §4 트랙 S, S1-b 선행 게이트).
    //
    // ── 왜 이 명령이 필요한가 ──
    //
    // 역직렬화기는 **모르는 키를 조용히 무시**한다(ReflectionTypedYml.h의
    // DeserializeObjectFrom이 YAML 키를 열거하지 않고 스키마 쪽 이름으로만 당겨
    // 온다 — verify-authored-rects.ps1 머리 주석이 같은 사실을 적어 뒀다). 그래서
    // S1-b가 `m_transform`을 Entity 스키마에서 빼는 순간, 승격 경로가 한 곳이라도
    // 빠지면 그 경로로 로드된 오브젝트의 위치·회전·크기가 **에러 하나 없이 사라진다**.
    //
    // 그런데 기존 회귀 세트는 그걸 못 잡는다 — prefab_roundtrip은 인스턴스 개수만
    // 세고, 값을 실제로 대조하는 검사는 UI 전용인 authored_rects 하나뿐이다.
    // 218개 자산의 형상을 바꾸면서 탐지기가 없으면, 통과는 "안 깨졌다"가 아니라
    // "확인하지 않았다"가 된다. 이 명령이 그 자를 세운다.
    //
    // 출력은 저장·재로드 전후로 그대로 비교할 수 있게 인덱스 순서 고정·고정소수점이다.
    // 소수 4자리인 이유: 이 검사가 잡으려는 것은 값의 **소실**(0/항등으로 무너짐)이지
    // 십진 왕복의 마지막 비트 흔들림이 아니다 — 더 조이면 거짓 실패가 난다.
    void HandleSceneTransformDigest(const std::vector<std::string>& parts)
    {
        Scene* scene = SceneManagers->GetActiveScene();
        if (nullptr == scene)
        {
            std::printf("[CLI] 활성 씬 없음\n");
            return;
        }

        const std::string label = (parts.size() > 1) ? parts[1] : std::string("digest");

        size_t emitted = 0;
        uint64_t hash = 1469598103934665603ull;   // FNV-1a 64 오프셋 기저
        const auto mix = [&hash](const char* text)
        {
            for (const char* p = text; *p; ++p)
            {
                hash ^= static_cast<uint8_t>(*p);
                hash *= 1099511628211ull;
            }
        };

        for (const auto& object : scene->m_Entities)
        {
            if (!object) continue;

            // 씬 루트(인덱스 0)의 이름은 씬 파일 이름을 따라간다 — 다른 이름으로
            // 저장하면 당연히 달라지므로, 그걸 비교하면 트랜스폼 데이터가 아니라
            // 파일명을 비교하는 셈이 된다(실측: 이것 하나 때문에 68줄 중 1줄이
            // 어긋났다). 루트는 저작 오브젝트가 아니라 컨테이너이므로 이름을 고정
            // 표기로 바꾼다 — 트랜스폼 값 자체는 그대로 비교한다.
            const bool isSceneRoot = (Entity::kSceneRootIndex == object->m_index);
            const std::string displayName = isSceneRoot
                ? std::string("<scene-root>") : object->m_name.ToString();

            // S3 — UI/Canvas는 Transform을 갖지 않는다. 여기서 Transform_()를
            // 그냥 부르면 폴백 로그가 UI 개수만큼 쏟아진다. 없는 것이 정상이므로
            // 표기로 남기고 넘어간다 — 이 줄이 곧 "이 오브젝트에는 공간 데이터가
            // 없다"는 적극적 확인이기도 하다(왕복 대조에도 그대로 실린다).
            if (!object->HasTransform())
            {
                char row[256]{};
                std::snprintf(row, sizeof(row), "%u|%s|%d|<no-transform>",
                    static_cast<unsigned>(object->m_index), displayName.c_str(),
                    static_cast<int>(object->GetParentIndex()));
                mix(row);
                ++emitted;
                std::printf("[tfdigest:%s] %s\n", label.c_str(), row);
                continue;
            }

            const auto& t = object->Transform_();
            char row[320]{};
            std::snprintf(row, sizeof(row),
                "%u|%s|%d|%.4f,%.4f,%.4f|%.4f,%.4f,%.4f,%.4f|%.4f,%.4f,%.4f",
                static_cast<unsigned>(object->m_index),
                displayName.c_str(),
                static_cast<int>(object->GetParentIndex()),
                t.position.x, t.position.y, t.position.z,
                t.rotation.x, t.rotation.y, t.rotation.z, t.rotation.w,
                t.scale.x, t.scale.y, t.scale.z);

            mix(row);
            ++emitted;
            std::printf("[tfdigest:%s] %s\n", label.c_str(), row);
        }

        char summary[192]{};
        std::snprintf(summary, sizeof(summary),
            "[tfdigest:%s] 합계 오브젝트 %zu · 해시 %016llx",
            label.c_str(), emitted, static_cast<unsigned long long>(hash));
        std::printf("%s\n", summary);
        Debug->LogWarning(summary);
    }

    // S4 측정 게이트 (SceneGraphRedesignPlan §4 트랙 S, S4).
    //
    // Scene::CommitRenderProxies()를 frames회 돌며 시간을 잰다. 이 단계는 매 프레임
    // 등록된 렌더 컴포넌트 **전부**에 대해 프록시 구조체를 새로 만들어 큐에 넣는다
    // — 바뀐 게 없어도 그렇다. "변경분만 커밋"이 값을 하는지 재려면 먼저 지금
    // 비용이 얼마인지 알아야 한다.
    //
    // 이 명령 혼자서는 "얼마나 빨라졌는지"를 말하지 않는다 — 최적화 전후로 같은
    // 인자로 두 번 돌려 비교하는 것이 사용법이다(미측정으로 보고할 것).
    // ── D3-b-0(SerializationPlan): 파서 동등성·속도 프로브 ─────────────────────
    //
    // yaml-cpp 소비 53파일·408매치를 옮기기 **전에** "ryml이 이 저장소의 저작 문서를
    // 같게 읽는가"를 먼저 증명한다. 옮긴 뒤에 어긋나면 파서 차이와 이행 실수를 가를 수
    // 없다. 함께 재는 파싱 시간이 D3-b 이득의 실측 상한이다(D0는 씬 로드의 60%가
    // 파싱이라고 말했다).
    //
    // 읽기 전용이라 저작 코퍼스를 오염시키지 않는다.
    void HandleSerializeParserCompare(const std::vector<std::string>& parts)
    {
        std::vector<std::string> targets;

        // 인자가 파일이면 그 하나만, `.`으로 시작하면 **확장자 필터**다. 확장자별로
        // 갈라 재야 어느 자산 종류가 파싱 비용을 쓰는지 알 수 있다 — 전체 합계만으로는
        // "부팅 catalog의 53.5 ms 중 파싱 몫이 얼마인가" 같은 질문에 답하지 못한다.
        std::string extensionFilter;
        if (parts.size() >= 2 && !parts[1].empty() && parts[1][0] == '.'
            && parts[1].find_first_of("/" "\\") == std::string::npos)
        {
            extensionFilter = parts[1];
        }

        if (parts.size() >= 2 && extensionFilter.empty())
        {
            targets.push_back(parts[1]);
        }
        else
        {
            // 저작 코퍼스 전수. 확장자는 이 저장소가 실제로 저작하는 것들이다.
            const file::path root = PathFinder::Relative();
            std::error_code error;
            file::recursive_directory_iterator it(
                root, file::directory_options::skip_permission_denied, error);
            const file::recursive_directory_iterator end;
            while (!error && it != end)
            {
                const file::directory_entry& entry = *it;
                if (entry.is_regular_file(error) && !error)
                {
                    const std::string ext = entry.path().extension().string();
                    const bool matched = extensionFilter.empty()
                        ? (ext == ".creator" || ext == ".prefab" || ext == ".asset"
                            || ext == ".meta" || ext == ".shadermeta" || ext == ".volume")
                        : (ext == extensionFilter);
                    if (matched)
                    {
                        targets.push_back(entry.path().string());
                    }
                }
                error.clear();
                it.increment(error);
            }
        }

        if (targets.empty())
        {
            std::printf("[serialize.parsercompare] selfcheck=fail reason=no-targets\n");
            return;
        }

        int equalCount = 0;
        int diffCount = 0;
        int parseFailCount = 0;
        int crlfCount = 0;
        int skippedBinary = 0;
        unsigned long long totalNodes = 0;
        double totalYamlUs = 0.0;
        double totalRymlUs = 0.0;

        for (const std::string& target : targets)
        {
            const Authoring::ParserProbeResult probe = Authoring::ProbeParsers(target);
            totalYamlUs += static_cast<double>(probe.yamlCppNanoseconds) / 1000.0;
            totalRymlUs += static_cast<double>(probe.rymlNanoseconds) / 1000.0;
            totalNodes += probe.comparedNodes;
            if (probe.skippedBinaryAsset) { ++skippedBinary; continue; }
            if (probe.normalizedCrLf) ++crlfCount;

            if (!probe.parsedByYamlCpp || !probe.parsedByRyml)
            {
                ++parseFailCount;
                std::printf("[serialize.parsercompare] PARSE-FAIL yamlcpp=%d ryml=%d file=%s reason=%s\n",
                    probe.parsedByYamlCpp ? 1 : 0, probe.parsedByRyml ? 1 : 0,
                    target.c_str(), probe.rymlError.c_str());
                continue;
            }
            if (probe.structurallyEqual)
            {
                ++equalCount;
            }
            else
            {
                ++diffCount;
                std::printf("[serialize.parsercompare] DIFF file=%s at=%s\n",
                    target.c_str(), probe.firstDifference.c_str());
            }
        }

        std::printf("[serialize.parsercompare] files=%zu equal=%d diff=%d parseFail=%d nodes=%llu\n",
            targets.size(), equalCount, diffCount, parseFailCount, totalNodes);
        // ryml 몫에는 CRLF 정규화 비용이 포함돼 있다 — 그것이 전제이기 때문이다.
        // 건너뛴 바이너리는 "문제없음"이 아니라 "확인하지 않음"이다.
        std::printf("[serialize.parsercompare] crlfNormalized=%d/%zu skippedBinary=%d\n",
            crlfCount, targets.size(), skippedBinary);
        std::printf("[serialize.parsercompare] yamlCppUs=%.1f rymlUs=%.1f speedup=%.2fx\n",
            totalYamlUs, totalRymlUs,
            (totalRymlUs > 0.0) ? (totalYamlUs / totalRymlUs) : 0.0);

        // 0개를 비교하고 "차이 0"을 통과로 읽지 않는다.
        const char* fail = nullptr;
        if (0 == targets.size())      fail = "no-targets";
        else if (0 == totalNodes)     fail = "compared-zero-nodes";
        else if (parseFailCount > 0)  fail = "parse-failed";
        else if (diffCount > 0)       fail = "structural-diff";
        std::printf("[serialize.parsercompare] selfcheck=%s%s%s\n",
            (nullptr == fail) ? "pass" : "fail",
            (nullptr == fail) ? "" : " reason=",
            (nullptr == fail) ? "" : fail);
    }

    // -- D3-b-1(SerializationPlan): ryml 에러 정책이 실제로 abort를 막는가 --
    //
    // ★ 이 검사는 "실패하면 빨개진다"가 아니라 **"정책이 없으면 크래시한다"**로
    //   이빨을 갖는다. ryml은 잘못된 문서를 만나면 예외가 아니라 프로세스를
    //   abort하므로, 콜백이 빠지거나 채널 하나를 놓치면 이 명령은 종료 코드가
    //   아니라 프로세스 사망으로 끝난다. 게이트는 그것도 실패로 읽는다.
    void HandleSerializeRymlError(const std::vector<std::string>& parts)
    {
        // 인자가 있으면 그 파일을 **있는 그대로** ryml에 넣는다. 무엇이 실제로
        // abort를 일으키는지는 지어내지 말고 재야 한다 — 처음 만든 합성 재현
        // 둘을 ryml이 조용히 받아들였다. 프로세스가 죽으면 그것이 답이다.
        if (parts.size() >= 2)
        {
            std::ifstream input(parts[1], std::ios::binary);
            if (!input)
            {
                std::printf("[serialize.rymlerror] probe=fail reason=open-failed path=%s\n",
                    parts[1].c_str());
                return;
            }
            const std::string text((std::istreambuf_iterator<char>(input)),
                std::istreambuf_iterator<char>());
            const Authoring::RymlParseAttempt attempt = Authoring::TryParseWithPolicy(text);
            std::printf("[serialize.rymlerror] probe bytes=%zu parsed=%d threw=%d nodes=%llu\n",
                text.size(), attempt.parsed ? 1 : 0, attempt.threw ? 1 : 0,
                static_cast<unsigned long long>(attempt.nodeCount));
            std::printf("[serialize.rymlerror] probeMessage=%s\n",
                attempt.message.empty() ? "(none)" : attempt.message.c_str());
            return;
        }

        const Authoring::RymlErrorPolicyProbe probe = Authoring::ProbeRymlErrorPolicy();

        std::printf("[serialize.rymlerror] loneCr=%d tabIndent=%d distinctChannels=%d\n",
            probe.threwOnLoneCr ? 1 : 0,
            probe.threwOnTabIndent ? 1 : 0,
            probe.coveredDistinctChannels ? 1 : 0);
        std::printf("[serialize.rymlerror] validParsed=%d crlfParsed=%d\n",
            probe.parsedValidDocument ? 1 : 0,
            probe.parsedCrLfDocument ? 1 : 0);
        std::printf("[serialize.rymlerror] firstMessage=%s\n",
            probe.firstMessage.empty() ? "(none)" : probe.firstMessage.c_str());

        const char* fail = nullptr;
        if (!probe.threwOnLoneCr)          fail = "lone-cr-did-not-throw";
        else if (!probe.threwOnTabIndent)  fail = "tab-indent-did-not-throw";
        // 두 실패가 같은 채널을 탔다면 나머지 채널은 여전히 abort할 수 있다.
        else if (!probe.coveredDistinctChannels) fail = "single-channel-only";
        // 대조군: 정책이 모든 파싱을 막아 버린 상태를 통과로 읽지 않는다.
        else if (!probe.parsedValidDocument) fail = "valid-document-rejected";
        // CRLF가 거부되면 정규화 사본이 다시 필요하다 — 성능 전제가 바뀐다.
        else if (!probe.parsedCrLfDocument)  fail = "crlf-document-rejected";
        // 예외는 왔는데 메시지가 비면 실제 실패에서 원인을 못 읽는다.
        else if (probe.firstMessage.empty()) fail = "empty-error-message";

        std::printf("[serialize.rymlerror] selfcheck=%s%s%s\n",
            (nullptr == fail) ? "pass" : "fail",
            (nullptr == fail) ? "" : " reason=",
            (nullptr == fail) ? "" : fail);
    }

    // -- D3-b-2(SerializationPlan): 스칼라 **변환** 파리티 --
    //
    // 구조 파리티(D3-b-0)는 스칼라를 문자열로만 비교했다. 그것으로는 `as<bool>`이
    // "yes"를 어떻게 읽는지가 증명되지 않는다 — 두 파서가 다르게 읽으면 트리는
    // 같은데 **값의 의미만 조용히 달라진다.** 로드는 성공하고 값만 틀린다.
    void HandleSerializeScalarParity(const std::vector<std::string>&)
    {
        const Authoring::ScalarParityResult result = Authoring::ProbeScalarConversions();

        for (const Authoring::ScalarParityCase& entry : result.cases)
        {
            if (!entry.converterAgrees)
            {
                // 이식 변환기의 차이는 **허용되지 않는다.** 먼저 찍는다.
                std::printf("[serialize.scalarparity] CONV-DIVERGE %-16s type=%-6s yamlcpp=%s(%s) conv=%s(%s)\n",
                    entry.name.c_str(), entry.type.c_str(),
                    entry.yamlCppOk ? "ok" : "fail", entry.yamlCppValue.c_str(),
                    entry.converterOk ? "ok" : "fail", entry.converterValue.c_str());
            }
        }
        for (const Authoring::ScalarParityCase& entry : result.cases)
        {
            if (entry.agrees) continue;
            std::printf("[serialize.scalarparity] DIVERGE %-16s type=%-6s yamlcpp=%s(%s) ryml=%s(%s)\n",
                entry.name.c_str(), entry.type.c_str(),
                entry.yamlCppOk ? "ok" : "fail", entry.yamlCppValue.c_str(),
                entry.rymlOk ? "ok" : "fail", entry.rymlValue.c_str());
        }

        std::printf("[serialize.scalarparity] cases=%zu agree=%u diverge=%u convDiverge=%u\n",
            result.cases.size(), result.agreeCount, result.divergeCount,
            result.converterDivergeCount);

        const char* fail = nullptr;
        if (result.cases.empty()) fail = "no-cases";
        // 이식 변환기가 yaml-cpp와 다르면 D3-b-2b-1a가 실패한 것이다.
        else if (result.converterDivergeCount > 0) fail = "converter-diverges";
        std::printf("[serialize.scalarparity] selfcheck=%s%s%s\n",
            (nullptr == fail) ? "pass" : "fail",
            (nullptr == fail) ? "" : " reason=",
            (nullptr == fail) ? "" : fail);
    }

    // -- D3-b-2b-1b-3a(SerializationPlan): 어댑터 수준 파리티 --
    //
    // 파서 파리티(트리)와 스칼라 파리티(값)가 증명하지 못하는 축이다. 소비자가
    // 실제로 부르는 것은 어댑터 연산이고, 두 backend의 비대칭(맵 키가 노드인가
    // 속성인가, 널이 타입인가 값 표기인가)을 어댑터가 옳게 흡수했는지는
    // 같은 문서를 양쪽에 넣어 봐야만 알 수 있다.
    void HandleSerializeAdapterParity(const std::vector<std::string>& parts)
    {
        std::vector<std::string> targets;
        if (parts.size() >= 2)
        {
            targets.push_back(parts[1]);
        }
        else
        {
            const file::path root = PathFinder::Relative();
            std::error_code error;
            file::recursive_directory_iterator it(
                root, file::directory_options::skip_permission_denied, error);
            const file::recursive_directory_iterator end;
            while (!error && it != end)
            {
                const file::directory_entry& entry = *it;
                if (entry.is_regular_file(error) && !error)
                {
                    const std::string ext = entry.path().extension().string();
                    if (ext == ".creator" || ext == ".prefab" || ext == ".asset"
                        || ext == ".meta" || ext == ".shadermeta" || ext == ".volume")
                    {
                        targets.push_back(entry.path().string());
                    }
                }
                error.clear();
                it.increment(error);
            }
        }

        const Authoring::AdapterParityResult result = Authoring::ProbeAdapterParity(targets);

        std::printf("[serialize.adapterparity] files=%u nodes=%llu mapEntries=%llu diverge=%u\n",
            result.files,
            static_cast<unsigned long long>(result.comparedNodes),
            static_cast<unsigned long long>(result.comparedMapEntries),
            result.divergences);
        std::printf("[serialize.adapterparity] skippedBinary=%u parseFailures=%u\n",
            result.skippedBinary, result.parseFailures);
        if (!result.firstDivergence.empty())
        {
            std::printf("[serialize.adapterparity] first=%s\n", result.firstDivergence.c_str());
        }

        const char* fail = nullptr;
        if (0 == result.files)               fail = "no-files";
        else if (0 == result.comparedNodes)  fail = "compared-zero-nodes";
        // 맵 순회를 한 번도 안 했다면 키 비대칭을 검사하지 않은 것이다.
        else if (0 == result.comparedMapEntries) fail = "compared-zero-map-entries";
        else if (result.parseFailures > 0)   fail = "parse-mismatch";
        else if (result.divergences > 0)     fail = "adapter-diverges";
        std::printf("[serialize.adapterparity] selfcheck=%s%s%s\n",
            (nullptr == fail) ? "pass" : "fail",
            (nullptr == fail) ? "" : " reason=",
            (nullptr == fail) ? "" : fail);
    }

    // ── D3-a-1(SerializationPlan): 저작 노드 구조 비교 계약 ────────────────────
    //
    // 이 검사가 재는 것은 성능이 아니라 **판정 규칙**이다. 구조 비교는 Dump 비교의
    // 동작을 그대로 옮기지 않는다 — 맵 키 순서를 무시하는 것이 의도된 차이이므로,
    // 그 차이를 검사가 직접 단정해 "실수로 바뀐 것"과 구분한다.
    void HandleSerializeNodeEqual(const std::vector<std::string>&)
    {
        struct Case
        {
            const char* name;
            const char* lhs;
            const char* rhs;
            bool expectedEqual;
            bool dumpAgrees;   // Dump 비교도 같은 답을 내는가
        };

        // dumpAgrees=false인 항목이 이 슬라이스가 바꾼 동작이다. 그런 항목이 0개면
        // 구조 비교를 넣을 이유가 없었다는 뜻이므로, 아래에서 그 수도 단정한다.
        static const Case kCases[] = {
            { "scalar-same",        "a: 1",                  "a: 1",                  true,  true  },
            { "scalar-diff",        "a: 1",                  "a: 2",                  false, true  },
            { "scalar-notation",    "a: 1",                  "a: 1.0",                false, true  },
            { "seq-same",           "a: [1, 2]",             "a: [1, 2]",             true,  true  },
            { "seq-order-matters",  "a: [1, 2]",             "a: [2, 1]",             false, true  },
            { "seq-length",         "a: [1, 2]",             "a: [1, 2, 3]",          false, true  },
            { "map-key-order",      "a: {x: 1, y: 2}",       "a: {y: 2, x: 1}",       true,  false },
            { "map-style",          "a: {x: 1}",             "a:\n  x: 1",            true,  false },
            { "map-missing-key",    "a: {x: 1, y: 2}",       "a: {x: 1}",             false, true  },
            { "map-value-diff",     "a: {x: 1}",             "a: {x: 2}",             false, true  },
            { "nested-key-order",   "a: {p: {m: 1, n: 2}}",  "a: {p: {n: 2, m: 1}}",  true,  false },
            { "null-vs-null",       "a: ~",                  "a: null",               true,  true  },
            { "null-vs-scalar",     "a: ~",                  "a: 0",                  false, true  },
            { "type-mismatch",      "a: [1]",                "a: {x: 1}",             false, true  },
        };

        int passed = 0;
        int failed = 0;
        int divergedFromDump = 0;
        for (const Case& testCase : kCases)
        {
            bool actual = false;
            bool dumpResult = false;
            try
            {
                const MetaYml::Node lhsDoc = MetaYml::Load(testCase.lhs);
                const MetaYml::Node rhsDoc = MetaYml::Load(testCase.rhs);
                actual = Authoring::NodesEqual(lhsDoc["a"], rhsDoc["a"]);
                dumpResult = (MetaYml::Dump(lhsDoc["a"]) == MetaYml::Dump(rhsDoc["a"]));
            }
            catch (const std::exception& exception)
            {
                std::printf("[serialize.nodeequal] case=%s FAIL(parse) %s\n",
                    testCase.name, exception.what());
                ++failed;
                continue;
            }

            const bool caseOk = (actual == testCase.expectedEqual);
            // 예상한 곳에서 예상한 만큼만 Dump와 갈리는지 함께 본다.
            const bool divergenceOk =
                ((dumpResult == testCase.expectedEqual) == testCase.dumpAgrees);
            if (!testCase.dumpAgrees) ++divergedFromDump;

            if (caseOk && divergenceOk)
            {
                ++passed;
            }
            else
            {
                ++failed;
                std::printf("[serialize.nodeequal] case=%s FAIL structural=%d expected=%d dump=%d dumpAgreesExpected=%d\n",
                    testCase.name, actual ? 1 : 0, testCase.expectedEqual ? 1 : 0,
                    dumpResult ? 1 : 0, testCase.dumpAgrees ? 1 : 0);
            }
        }

        std::printf("[serialize.nodeequal] cases=%d passed=%d failed=%d divergedFromDump=%d\n",
            static_cast<int>(std::size(kCases)), passed, failed, divergedFromDump);

        if (0 == divergedFromDump)
        {
            // 전부 Dump와 같은 답이면 이 슬라이스는 아무것도 바꾸지 않은 것이다.
            std::printf("[serialize.nodeequal] selfcheck=fail reason=no-divergence-covered\n");
            return;
        }
        std::printf("[serialize.nodeequal] selfcheck=%s\n", (0 == failed) ? "pass" : "fail");
    }

    // ── D0(SerializationPlan): 직렬화 기준선 ───────────────────────────────────
    //
    // 이 명령이 재는 것은 벤치가 재현한 모형이 아니라 제품 로드 경로 그 자체다
    // (SerializationProfiler의 Scope가 SceneManager/ComponentFactory/PrefabUtility
    // 본체에 들어 있다). dx12.encoderbench가 모형만 재고 있던 전례를 반복하지 않기
    // 위한 선택이고, 대가로 계측 플래그가 켜져 있는 동안 로드 경로에 시계 읽기가
    // 얹힌다 — 그래서 기본값은 꺼짐이고 이 명령이 켰다가 반드시 되돌린다.
    //
    // 출력은 회귀 스크립트가 파싱한다. key=value 형식을 바꾸면 게이트가 눈을 감는다.
    void PrintSerializationStage(const char* mode,
        SerializationProfile::Stage stage,
        const SerializationProfile::Snapshot& snapshot,
        int iterations)
    {
        const auto& sample = snapshot[stage];
        const double totalUs = static_cast<double>(sample.nanoseconds) / 1000.0;
        // perIterUs는 "반복 1회분"이지 "호출 1회분"이 아니다. calls는 그 회차 안에서
        // 몇 번 불렸는지를 따로 말한다(예: 씬 하나에 EntityDeserialize 68회).
        // 두 값을 같은 것으로 읽으면 단계별 비중을 통째로 오해한다.
        std::printf("[serialize.bench] mode=%s stage=%s totalUs=%.3f perIterUs=%.3f calls=%llu\n",
            mode,
            std::string(SerializationProfile::StageName(stage)).c_str(),
            totalUs,
            totalUs / static_cast<double>((std::max)(1, iterations)),
            static_cast<unsigned long long>(sample.calls));
    }

    // 부팅 구간은 CLI가 켜기 전에 끝난다 — 별도 슬롯에서 읽는다.
    void PrintSerializationBoot()
    {
        const auto boot = SerializationProfile::TakeBoot();
        const auto& catalog = boot[SerializationProfile::Stage::AssetCatalog];
        const double ms = static_cast<double>(catalog.nanoseconds) / 1'000'000.0;
        std::printf("[serialize.bench] mode=boot stage=AssetCatalog totalMs=%.3f parsedMeta=%llu\n",
            ms, static_cast<unsigned long long>(catalog.calls));
        if (0 == catalog.calls)
        {
            // 0개를 재고 "빠르다"고 보고하는 사고를 막는다.
            std::printf("[serialize.bench] mode=boot selfcheck=fail reason=parsed-meta-zero\n");
            return;
        }
        // ★ selfcheck는 언제나 독립 라인이다. 처음에는 perMetaUs와 같은 줄에 붙였는데,
        //   게이트의 `mode=X selfcheck=Y` 정규식이 이 한 줄만 놓쳐 "selfcheck 3개"로
        //   세었다 — 검사가 계약을 못 읽고 스스로 빨개진 형태다. 형식은 계약이다.
        std::printf("[serialize.bench] mode=boot perMetaUs=%.3f\n",
            (ms * 1000.0) / static_cast<double>(catalog.calls));
        std::printf("[serialize.bench] mode=boot selfcheck=pass\n");
    }

    // ★ 구성 표기는 장식이 아니다. Debug는 같은 조건에서 배 단위로 느리고 단계 간
    //   비중까지 뒤집는다 — 어떤 exe로 잰 값인지 출력이 스스로 말하게 해서, Debug
    //   수치가 기준선처럼 굳는 사고를 막는다.
    constexpr const char* kSerializeBenchConfig =
#if defined(NDEBUG)
        "Release";
#else
        "Debug";
#endif

    void HandleSerializeBench(const std::vector<std::string>& parts)
    {
        std::printf("[serialize.bench] config=%s\n", kSerializeBenchConfig);
        if (parts.size() < 2)
        {
            std::printf("[CLI] 사용법: serialize.bench boot\n");
            std::printf("[CLI]         serialize.bench scene <절대경로> [반복=3]\n");
            std::printf("[CLI]         serialize.bench prefab <이름|경로> [반복=3]\n");
            return;
        }

        const std::string mode = parts[1];

        if ("boot" == mode)
        {
            PrintSerializationBoot();
            return;
        }

        if (parts.size() < 3)
        {
            std::printf("[CLI] serialize.bench %s: 대상이 없다\n", mode.c_str());
            return;
        }

        const std::string target = parts[2];
        const int iterations = (parts.size() >= 4)
            ? (std::max)(1, std::atoi(parts[3].c_str()))
            : 3;

        if ("scene" == mode)
        {
            // 워밍업 1회는 계측을 끈 채로 돈다 — 첫 로드의 지연 초기화(프리팹 캐시,
            // 애셋 상주화)를 평균에 섞지 않기 위해서다. 워밍업을 빼면 1회차만
            // 유별나게 큰 값이 평균을 지배한다.
            SerializationProfile::SetEnabled(false);
            if (nullptr == SceneManagers->LoadSceneImmediate(target))
            {
                std::printf("[serialize.bench] mode=scene selfcheck=fail reason=warmup-load-failed target=%s\n",
                    target.c_str());
                return;
            }

            SerializationProfile::Reset();
            SerializationProfile::SetEnabled(true);
            int loaded = 0;
            for (int i = 0; i < iterations; ++i)
            {
                if (nullptr != SceneManagers->LoadSceneImmediate(target))
                    ++loaded;
            }
            SerializationProfile::SetEnabled(false);

            const auto snapshot = SerializationProfile::Take();
            std::printf("[serialize.bench] mode=scene target=%s iterations=%d loaded=%d warmup=1\n",
                target.c_str(), iterations, loaded);

            using Stage = SerializationProfile::Stage;
            for (uint32_t i = 0; i < SerializationProfile::kStageCount; ++i)
            {
                const Stage stage = static_cast<Stage>(i);
                if (Stage::AssetCatalog == stage) continue; // 부팅 전용 슬롯
                PrintSerializationStage("scene", stage, snapshot, iterations);
            }

            // ★ 자를 먼저 검증한다. 분해 합이 루트를 넘으면 계측 자체가 틀린 것이고,
            //   그 경우 아래 수치는 어떤 판정에도 쓸 수 없다.
            const double rootUs =
                static_cast<double>(snapshot[Stage::SceneLoadTotal].nanoseconds) / 1000.0;
            double childUs = 0.0;
            bool everyChildRan = true;
            for (uint32_t i = 0; i < SerializationProfile::kStageCount; ++i)
            {
                const Stage stage = static_cast<Stage>(i);
                if (!SerializationProfile::IsSceneLoadChild(stage)) continue;
                childUs += static_cast<double>(snapshot[stage].nanoseconds) / 1000.0;
                if (0 == snapshot[stage].calls) everyChildRan = false;
            }

            const double unattributedUs = rootUs - childUs;
            std::printf("[serialize.bench] mode=scene rootUs=%.3f childSumUs=%.3f unattributedUs=%.3f childRatio=%.4f\n",
                rootUs, childUs, unattributedUs,
                (rootUs > 0.0) ? (childUs / rootUs) : 0.0);

            const char* failReason = nullptr;
            if (loaded != iterations)                             failReason = "load-count-mismatch";
            else if (snapshot[Stage::SceneLoadTotal].calls !=
                     static_cast<uint64_t>(iterations))           failReason = "root-call-mismatch";
            else if (!everyChildRan)                              failReason = "child-stage-zero-calls";
            else if (childUs > rootUs)                            failReason = "child-sum-exceeds-root";
            else if (rootUs <= 0.0)                               failReason = "root-zero-time";

            if (nullptr != failReason)
                std::printf("[serialize.bench] mode=scene selfcheck=fail reason=%s\n", failReason);
            else
                std::printf("[serialize.bench] mode=scene selfcheck=pass\n");
            return;
        }

        if ("prefab" == mode)
        {
            SerializationProfile::Reset();
            SerializationProfile::SetEnabled(true);

            // 캐시 미스는 최초 1회뿐이다 — 그 사실을 숨기지 않고 cacheMiss로 보고한다.
            Prefab* prefab = PrefabUtilitys->LoadPrefab(target);
            const auto afterLoad = SerializationProfile::Take();
            if (nullptr == prefab)
            {
                SerializationProfile::SetEnabled(false);
                std::printf("[serialize.bench] mode=prefab selfcheck=fail reason=load-failed target=%s\n",
                    target.c_str());
                return;
            }

            int instantiated = 0;
            for (int i = 0; i < iterations; ++i)
            {
                if (nullptr != PrefabUtilitys->InstantiatePrefab(
                        prefab, target + "_d0bench" + std::to_string(i)))
                    ++instantiated;
            }
            SerializationProfile::SetEnabled(false);

            const auto snapshot = SerializationProfile::Take();
            using Stage = SerializationProfile::Stage;
            // parseOnLoad는 LoadPrefab이 문서를 실제로 읽은 횟수(캐시 미스), nestedParse는
            // 소환 도중 중첩 프리팹 정의를 추가로 읽은 횟수다. 둘을 합쳐 cacheMiss 하나로
            // 보고했더니 "1이라고 했는데 2가 찍힌" 모순으로 보였다 — 계측이 틀린 게 아니라
            // 소환이 leaf 정의를 더 읽은 것이었고, 그 사실이 보이는 편이 옳다.
            const unsigned long long parseOnLoad =
                static_cast<unsigned long long>(afterLoad[Stage::PrefabParse].calls);
            const unsigned long long totalParse =
                static_cast<unsigned long long>(snapshot[Stage::PrefabParse].calls);
            std::printf("[serialize.bench] mode=prefab target=%s iterations=%d instantiated=%d parseOnLoad=%llu nestedParse=%llu\n",
                target.c_str(), iterations, instantiated,
                parseOnLoad,
                (totalParse >= parseOnLoad) ? (totalParse - parseOnLoad) : 0ull);
            PrintSerializationStage("prefab", Stage::PrefabParse, snapshot, 1);
            PrintSerializationStage("prefab", Stage::PrefabInstantiate, snapshot, iterations);
            PrintSerializationStage("prefab", Stage::ComponentLoad, snapshot, iterations);

            const char* failReason = nullptr;
            if (instantiated != iterations)                              failReason = "instantiate-count-mismatch";
            else if (snapshot[Stage::PrefabInstantiate].calls !=
                     static_cast<uint64_t>(iterations))                  failReason = "instantiate-call-mismatch";
            else if (0 == snapshot[Stage::PrefabInstantiate].nanoseconds) failReason = "instantiate-zero-time";
            // ★ 이 분기는 변이 실험이 열었다. ComponentLoad 계측을 제거한 변이본에서
            //   scene 모드는 fail을 냈는데 prefab 모드는 그대로 통과했다 — 소환은
            //   반드시 컴포넌트를 붙이므로 0 calls는 계측이 끊어졌다는 뜻이다.
            else if (0 == snapshot[Stage::ComponentLoad].calls)          failReason = "component-load-zero-calls";

            if (nullptr != failReason)
                std::printf("[serialize.bench] mode=prefab selfcheck=fail reason=%s\n", failReason);
            else
                std::printf("[serialize.bench] mode=prefab selfcheck=pass\n");
            return;
        }

        std::printf("[CLI] serialize.bench: 알 수 없는 모드 '%s'\n", mode.c_str());
    }

    void HandleSceneProxyBench(const std::vector<std::string>& parts)
    {
        if (parts.size() < 2)
        {
            std::printf("[CLI] 사용법: scene.proxybench <프레임수>\n");
            return;
        }

        const int frames = (std::max)(1, std::atoi(parts[1].c_str()));

        Scene* scene = SceneManagers->GetActiveScene();
        if (nullptr == scene) { std::printf("[CLI] 활성 씬 없음\n"); return; }

        const size_t componentCount = scene->RenderProxyComponentCount();
        if (0 == componentCount)
        {
            // 0개를 재고 "빠르다"고 보고하는 사고를 막는다 — 하한 가드가 없으면
            // 빈 씬에서 측정이 눈을 감는다(회귀 세트의 README 원칙과 같은 이유).
            std::printf("[CLI] scene.proxybench: 등록된 렌더 컴포넌트가 0개다 — 잴 것이 없다\n");
            return;
        }

        using PerfClock = std::chrono::steady_clock;
        scene->CommitRenderProxies();   // 워밍업 1회(첫 호출의 지연 초기화를 평균에서 뺀다)

        std::vector<double> samplesUs;
        samplesUs.reserve(static_cast<size_t>(frames));
        for (int f = 0; f < frames; ++f)
        {
            const auto t0 = PerfClock::now();
            scene->CommitRenderProxies();
            const auto t1 = PerfClock::now();
            samplesUs.push_back(std::chrono::duration<double, std::micro>(t1 - t0).count());
        }

        double sum = 0.0;
        double minUs = samplesUs.front();
        double maxUs = samplesUs.front();
        for (double v : samplesUs)
        {
            sum += v;
            minUs = (std::min)(minUs, v);
            maxUs = (std::max)(maxUs, v);
        }
        const double avg = sum / static_cast<double>(samplesUs.size());

        char line[256]{};
        std::snprintf(line, sizeof(line),
            "[scene.proxybench] 컴포넌트 %zu개 · 평균 %.2fus 최소 %.2fus 최대 %.2fus (프레임 %d) · 컴포넌트당 %.3fus",
            componentCount, avg, minUs, maxUs, frames, avg / static_cast<double>(componentCount));
        std::printf("%s\n", line);
        Debug->LogWarning(line);
    }

    // S2 A/B 토글의 유일한 쓰기 지점(SceneGraphRedesignPlan §4 트랙 S, S2 —
    // Scene::SetDirtyTraversalEnabled). 인자 없이 부르면 현재값만 보여준다.
    void HandleSceneDirtyTraversal(const std::vector<std::string>& parts)
    {
        if (parts.size() < 2)
        {
            std::printf("[CLI] scene.dirtytraversal 현재값: %s (사용법: scene.dirtytraversal 0|1)\n",
                Scene::IsDirtyTraversalEnabled() ? "1(dirty만 재계산)" : "0(항상 재계산 — 옛 경로)");
            return;
        }

        const bool enable = ("1" == parts[1] || "on" == parts[1] || "true" == parts[1]);
        const bool disable = ("0" == parts[1] || "off" == parts[1] || "false" == parts[1]);
        if (!enable && !disable)
        {
            std::printf("[CLI] 사용법: scene.dirtytraversal 0|1\n");
            return;
        }

        Scene::SetDirtyTraversalEnabled(enable);
        const std::string msg = std::string("[scene.dirtytraversal] ")
            + (enable ? "켬(1) — dirty·worldChanged만 재계산" : "끔(0) — 항상 재계산(옛 경로, A/B 대조용)");
        Debug->LogWarning(msg);
        std::printf("[CLI] %s\n", msg.c_str());
    }

    // E7-b A/B 토글의 유일한 쓰기 지점(SceneGraphRedesignPlan 트랙 E, E7-b —
    // Scene::SetBoneCacheEnabled). 인자 없이 부르면 현재값만 보여준다.
    void HandleSceneBoneCache(const std::vector<std::string>& parts)
    {
        if (parts.size() < 2)
        {
            std::printf("[CLI] scene.bonecache 현재값: %s (사용법: scene.bonecache 0|1)\n",
                Scene::IsBoneCacheEnabled() ? "1(인덱스 캐시)" : "0(매 프레임 FindBone — 옛 경로)");
            return;
        }

        const bool enable = ("1" == parts[1] || "on" == parts[1] || "true" == parts[1]);
        const bool disable = ("0" == parts[1] || "off" == parts[1] || "false" == parts[1]);
        if (!enable && !disable)
        {
            std::printf("[CLI] 사용법: scene.bonecache 0|1\n");
            return;
        }

        Scene::SetBoneCacheEnabled(enable);
        const std::string msg = std::string("[scene.bonecache] ")
            + (enable ? "켬(1) — 뼈 인덱스 캐시 적중 시 FindBone 생략"
                      : "끔(0) — 매 프레임 FindBone 선형 탐색(옛 경로, A/B 대조용)");
        Debug->LogWarning(msg);
        std::printf("[CLI] %s\n", msg.c_str());
    }

    // 뼈 이름 조회와 순회 도달성을 한 화면에 놓고 대조하는 진단(트랙 E, E7-b 후속).
    //
    // ── 왜 이 명령이 필요했나 ──
    //
    // scene.traversalbench 0이 "분기 도달 61개 · 인덱스 해석 0개"를 찍었다. 그 한 줄만
    // 보면 Skeleton::FindBone이 이름을 못 찾는 것으로 읽힌다 — FindBone은 못 찾는 것을
    // 정상 경로로 취급해 로그를 한 줄도 안 남기니 확인할 방법도 없었다. 실제로는
    // 이름이 61/61 전부 맞았고, 진짜 원인은 그 위였다: 모델 루트가 씬 루트의
    // m_childrenIndices에서 빠져 AllUpdateWorldMatrix가 서브트리를 통째로 건너뛰고
    // 있었다(SceneManager::RemapLoadBatchIndices — 거기 주석에 경위를 적어 뒀다).
    //
    // 그래서 이 명령은 둘을 **나란히** 찍는다. 이름 조회(FindBone 직접 호출)와
    // 순회 도달성(조상 사슬이 전부 부모의 children에 실려 있는가)은 서로 다른
    // 고장이고, 어느 하나만 보면 반드시 오진한다.
    void HandleSceneBoneDump(const std::vector<std::string>& parts)
    {
        Scene* scene = SceneManagers->GetActiveScene();
        if (nullptr == scene) { std::printf("[CLI] 활성 씬 없음\n"); return; }

        const int limit = (parts.size() >= 2) ? (std::max)(0, std::atoi(parts[1].c_str())) : 8;

        // 눈에 안 보이는 차이(앞뒤 공백·제어문자·비ASCII)를 드러내는 표기 — 이름
        // 불일치의 형태를 확정하려면 바이트가 보여야 한다.
        auto quote = [](const std::string& s)
        {
            std::string out = "\"";
            for (unsigned char ch : s)
            {
                if (ch >= 0x20 && ch < 0x7F) { out += static_cast<char>(ch); }
                else { char buf[8]{}; std::snprintf(buf, sizeof(buf), "<%02X>", ch); out += buf; }
            }
            out += "\"";
            return out;
        };

        // 이 노드가 AllUpdateWorldMatrix의 순회에 실제로 닿는가. 순회는
        // m_Entities[0]->m_childrenIndices에서만 내려가므로, 조상 사슬의
        // **모든** 고리가 "부모의 children에 내가 실려 있다"를 만족해야 한다.
        // m_parentIndex만 따라 올라가는 검사는 이번 결함을 통과시킨다(부모 포인터는
        // 멀쩡했고 부모의 목록에서만 빠져 있었다) — 반드시 목록 쪽을 본다.
        auto brokenLinkOf = [&](Entity::Index start) -> Entity::Index
        {
            Entity::Index cur = start;
            for (int hop = 0; hop < 256; ++hop)
            {
                const auto& node = scene->TryGetEntity(cur);
                if (!node) return cur;
                if (Entity::kSceneRootIndex == node->m_index) return Entity::INVALID_INDEX;

                const Entity::Index parentIndex = node->GetParentIndex();
                const auto& parentObj = scene->TryGetEntity(parentIndex);
                if (!parentObj) return cur;
                const auto& sib = parentObj->GetChildrenIndices();
                if (std::find(sib.begin(), sib.end(), node->m_index) == sib.end()) return cur;

                if (parentIndex == cur) return cur;
                cur = parentIndex;
            }
            return start;
        };

        // I6-B3 — 덤프 대상을 legacy 스켈레톤이 아니라 Animator로 든다.
        // 이 진단은 게이트 소비자가 0이지만 손으로 도달성을 볼 때 쓰는
        // 살아 있는 표면이라 폐기가 아니라 이관이다(I6-A의 판정과 다르다 —
        // 그쪽은 legacy 왕복 자체가 목적인 진단이었다).
        Animator* dumpedAnimator = nullptr;
        size_t boneObjectCount = 0;
        size_t hitCount = 0;
        size_t emptyTagCount = 0;
        size_t noTransformCount = 0;
        size_t unreachableCount = 0;
        size_t cachedIndexCount = 0;
        int shownNameFail = 0;
        int shownUnreachable = 0;

        for (const auto& obj : scene->m_Entities)
        {
            if (!obj || obj->IsDestroyMark()) continue;
            BoneComponent* bc = obj->GetComponent<BoneComponent>();
            if (!bc) continue;

            ++boneObjectCount;
            if (!obj->HasTransform()) ++noTransformCount;
            if (bc->GetResolvedBoneIndex() >= 0) ++cachedIndexCount;

            const Entity::Index broken = brokenLinkOf(obj->m_index);
            if (Entity::IsValidIndex(broken))
            {
                ++unreachableCount;
                if (shownUnreachable < limit)
                {
                    ++shownUnreachable;
                    const auto& badNode = scene->TryGetEntity(broken);
                    const auto& badParent = badNode ? scene->TryGetEntity(badNode->GetParentIndex()) : nullptr;
                    std::printf("[scene.bonedump] 순회 미도달 — \"%s\"의 조상 idx=%d(\"%s\")가 부모 idx=%d(\"%s\")의 children에 없다\n",
                        obj->GetHashedName().ToString().c_str(), static_cast<int>(broken),
                        badNode ? badNode->GetHashedName().ToString().c_str() : "?",
                        badNode ? static_cast<int>(badNode->GetParentIndex()) : -1,
                        badParent ? badParent->GetHashedName().ToString().c_str() : "?");
                }
            }

            const auto& rootObj = scene->TryGetEntity(obj->GetRootIndex());
            if (!rootObj) continue;
            const auto& animator = rootObj->GetComponent<Animator>();
            if (!animator || 0 == animator->GetSkeletonSerial()) continue;

            if (!dumpedAnimator) dumpedAnimator = animator;

            const std::string& tag = obj->RemoveSuffixNumberTag();
            if (tag.empty()) ++emptyTagCount;
            if (animator->ResolveBoneIndex(tag) >= 0)
            {
                ++hitCount;
            }
            else if (shownNameFail < limit)
            {
                ++shownNameFail;
                std::printf("[scene.bonedump] 이름 불일치 — idx=%d m_name=%s tag=%s(len=%zu) -> FindBone 실패\n",
                    static_cast<int>(obj->m_index),
                    quote(obj->GetHashedName().ToString()).c_str(),
                    quote(tag).c_str(), tag.size());
            }
        }

        std::printf("[scene.bonedump] 뼈 마커 %zu개 · 이름 조회 적중 %zu개 · tag 빈 문자열 %zu개\n",
            boneObjectCount, hitCount, emptyTagCount);
        std::printf("[scene.bonedump] Transform없음 %zu개 · 순회 미도달 %zu개 · 캐시된 인덱스 %zu개 (bonecache=%s)\n",
            noTransformCount, unreachableCount, cachedIndexCount, Scene::IsBoneCacheEnabled() ? "1" : "0");

        if (dumpedAnimator)
        {
            // m_boneMap은 계수에서 뺐다 — FindBone이 정본이고 그 맵은 아무도
            // 읽지 않는 죽은 필드다(실측). 신원 축은 experiment면 generation,
            // 아니면 legacy serial이다(GetSkeletonSerial 주석 참조).
            bool viaExperiment = false;
            const size_t boneCount = dumpedAnimator->GetBoneCount(&viaExperiment);
            std::printf("[scene.bonedump] 스켈레톤 serial=%llu · 뼈 %zu개 (출처=%s)\n",
                static_cast<unsigned long long>(dumpedAnimator->GetSkeletonSerial()),
                boneCount, viaExperiment ? "experiment" : "legacy");

            int printed = 0;
            for (size_t index = 0; index < boneCount; ++index)
            {
                if (printed >= limit) break;
                ++printed;
                const std::string name =
                    dumpedAnimator->GetBoneName(static_cast<int>(index));
                if (name.empty())
                {
                    std::printf("[scene.bonedump]   뼈[%d] = 이름 없음\n", printed - 1);
                    continue;
                }
                std::printf("[scene.bonedump]   뼈[%d] index=%zu name=%s(len=%zu)\n",
                    printed - 1, index, quote(name).c_str(), name.size());
            }
        }
        else
        {
            std::printf("[scene.bonedump] 스켈레톤에 도달한 뼈 오브젝트가 없다 — 관문 앞에서 끊겼다\n");
        }
    }

    // 진단용(트랙 P · P4-a 게이트) — prefab.objectguid.
    //
    // ★ else-if 명령 사슬이 아니라 조기 디스패치로 둔 이유: 그 사슬이 이미
    // MSVC의 블록 중첩 상한에 닿아 있어(C1061, 실측) 한 줄만 더해도 컴파일이
    // 깨진다. scene.dirtytraversal·scene.bonecache 등이 쓰는 것과 같은 관례로
    // 함수로 빼고 Execute 앞머리에서 return한다.
    void HandlePrefabObjectGuid(const std::vector<std::string>& parts)
    {
        // 진단용(트랙 P · P4-a 게이트). prefab.status의 집계 수치(씬 인스턴스/
        // 등록 개수)만으로는 "그 값이 맞는 값인가"를 볼 수 없다 — 중첩 프리팹
        // 스탬핑 버그는 개수를 바꾸지 않고 값만 바꾼다(중첩 루트가 바깥 프리팹의
        // guid로 덮인다. 결함 2). 이 명령은 이름으로 찾은 오브젝트의
        // m_prefabFileGuid를 그대로 문자열로 찍어, 회귀 스크립트가 특정 노드의
        // guid가 "자기 자신의 것"인지 "바깥 것으로 덮였는지"를 값 단위로 대조할
        // 수 있게 한다.
        //
        // Debug->LogWarning은 인메모리·HTML 싱크로만 가고 회귀가 읽는 stdout에는
        // 안 나온다(오늘 실제로 물린 함정) — 그래서 std::printf로 찍는다.
        if (parts.size() < 2)
        {
            std::printf("[CLI] 사용법: prefab.objectguid <오브젝트 이름>\n");
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

        const std::string guidStr = object->m_prefabFileGuid.ToString();
        std::printf("[CLI] [prefab.objectguid] %s guid=%s\n", parts[1].c_str(), guidStr.c_str());
        }

    // S2 측정 게이트(SceneGraphRedesignPlan §4 트랙 S, S2). 폭 10 트리를
    // objectCount개(+루트 1개) 합성 생성해 "전부 정지"·"10%만 매 프레임 이동" 두
    // 시나리오로 AllUpdateWorldMatrix를 frames회씩 재고, 끝나면 만든 오브젝트를
    // 전부 파괴 마크해 씬을 원상 복구한다. scene.dirtytraversal 0/1을 바꿔가며
    // 같은 명령을 두 번 돌리는 것이 A/B 비교 방법이다 — 이 명령 혼자서는
    // "옛 경로 대비 얼마나 빨라졌는지"를 말하지 않는다(미측정으로 보고할 것).
    //
    // ★ 레인 2(트랙 E E7-b 측정 준비) — <오브젝트수> 0은 합성을 건너뛰고 "현재
    // 씬을 그대로" 잰다. Bone 판정(순회의 Scene::UpdateModelRecursive Bone
    // 분기)의 이득은 합성 Empty 트리가 아니라 실제 뼈가 있는 저작 씬에서만
    // 드러나기 때문이다. 시나리오는 "전부 정지" 하나뿐이다(판단) — "10% 이동"은
    // 대상을 골라 매 프레임 실제 Transform 위치를 덮어쓰고 되돌리지 않는데,
    // 합성 오브젝트라면 무해해도 저작 오브젝트에 그대로 쓰면 씬을 오염시킨다.
    void HandleSceneTraversalBench(const std::vector<std::string>& parts)
    {
        if (parts.size() < 3)
        {
            std::printf("[CLI] 사용법: scene.traversalbench <오브젝트수> <프레임수> (0 = 합성 없이 현재 씬)\n");
            return;
        }

        const int objectCount = (std::max)(0, std::atoi(parts[1].c_str()));
        const int frames = (std::max)(1, std::atoi(parts[2].c_str()));

        Scene* scene = SceneManagers->GetActiveScene();
        if (nullptr == scene) { std::printf("[CLI] 활성 씬 없음\n"); return; }

        constexpr int kBenchWidth = 10;

        // 0개 모드는 아무것도 만들지 않으므로 둘 다 빈 채로 남는다 — 정리 단계와
        // "10% 이동" 시나리오가 아래에서 이 빈 상태를 보고 스스로 건너뛴다.
        std::vector<GameObjectIndex> created;
        std::vector<GameObjectIndex> movers;

        if (0 == objectCount)
        {
			// 만들지 않고 이미 있는 것을 센다 — 무엇을 쟀는지 모르면 수치가
			// 의미가 없다. E7-c 이후 뼈 정체성은 BoneComponent 하나가 정본이다.
            size_t liveObjectCount = 0;
            size_t markedCount = 0;
            for (const auto& obj : scene->m_Entities)
            {
                if (!obj || obj->IsDestroyMark()) continue;
                ++liveObjectCount;
                if (obj->GetComponent<BoneComponent>()) ++markedCount;
            }

            if (0 == liveObjectCount)
            {
                // 0개를 재고 "빠르다"고 보고하는 사고를 막는다 — scene.proxybench
                // (HandleSceneProxyBench)와 같은 하한 가드 관례.
                std::printf("[CLI] scene.traversalbench: 활성 씬에 오브젝트가 0개다 — 잴 것이 없다\n");
                return;
            }

            char header[192]{};
            std::snprintf(header, sizeof(header),
				"[scene.traversalbench] 현재 씬 그대로 · 오브젝트 %zu개(뼈 마커 %zu개) · dirtytraversal=%s · bonecache=%s",
				liveObjectCount, markedCount, Scene::IsDirtyTraversalEnabled() ? "1" : "0",
                Scene::IsBoneCacheEnabled() ? "1" : "0");
            std::printf("%s\n", header);
            Debug->LogWarning(header);
        }
        else
        {
            auto benchRoot = scene->CreateEntity("__TraversalBenchRoot");
            if (!benchRoot)
            {
                std::printf("[CLI] scene.traversalbench: 루트 생성 실패\n");
                return;
            }

            // 폭 kBenchWidth로 BFS 합성 — 깊이는 objectCount에 따라 자연히 정해진다.
            created.push_back(benchRoot->m_index);

            std::vector<GameObjectIndex> currentLevel{ benchRoot->m_index };
            int madeCount = 0;
            while (madeCount < objectCount && !currentLevel.empty())
            {
                std::vector<GameObjectIndex> nextLevel;
                for (GameObjectIndex parentIdx : currentLevel)
                {
                    for (int c = 0; c < kBenchWidth && madeCount < objectCount; ++c)
                    {
                        auto child = scene->CreateEntity(
                            "__bench_" + std::to_string(madeCount), GameObjectType::Empty, parentIdx);
                        if (!child) continue;
                        created.push_back(child->m_index);
                        nextLevel.push_back(child->m_index);
                        ++madeCount;
                    }
                    if (madeCount >= objectCount) break;
                }
                currentLevel = std::move(nextLevel);
            }

            // 10%만 매 프레임 이동시킬 대상 — 루트(created[0])를 뺀 생성분에서 10개마다 하나.
            for (size_t i = 1; i < created.size(); i += 10)
            {
                movers.push_back(created[i]);
            }

            char header[192]{};
            std::snprintf(header, sizeof(header),
                "[scene.traversalbench] 오브젝트 %d개(+루트 1) · dirtytraversal=%s",
                madeCount, Scene::IsDirtyTraversalEnabled() ? "1" : "0");
            std::printf("%s\n", header);
            Debug->LogWarning(header);
        }

        using PerfClock = std::chrono::steady_clock;
        const auto runScenario = [&](const char* label, bool moveEach)
        {
            // 첫 패스는 새 슬롯이 전부 dirty=1이라 워밍업으로 버린다 — "이미
            // 정착된 상태에서 정지/이동"을 재는 것이 목적이지, 스폰 직후 첫
            // 패스의 전수 재계산 비용이 아니다.
            scene->AllUpdateWorldMatrix();

            std::vector<double> samplesUs;
            samplesUs.reserve(static_cast<size_t>(frames));
            for (int f = 0; f < frames; ++f)
            {
                if (moveEach)
                {
                    for (size_t i = 0; i < movers.size(); ++i)
                    {
                        if (Entity* mover = scene->GetEntityRaw(movers[i]))
                        {
                            const float x = static_cast<float>((f + static_cast<int>(i)) % 100) * 0.01f;
                            mover->Transform_().SetPosition(math::vector3{ x, 0.f, 0.f });
                        }
                    }
                }

                const auto t0 = PerfClock::now();
                scene->AllUpdateWorldMatrix();
                const auto t1 = PerfClock::now();
                samplesUs.push_back(std::chrono::duration<double, std::micro>(t1 - t0).count());
            }

            double sum = 0.0;
            double minUs = samplesUs.front();
            double maxUs = samplesUs.front();
            for (double v : samplesUs)
            {
                sum += v;
                minUs = (std::min)(minUs, v);
                maxUs = (std::max)(maxUs, v);
            }
            const double avg = sum / static_cast<double>(samplesUs.size());

            char line[256]{};
            std::snprintf(line, sizeof(line),
                "[scene.traversalbench] %s — 평균 %.2fus 최소 %.2fus 최대 %.2fus (프레임 %d)",
                label, avg, minUs, maxUs, frames);
            std::printf("%s\n", line);
            Debug->LogWarning(line);
        };

        // 0개 모드(현재 씬)는 "전부 정지" 하나만 — 헤더 위에서 이미 적었다.
        // 합성 모드는 기존 그대로 둘 다 잰다.
        runScenario(0 == objectCount ? "전부 정지(현재 씬)" : "전부 정지", false);
        if (0 != objectCount)
        {
            runScenario("10% 매프레임 이동", true);
        }
        else
        {
            // ★ 잰 것이 뼈 경로를 실제로 태웠는지 사후 확인한다.
            //
            // 마커가 붙어 있어도 Scene::UpdateModelRecursive의 뼈 분기는 루트의
            // Animator·Skeleton이 준비되고 켜져 있을 때만 FindBone까지 간다 —
            // 아니면 그 앞에서 return한다. 그 경우 scene.bonecache를 켜든 끄든
            // 같은 수치가 나오고, 그것을 "캐시가 효과 없다"로 오독하게 된다.
            // 해석된 뼈 수(m_boneIndex >= 0)를 함께 찍어 그 오독을 막는다.
            // "해석 0개"는 두 가지 서로 다른 사실을 같은 모습으로 보여 준다:
            // ① 분기 앞의 관문(루트·Animator·Skeleton·활성)에서 되돌아왔다,
            // ② 분기는 탔지만 FindBone이 못 찾았다(스켈레톤의 m_bones가 비었거나
            //    이름이 안 맞는다). 둘은 원인도 조치도 다르므로 관문을 하나씩
            //    되짚어 어디서 끊겼는지 그대로 찍는다 — 숫자 하나만 보고
            //    "캐시가 효과 없다"고 결론 내리는 것을 막는다.
            size_t resolvedCount = 0;
            size_t noRootCount = 0;
            size_t noAnimatorCount = 0;
            size_t noSkeletonCount = 0;
            size_t reachedCount = 0;
            size_t skeletonBoneMax = 0;
            for (const auto& obj : scene->m_Entities)
            {
                if (!obj || obj->IsDestroyMark()) continue;
                BoneComponent* bc = obj->GetComponent<BoneComponent>();
                if (!bc) continue;

                const auto& rootObj = scene->TryGetEntity(obj->GetRootIndex());
                if (!rootObj) { ++noRootCount; continue; }
                const auto& animator = rootObj->GetComponent<Animator>();
                if (!animator || !animator->IsEnabled()) { ++noAnimatorCount; continue; }
                // I6-B3 — 관문과 본 계수가 창구를 탄다. 벤치가 legacy 객체
                // 존재를 관문으로 쓰면 그 객체를 은퇴시킬 수 없다.
                if (0 == animator->GetSkeletonSerial()) { ++noSkeletonCount; continue; }

                ++reachedCount;
                skeletonBoneMax = (std::max)(skeletonBoneMax, animator->GetBoneCount());
                if (bc->GetResolvedBoneIndex() >= 0) ++resolvedCount;
            }
            // 512 — 한글이 UTF-8에서 글자당 3바이트라 이 문장은 값이 다 차면 300바이트를
            // 넘긴다. snprintf는 넘치면 조용히 잘라내므로, 진단이 스스로 잘린 채 나가는
            // 것을 막으려고 여유를 둔다.
            char diag[512]{};
            std::snprintf(diag, sizeof(diag),
                // ★ "도달"이 아니라 "관문 통과"라고 쓴다 — 이 값은 순회가 그 노드에
                // 닿았는지가 아니라, 닿았다면 통과했을 조건(루트·Animator·Skeleton·
                // 활성)을 만족하는지만 센다. 아래 주석의 사건이 정확히 이 두 낱말의
                // 차이에서 났다: "도달 61개"를 읽고 순회는 당연히 닿은 줄 알았는데
                // 실은 서브트리가 통째로 순회 밖이었다. 라벨이 스스로를 설명하면
                // 주석을 안 읽어도 오독하지 않는다.
                "[scene.traversalbench] 뼈 경로 — 마커 보유 중 관문 통과 %zu개(루트없음 %zu · 애니메이터없음/꺼짐 %zu · 스켈레톤없음 %zu)"
                " · 인덱스 해석 %zu개 · 스켈레톤 뼈 수 최대 %zu"
                " ※ 관문 통과 ≠ 순회 도달(도달성은 scene.bonedump)",
                reachedCount, noRootCount, noAnimatorCount, noSkeletonCount, resolvedCount, skeletonBoneMax);
            std::printf("%s\n", diag);
            Debug->LogWarning(diag);

            // ★ 이 관문 계수는 **순회와 무관한 코드**가 자기 힘으로 조상을 타고
            // 올라가 센 값이다 — 순회가 실제로 이 노드에 닿았는지는 재지 않는다.
            // 실제로 한 번 이 차이에 걸렸다: 관문 61개 전부 통과 · 해석 0개가
            // 나왔는데, 진짜 원인은 모델 루트가 씬 루트의 m_childrenIndices에서
            // 빠져 순회가 서브트리를 통째로 건너뛴 것이었다(FindBone은 멀쩡했다).
            // 그래서 해석 0개면 조회가 아니라 도달성부터 의심하도록 안내한다.
            if (0 == resolvedCount && reachedCount > 0)
            {
                std::printf("[scene.traversalbench] ↑ 해석 0개 — 이 관문 계수는 순회 도달성을 재지 않는다."
                    " scene.bonedump으로 이름 조회와 순회 도달성을 갈라 볼 것\n");
            }
        }

        // 정리 — 만든 오브젝트가 있을 때만 파괴 마크해 씬을 원상 복구한다(0개
        // 모드는 애초에 아무것도 만들지 않았으므로 created가 비어 있어 아래
        // 루프가 그냥 아무 일도 안 한다 — 그래도 완료 로그는 만든 게 있을 때만
        // 찍는다, 안 그러면 "0개 파괴 마크 완료"가 매번 찍혀 헷갈린다).
        // 실제 슬롯 회수는 엔진의 정상 프레임 종료 파괴 지점(FlushPendingDestroy →
        // DestroyEntities)이 다음 틱에 한다 — 다른 destroy 계열 콘솔 명령과
        // 같은 규약이다(이 명령은 여기서 프레임을 진행시키지 않는다).
        if (!created.empty())
        {
            for (GameObjectIndex idx : created)
            {
                scene->DestroyEntity(idx);
            }
            std::printf("[CLI] scene.traversalbench: 오브젝트 %zu개 파괴 마크 완료\n", created.size());
        }
    }
}

// ── 콘솔 명령 디스패치 표 ────────────────────────────────────────────────
//
// 예전에는 Execute 안의 else-if 사슬 하나가 명령 117개를 받았다. 사슬은 단마다
// 블록을 한 겹씩 더 쌓아서 MSVC 중첩 한계(C1061)에 닿았고, 명령을 더 붙일 수가
// 없었다. 이름→핸들러 표로 바꾸면 명령이 몇 개든 중첩 깊이는 1이라 그 한계가
// 구조적으로 사라진다.
//
// 핸들러는 전부 내부 링크(static)다 - 이 파일은 유니티 빌드에 들어가므로
// 외부 링크 심볼을 늘리면 다른 TU와 부딪힐 수 있다.
namespace ConsoleCmd
{
    static std::string ResolveTestArtifactPath(
        std::string_view category, std::string_view requestedPath)
    {
        file::path output(requestedPath);
        if (output.is_relative())
        {
            output = PathFinder::TestArtifactPath(category) / output;
        }
        output = output.lexically_normal();
        std::error_code error{};
        file::create_directories(output.parent_path(), error);
        return output.string();
    }

    static void Cmd_help(const ConsoleCommandContext& ctx)
    {
        ctx.system.PrintHelp();
    }

    static void Cmd_quit(const ConsoleCommandContext& ctx)
    {
        std::printf("[CLI] 종료 요청\n");
        ctx.system.RequestQuit();
    }

    static void Cmd_game_pak(const ConsoleCommandContext& ctx)
    {
        const bool buildOk = GameBuilderSystem::GetInstance()->BuildGame();
        std::printf("[CLI] game.pak Release Player 패키지 %s\n",
            buildOk ? "빌드·검증·게시 완료" : "실패");
        if (!buildOk)
        {
            // 무인 --exec/--script 호출자가 printf를 파싱하지 않아도 실패를 안다.
            // 뒤의 quit/진단 명령은 계속 실행하되 최종 프로세스 결과는 비-0으로 남긴다.
            EngineBootstrap::SetExitCode(5);
        }
    }

    static void Cmd_wait(const ConsoleCommandContext& ctx)
    {
        const std::vector<std::string>& parts = ctx.parts;

        const int frames = (parts.size() > 1) ? std::max(0, std::atoi(parts[1].c_str())) : 1;
        ctx.system.SetWaitFrames(frames);
        std::printf("[CLI] %d 프레임 대기\n", frames);
    }

    static void Cmd_scene_load(const ConsoleCommandContext& ctx)
    {
        const std::vector<std::string>& parts = ctx.parts;
        const std::string& cmd = ctx.cmd;

        if (parts.size() < 2)
        {
            std::printf("[CLI] 사용법: %s <씬 경로>\n", cmd.c_str());
            return;
        }

        // scene.load  : 씬을 열기만 한다(기존 씬 유지)
        // scene.switch: 씬을 열고 활성 씬으로 교체한다(기존 씬 파괴 → 언로드 유발)
        //
        // ★ 단계마다 즉시 찍는다.
        //
        //   씬 교체가 멈추는 것을 쫓다가 출력이 0바이트인 실행을 만났다.
        //   프로세스를 죽여도 아무것도 안 남아 어디까지 갔는지조차 알 수
        //   없었다. 함수가 끝나야 찍히는 로그는 멈춘 자리를 못 알려 준다 —
        //   dx12.compare 크래시 때와 같은 자리다(그때도 outLog가 함수 끝에
        //   가서야 쓰여서 세 번을 헛짚었다).
        std::printf("[CLI] %s 시작: %s\n", cmd.c_str(), parts[1].c_str());

        Scene* scene = SceneManagers->LoadScene(parts[1]);
        std::printf("[CLI] LoadScene 반환: %s\n",
            (nullptr != scene) ? "성공" : "널");

        if (!scene)
        {
            Debug->LogError("[CLI] 씬 로드 실패: " + parts[1]);
            std::printf("[CLI] 씬 로드 실패: %s\n", parts[1].c_str());
            return;
        }

        if (cmd == "scene.switch")
        {
            std::printf("[CLI] ActivateScene 진입\n");
            SceneManagers->ActivateScene(scene, true);
            std::printf("[CLI] ActivateScene 반환\n");
        }
        std::printf("[CLI] %s 완료: %s\n", cmd.c_str(), parts[1].c_str());
    }

    // DontDestroyOnLoad 지정 — 씬 이송 경로를 시나리오에서 태우기 위한 진단 명령.
    //
    // 이 경로(Scene::DetachEntityHierarchy / AttachExistingEntity*)는
    // SceneManager의 씬 로드 안에서만 불려, 지금까지 회귀 세트가 **단 한 번도
    // 태운 적이 없다.** 그래서 E5-R2(캔버스 캐시 핸들화)는 델타를 잴 자를 못
    // 만들었고, L3의 잔여(이송 신호를 C#까지 전달)도 검증 수단이 없었다.
    // 이 명령이 그 둘의 공통 선행이다.
    static void Cmd_scene_ddol(const ConsoleCommandContext& ctx)
    {
        Scene* scene = SceneManagers->GetActiveScene();
        if (!scene) { std::printf("[CLI] 활성 씬 없음\n"); return; }

        if (ctx.parts.size() < 2)
        {
            std::printf("[CLI] 사용법: scene.ddol <오브젝트이름>\n");
            return;
        }

        const std::string name = TrimLine(ctx.line.substr(ctx.cmd.size()));
        auto obj = scene->GetEntity(name);
        if (!obj)
        {
            std::printf("[CLI] scene.ddol 대상 없음: %s\n", name.c_str());
            return;
        }

		Object::SetDontDestroyOnLoad(obj);
        std::printf("[CLI] scene.ddol 지정: %s (DDOL=%d)\n",
            name.c_str(), obj->IsDontDestroyOnLoad() ? 1 : 0);
    }

	static void Cmd_ai_status(const ConsoleCommandContext& ctx)
	{
		const size_t total = AIManagers->GetRegisteredAIComponentCount();
		if (ctx.parts.size() < 2)
		{
			std::printf("[AI 레지스트리] total=%zu\n", total);
			return;
		}

		Scene* scene = SceneManagers->GetActiveScene();
		Entity* object = scene ? scene->GetEntity(ctx.parts[1]) : nullptr;
		StateMachineComponent* component = object
			? object->GetComponent<StateMachineComponent>() : nullptr;
		const bool registered = AIManagers->IsAIComponentRegistered(component);
		std::printf("[AI 레지스트리] object=%s registered=%d total=%zu scene=%u\n",
			ctx.parts[1].c_str(), registered ? 1 : 0, total,
			scene ? scene->GetSceneId() : 0u);
	}

    static void Cmd_scene_new(const ConsoleCommandContext& ctx)
    {
        const std::vector<std::string>& parts = ctx.parts;
        const std::string& line = ctx.line;
        const std::string& cmd = ctx.cmd;

        // 빈 씬에서 시작한다. 기능별 테스트 씬은 '무엇이 들어 있는지'를 전부
        // 알아야 결과를 판정할 수 있는데, 열려 있던 씬 위에 쌓으면 그게 깨진다.
        const std::string name = (parts.size() > 1)
            ? TrimLine(line.substr(cmd.size())) : std::string("FeatureTest");

        // SceneManager::CreateScene을 쓰지 않는다. 그쪽은 옛 씬을 그 자리에서
        // 해체하고 새 씬을 활성으로 바꾸는데, 그 '그 자리'가 프레임 중간이라
        // 커맨드를 만들고 있던 렌더 워커의 발밑에서 자료구조가 사라진다.
        // 실측으로 ShadowMapPass::CreateCommandListCascadeShadow →
        // DX11CommandContext::UpdateBuffer에서 죽었다.
        //
        // scene.switch가 쓰는 경로를 그대로 쓴다: 씬만 만들어 두고 교체는
        // ActivateScene에 맡긴다 — 그쪽은 BeforeAwakeSceneLoad(프레임의 안전
        // 지점)까지 미룬다.
        Scene* scene = Scene::CreateNewScene(name);
        if (!scene)
        {
            Debug->LogError("[CLI] 씬 생성 실패: " + name);
            std::printf("[CLI] 씬 생성 실패: %s\n", name.c_str());
            return;
        }

        SceneManagers->ActivateScene(scene, true);

        Debug->LogWarning("[CLI] 새 씬: " + name);
        std::printf("[CLI] 새 씬: %s\n", name.c_str());
    }

    static void Cmd_scene_save(const ConsoleCommandContext& ctx)
    {
        const std::vector<std::string>& parts = ctx.parts;
        const std::string& line = ctx.line;
        const std::string& cmd = ctx.cmd;

        if (parts.size() < 2)
        {
            std::printf("[CLI] 사용법: scene.save <저장 경로>\n");
            return;
        }

        // 경로에 공백이 들어갈 수 있으므로 명령어 뒤 전체를 경로로 본다.
        const std::string path = TrimLine(line.substr(cmd.size()));

        if (!SceneManagers->GetActiveScene())
        {
            std::printf("[CLI] 활성 씬 없음\n");
            return;
        }

        // 상위 디렉터리를 만들어 준다. 없으면 ofstream이 조용히 실패하고
        // '저장했다'는 메시지만 남는다.
        std::error_code directoryError{};
        file::create_directories(file::path(path).parent_path(), directoryError);

        SceneManagers->SaveScene(path);

        // 실제로 파일이 생겼는지 확인한다 — SaveScene은 실패를 돌려주지 않는다.
        if (!file::exists(path))
        {
            Debug->LogError("[CLI] 씬 저장 실패: " + path);
            std::printf("[CLI] 씬 저장 실패: %s\n", path.c_str());
            return;
        }

        const auto bytes = file::file_size(path);
        Debug->LogWarning("[CLI] 씬 저장: " + path);
        std::printf("[CLI] 씬 저장: %s (%llu 바이트)\n", path.c_str(),
            static_cast<unsigned long long>(bytes));
    }

    static void Cmd_object_create(const ConsoleCommandContext& ctx)
    {
        const std::vector<std::string>& parts = ctx.parts;

        if (parts.size() < 2)
        {
            std::printf("[CLI] 사용법: object.create <이름> [Empty|Light|Camera|Mesh|UI|Canvas]\n");
            return;
        }

        Scene* scene = SceneManagers->GetActiveScene();
        if (!scene) { std::printf("[CLI] 활성 씬 없음\n"); return; }

        GameObjectType type = GameObjectType::Empty;
        std::string name = parts[1];
        if (parts.size() > 2)
        {
            const std::string& typeName = parts[2];
            if (typeName == "Light")       type = GameObjectType::Light;
            else if (typeName == "Camera") type = GameObjectType::Camera;
            else if (typeName == "Mesh")   type = GameObjectType::Mesh;
            // ★ UI·Canvas 추가 (2026-08-20, 자산·게이트 CLI 이전).
            //
            // 이 둘이 없어 **CLI로는 UI 오브젝트를 아예 저작할 수 없었다.**
            // RectTransformComponent는 손으로 붙일 수 없고(ComponentFactory가
            // 의도적으로 목록에서 뺀다 — 3D 오브젝트에 붙으면 UI 레이아웃 순회에
            // 끼어들어 자식에게 스크린 좌표계를 조용히 전파한다), 그 부착은
            // GameObject::AttachSpatialComponent가 **오브젝트 타입으로** 정한다:
            // UI는 rect만, Canvas는 rect와 Transform 둘 다, 나머지는 Transform만.
            //
            // 즉 타입을 못 주면 rect가 없는 오브젝트만 만들 수 있고, ui.rect·
            // ui.hitbox가 전부 "RectTransform 없음"으로 떨어진다(실측). ui.* 명령은
            // 관측·설정이지 **생성이 아니다** — §0.05의 "CLI 저작 표면은 이미 서
            // 있다"가 UI에 대해서는 성립하지 않았다.
            //
            // 해상도 스윕 게이트 이전과 verify-authored-rects의 후계가 **둘 다**
            // 이 능력을 선행으로 요구한다.
            else if (typeName == "UI")     type = GameObjectType::UI;
            else if (typeName == "Canvas") type = GameObjectType::Canvas;
            else if (typeName != "Empty")
            {
                std::printf("[CLI] 알 수 없는 오브젝트 타입: %s\n", typeName.c_str());
                return;
            }
        }

        auto object = scene->CreateEntity(name, type);
        if (!object)
        {
            std::printf("[CLI] 오브젝트 생성 실패: %s\n", name.c_str());
            return;
        }

        Debug->LogWarning("[CLI] 오브젝트 생성: " + name);
        std::printf("[CLI] 오브젝트 생성: %s\n", name.c_str());
    }

    static void Cmd_object_rename(const ConsoleCommandContext& ctx)
    {
        const std::vector<std::string>& parts = ctx.parts;
        const std::string& line = ctx.line;
        const std::string& cmd = ctx.cmd;

        // 같은 모델을 여러 번 배치하면 이름이 겹쳐 이후 명령이 첫 번째만 잡는다.
        // 하나 놓고 바로 이름을 바꾸면 그 문제가 없다.
        if (parts.size() < 3)
        {
            std::printf("[CLI] 사용법: object.rename <이전 이름> <새 이름>\n");
            return;
        }

        Scene* scene = SceneManagers->GetActiveScene();
        if (!scene) { std::printf("[CLI] 활성 씬 없음\n"); return; }

        // 이전 이름에 공백이 흔하다. 같은 모델을 두 번 놓으면 엔진이
        // "Prim_Cube (1)"처럼 번호를 붙이기 때문이다. 그래서 새 이름을 마지막
        // 토큰으로 보고 그 앞 전체를 이전 이름으로 본다(prefab.create와 같은 규칙).
        const std::string newName = parts.back();
        std::string rest = TrimLine(line.substr(cmd.size()));
        const std::string oldName = TrimLine(rest.substr(0, rest.rfind(newName)));

        auto object = scene->GetEntity(oldName);
        if (!object)
        {
            Debug->LogError("[CLI] 오브젝트를 찾을 수 없음: " + oldName);
            std::printf("[CLI] 오브젝트를 찾을 수 없음: %s\n", oldName.c_str());
            return;
        }

        object->m_name = newName;
        Debug->LogWarning("[CLI] 이름 변경: " + oldName + " -> " + newName);
        std::printf("[CLI] 이름 변경: %s -> %s\n", oldName.c_str(), newName.c_str());
    }

    static void Cmd_scene_hierarchycheck(const ConsoleCommandContext& ctx)
    {
        (void)ctx;

        // scene.hierarchycheck — 계층 표기의 불변식을 잰다.
        //
        // 재는 불변식은 하나다:
        //
        //     자식이 부모의 m_childrenIndices에 실려 있다  <=>  자식의 m_parentIndex가 그 부모다
        //
        // 이 쌍이 깨지면 순회(m_Entities[0]->m_childrenIndices에서만 내려간다)가
        // 서브트리를 통째로 빠뜨리는데 에러도 로그도 없다 — 뼈 61개가 그렇게
        // 순회 밖에 있었다.
        //
        // 최상위 오브젝트의 표기가 갈려 있는 것이 그 뿌리다(SceneGraphRedesignPlan
        // 트랙 E). 같은 뜻인데 두 값이 쓰인다:
        //   · Entity::AddChild            -> m_parentIndex = 부모 인덱스(루트면 0)
        //   · Scene::AttachExistingEntity / DDOL 이탈 -> INVALID_INDEX(-1)
        // 둘 다 씬 루트의 children에는 들어가므로, "-1인데 루트 children에 있음"이
        // 정상처럼 보인다. 그 상태를 세는 것이 topLevelInvalid다.
        Scene* scene = SceneManagers->GetActiveScene();
        if (!scene) { std::printf("[CLI] 활성 씬 없음\n"); return; }

        const auto& objects = scene->m_Entities;

        size_t total = 0;
        size_t topLevelRoot = 0;      // 최상위인데 m_parentIndex == 0 (쌍이 맞는 표기)
        size_t topLevelInvalid = 0;   // 최상위인데 m_parentIndex == INVALID (쌍이 어긋난 표기)
        size_t pairMismatch = 0;      // 부모의 children에 있는데 m_parentIndex가 그 부모가 아님
        size_t orphan = 0;            // 아무의 children에도 없음(씬 루트 제외)
        size_t unreachable = 0;       // 씬 루트에서 children만 따라 내려가 닿지 못함

        // 어느 부모의 children에 실려 있는지 역인덱스를 만든다.
        std::unordered_map<Entity::Index, Entity::Index> listedUnder;
        for (const auto& obj : objects)
        {
            if (!obj) continue;
            for (Entity::Index childIdx : obj->GetChildrenIndices())
            {
                listedUnder[childIdx] = obj->m_index;
            }
        }

        // 씬 루트에서 children만 따라 내려가 닿는 집합.
        std::unordered_set<Entity::Index> reached;
        if (!objects.empty() && objects[0])
        {
            std::vector<Entity::Index> stack{ objects[0]->m_index };
            reached.insert(objects[0]->m_index);
            while (!stack.empty())
            {
                const Entity::Index cur = stack.back();
                stack.pop_back();
                const auto& node = scene->TryGetEntity(cur);
                if (!node) continue;
                for (Entity::Index childIdx : node->GetChildrenIndices())
                {
                    if (reached.insert(childIdx).second) stack.push_back(childIdx);
                }
            }
        }

        for (const auto& obj : objects)
        {
            if (!obj) continue;
            ++total;
            if (Entity::kSceneRootIndex == obj->m_index) continue;   // 씬 루트 자신은 제외

            auto it = listedUnder.find(obj->m_index);
            if (it == listedUnder.end())
            {
                ++orphan;
            }
            else if (it->second == Entity::kSceneRootIndex)
            {
                if (Entity::IsInvalidIndex(obj->GetParentIndex())) ++topLevelInvalid;
                else if (Entity::kSceneRootIndex == obj->GetParentIndex()) ++topLevelRoot;
                else ++pairMismatch;
            }
            else if (it->second != obj->GetParentIndex())
            {
                ++pairMismatch;
            }

            if (reached.find(obj->m_index) == reached.end()) ++unreachable;
        }

        const size_t storeMismatch = scene->CountHierarchyStoreMismatches();
        std::printf("[scene.hierarchycheck] 오브젝트 %zu · 최상위(0표기) %zu · 최상위(-1표기) %zu"
            " · 쌍불일치 %zu · 고아 %zu · 순회미도달 %zu · Store불일치 %zu\n",
            total, topLevelRoot, topLevelInvalid, pairMismatch, orphan, unreachable, storeMismatch);
    }

    static void Cmd_object_duplicate(const ConsoleCommandContext& ctx)
    {
        const std::vector<std::string>& parts = ctx.parts;
        const std::string& line = ctx.line;
        const std::string& cmd = ctx.cmd;

        // object.duplicate <오브젝트> [새 이름]
        //
        // 에디터의 Ctrl+D(DuplicateGameObjectCommand::Redo)와 **같은 원시 함수**를
        // 부른다 — Object::Instantiate. 에디터 전용 경로를 CLI에서도 태울 수 있어야
        // 회귀가 그 경로를 잴 수 있다. 지금 이 경로는 게이트가 하나도 없다.
        if (parts.size() < 2)
        {
            std::printf("[CLI] 사용법: object.duplicate <오브젝트> [새 이름]\n");
            return;
        }

        Scene* scene = SceneManagers->GetActiveScene();
        if (!scene) { std::printf("[CLI] 활성 씬 없음\n"); return; }

        // 이름 규칙은 object.rename·prefab.create와 같다 — 인자가 둘이면 마지막
        // 토큰이 새 이름이고 그 앞 전체가 원본 이름이다(공백 있는 이름 때문).
        std::string sourceName;
        std::string newName;
        if (parts.size() >= 3)
        {
            newName = parts.back();
            std::string rest = TrimLine(line.substr(cmd.size()));
            sourceName = TrimLine(rest.substr(0, rest.rfind(newName)));
        }
        else
        {
            sourceName = TrimLine(line.substr(cmd.size()));
        }

        auto source = scene->GetEntity(sourceName);
        if (!source)
        {
            Debug->LogError("[CLI] 오브젝트를 찾을 수 없음: " + sourceName);
            std::printf("[CLI] 오브젝트를 찾을 수 없음: %s\n", sourceName.c_str());
            return;
        }

        const std::string finalName = newName.empty() ? source->m_name.ToString() : newName;
		auto* cloned = dynamic_cast<Entity*>(Object::Instantiate(source, finalName));
        if (!cloned)
        {
            std::printf("[CLI] 복제 실패: %s\n", sourceName.c_str());
            return;
        }

        // Object::Instantiate가 newName을 반영하지만(Object.cpp:113) 씬 편입이
        // 고유 이름 생성으로 그것을 덮는다("Orig" -> "Orig (1)"). 회귀가 이름으로
        // 대상을 집으므로 반환 뒤에 한 번 더 지정한다.
        if (!newName.empty())
        {
            cloned->m_name = newName;
        }

        Debug->LogWarning("[CLI] 복제: " + sourceName + " -> " + cloned->m_name.ToString());
        std::printf("[CLI] 복제: %s -> %s (index=%d)\n",
            sourceName.c_str(), cloned->m_name.ToString().c_str(),
            static_cast<int>(cloned->m_index));
    }

    static void Cmd_object_parent(const ConsoleCommandContext& ctx)
    {
        const std::vector<std::string>& parts = ctx.parts;
        const std::string& line = ctx.line;
        const std::string& cmd = ctx.cmd;

        // object.parent <자식> <부모>
        //
        // 계층을 가진 자산을 CLI로 저작하기 위한 명령이다. 이것이 없으면
        // object.create가 만드는 것은 전부 루트라, 회귀 게이트가 쓰는 픽스처를
        // 저작 자산 없이 만들 수 없었다(SceneGraphRedesignPlan §0.05).
        //
        // 부모를 "-"로 주면 씬 루트로 되돌린다.
        if (parts.size() < 3)
        {
            std::printf("[CLI] 사용법: object.parent <자식> <부모 | ->\n"
                "       부모에 -를 주면 씬 루트로 올린다\n");
            return;
        }

        Scene* scene = SceneManagers->GetActiveScene();
        if (!scene) { std::printf("[CLI] 활성 씬 없음\n"); return; }

        // 이름에 공백이 흔하므로(엔진이 "Prim_Cube (1)"처럼 번호를 붙인다)
        // 부모를 마지막 토큰으로 보고 그 앞 전체를 자식으로 본다
        // — object.rename·prefab.create와 같은 규칙이다.
        const std::string parentName = parts.back();
        std::string rest = TrimLine(line.substr(cmd.size()));
        const std::string childName = TrimLine(rest.substr(0, rest.rfind(parentName)));

        auto child = scene->GetEntity(childName);
        if (!child)
        {
            Debug->LogError("[CLI] 자식 오브젝트를 찾을 수 없음: " + childName);
            std::printf("[CLI] 자식 오브젝트를 찾을 수 없음: %s\n", childName.c_str());
            return;
        }

		Entity* parent =
            ("-" == parentName) ? scene->GetRootEntity() : scene->GetEntity(parentName);
        if (!parent)
        {
            Debug->LogError("[CLI] 부모 오브젝트를 찾을 수 없음: " + parentName);
            std::printf("[CLI] 부모 오브젝트를 찾을 수 없음: %s\n", parentName.c_str());
            return;
        }

        if (parent->m_index == child->m_index)
        {
            std::printf("[CLI] 자기 자신을 부모로 삼을 수 없음: %s\n", childName.c_str());
            return;
        }

        // 순환을 막는다 — 자기 자손을 부모로 삼으면 순회가 무한이 된다.
        for (auto ancestor = parent; ancestor; )
        {
            if (ancestor->m_index == child->m_index)
            {
                Debug->LogError("[CLI] 순환 계층 거부: " + childName + " <- " + parentName);
                std::printf("[CLI] 순환이 된다(부모가 자식의 자손): %s\n", parentName.c_str());
                return;
            }
            const Entity::Index ancestorParent = ancestor->GetParentIndex();
            if (Entity::INVALID_INDEX == ancestorParent) break;
            auto next = scene->GetEntity(ancestorParent);
            if (next == ancestor) break;
            ancestor = next;
        }

        // 부모 인덱스·Transform 부모 ID·자식 목록을 한 점에서 함께 옮긴다.
		parent->AddChild(child);

        Debug->LogWarning("[CLI] 부모 지정: " + childName + " -> " + parentName);
        std::printf("[CLI] 부모 지정: %s -> %s\n", childName.c_str(), parentName.c_str());
    }

    // H2 root-reference 회귀용. m_rootIndex는 일반 parent와 별개의 same-scene
    // 참조라서 DDOL/Prefab 슬롯 재배정 때 따로 remap해야 한다. 모델 자산 없이
    // 그 경계를 태울 수 있도록 설정과 조회를 한 명령에 둔다.
    static void Cmd_object_rootref(const ConsoleCommandContext& ctx)
    {
        if (ctx.parts.size() < 2)
        {
            std::printf("[CLI] 사용법: object.rootref <오브젝트> [루트오브젝트 | -]\n");
            return;
        }

        Scene* scene = SceneManagers->GetActiveScene();
        if (!scene) { std::printf("[CLI] 활성 씬 없음\n"); return; }

        Entity* object = scene->GetEntity(ctx.parts[1]);
        if (!object)
        {
            std::printf("[CLI] rootref 대상 없음: %s\n", ctx.parts[1].c_str());
            return;
        }

        if (ctx.parts.size() >= 3)
        {
            if (ctx.parts[2] == "-")
            {
                object->SetRootIndex(Entity::INVALID_INDEX);
            }
            else
            {
                Entity* root = scene->GetEntity(ctx.parts[2]);
                if (!root)
                {
                    std::printf("[CLI] rootref 루트 없음: %s\n", ctx.parts[2].c_str());
                    return;
                }
                object->SetRootIndex(root->m_index);
            }
        }

        const Entity::Index rootIndex = object->GetRootIndex();
        Entity* root = scene->TryGetEntity(rootIndex);
        std::printf("[object.rootref] %s root=%d name=%s\n",
            object->GetHashedName().ToString().c_str(), static_cast<int>(rootIndex),
            root ? root->GetHashedName().ToString().c_str() : "<invalid>");
    }

    static void Cmd_object_transform(const ConsoleCommandContext& ctx)
    {
        const std::vector<std::string>& parts = ctx.parts;

        // object.transform <이름> <px> <py> <pz> [rx ry rz] [sx sy sz]
        // 회전은 오일러 각(도)이다. 라디안을 쓰면 스크립트를 읽을 때 값이
        // 무슨 뜻인지 바로 안 보인다.
        if (parts.size() < 5)
        {
            std::printf("[CLI] 사용법: object.transform <이름> <px> <py> <pz>"
                " [rx ry rz] [sx sy sz]\n"
                "       이름에 공백이 있으면 따옴표로 묶는다: \"Main Camera\"\n");
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

        const auto number = [&](size_t index, float fallback) -> float
        {
            return (parts.size() > index)
                ? static_cast<float>(std::atof(parts[index].c_str())) : fallback;
        };

        const math::vector3 position{ number(2, 0.f), number(3, 0.f), number(4, 0.f) };
        const math::vector3 euler{ number(5, 0.f), number(6, 0.f), number(7, 0.f) };
        const math::vector3 scale{ number(8, 1.f), number(9, 1.f), number(10, 1.f) };

        object->Transform_().SetPosition(position);
        object->Transform_().SetRotation(math::quaternion_from_pitch_yaw_roll(
            math::radians(euler.x), math::radians(euler.y), math::radians(euler.z)));
        object->Transform_().SetScale(scale);
        object->Transform_().UpdateWorldMatrix();

        // ★ 이 경로도 오버라이드로 기록한다 (SceneGraphRedesignPlan P-write S4).
        //
        // object.property와 달리 여기는 리플렉션 세터(ApplyReflectedProperty)를
        // 지나지 않고 Transform의 세터를 직접 부른다 — 실측으로 확인한, CLI에서
        // Property::setter를 우회하는 유일한 다른 쓰기 경로다. 이 슬라이스를 빼면
        // object.transform으로 만든 로컬 수정은 S3 이후에도 여전히 조용히 유실된다.
        //
        // Transform은 S1-b+S3에서 컴포넌트로 승격됐으므로 다른 컴포넌트와 똑같이
        // 다룬다(순번도 ComputeComponentSlot이 센다). 세 필드는 Transform::reflect()
        // 의 이름 그대로다 — 이름이 어긋나면 RecordPropertyOverride가 프로퍼티 노드를
        // 못 찾고 조용히 아무것도 안 남기므로, 필드명을 바꿀 때 여기도 함께 본다.
        PrefabUtility::RecordPropertyOverride(*object, object->Transform_(), "position");
        PrefabUtility::RecordPropertyOverride(*object, object->Transform_(), "rotation");
        PrefabUtility::RecordPropertyOverride(*object, object->Transform_(), "scale");

        char message[192]{};
        std::snprintf(message, sizeof(message),
            "[CLI] 변환 설정: %s pos(%.2f %.2f %.2f) rot(%.1f %.1f %.1f) scale(%.2f %.2f %.2f)",
            parts[1].c_str(), position.x, position.y, position.z,
            euler.x, euler.y, euler.z, scale.x, scale.y, scale.z);
        Debug->LogWarning(message);
        std::printf("%s\n", message);
    }

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

    static void Cmd_object_property(const ConsoleCommandContext& ctx)
    {
        const std::vector<std::string>& parts = ctx.parts;
        const std::string& line = ctx.line;
        const std::string& cmd = ctx.cmd;

        // object.property <오브젝트> <컴포넌트> <필드> <값...>
        //
        // 리플렉션으로 설정한다. 컴포넌트마다 전용 명령을 만들면 종류가 늘 때마다
        // CLI가 같이 늘고, 그건 인스펙터가 이미 하는 일을 두 번 하는 것이다 —
        // 인스펙터도 같은 프로퍼티 목록을 훑는다.
        if (parts.size() < 5)
        {
            std::printf("[CLI] 사용법: object.property <오브젝트> <컴포넌트> <필드> <값...>\n");
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

        const Meta::Type* requestedType = Meta::Find(parts[2]);
        Component* target = nullptr;
        for (const auto& component : object->m_components)
        {
            if (component && (component->ToString() == parts[2]
                || (requestedType && component->GetTypeID() == requestedType->typeID)))
            {
                target = component.get();
                break;
            }
        }
        if (nullptr == target)
        {
            Debug->LogError("[CLI] 컴포넌트를 찾을 수 없음: " + parts[1] + "." + parts[2]);
            std::printf("[CLI] 컴포넌트를 찾을 수 없음: %s\n", parts[2].c_str());
            return;
        }

        // 값에 쉼표로 구분한 성분이 들어올 수 있다(벡터·색). 필드 이름 뒤 전체.
        std::string rest = TrimLine(line.substr(cmd.size()));
        for (size_t i = 1; i <= 3; ++i) rest = TrimLine(rest.substr(rest.find(parts[i]) + parts[i].size()));
        const std::string rawValue = rest;

        if (!ApplyReflectedProperty(target, parts[3], rawValue))
        {
            // 실패를 로그에도 남긴다. 콘솔 출력은 스크립트 실행에서 리다이렉트되지
            // 않아 보이지 않고, 그러면 '설정한 줄 알았는데 안 된' 씬이 저장된다.
            Debug->LogError("[CLI] 프로퍼티 설정 실패: " + parts[2] + "." + parts[3]
                + " = " + rawValue);
            std::printf("[CLI] 프로퍼티 설정 실패: %s.%s\n", parts[2].c_str(), parts[3].c_str());
            return;
        }

        // ★ 저작 의도의 기록 지점 (SceneGraphRedesignPlan P-write S3).
        //
        // 프리팹 인스턴스라면 이 수정이 "로컬 수정"이고, 기록해 두지 않으면 다음
        // 프리팹 갱신이 에러도 로그도 없이 덮어쓴다. 값은 넘기지 않는다 —
        // RecordPropertyOverride가 방금 쓰인 컴포넌트 상태에서 직접 뽑는다.
        //
        // 부수 효과 하나를 알고 쓴다: 이 목록이 비어 있지 않게 되는 순간
        // UpdateInstances의 과도기 시딩(m_prefabOverrides.empty() 조건)이 더 이상
        // 돌지 않는다. 그건 손실이 아니라 이득이다 — 그 시딩은 m_name·m_instanceID·
        // m_index·m_parentIndex 같은 **엔진 장부까지 사용자 수정으로 오기록**한다
        // (실측: 8건 중 7건). 정본이 서면 추론은 물러나는 것이 맞다.
        PrefabUtility::RecordPropertyOverride(*object, *target, parts[3]);

        Debug->LogWarning("[CLI] 프로퍼티 설정: " + parts[1] + "." + parts[2] + "."
            + parts[3] + " = " + rawValue);
        std::printf("[CLI] 프로퍼티 설정: %s.%s = %s\n", parts[2].c_str(), parts[3].c_str(),
            rawValue.c_str());
    }

    static void Cmd_model_load(const ConsoleCommandContext& ctx)
    {
        const std::vector<std::string>& parts = ctx.parts;
        const std::string& line = ctx.line;
        const std::string& cmd = ctx.cmd;

        if (parts.size() < 2)
        {
            std::printf("[CLI] 사용법: model.load <모델 경로>\n");
            return;
        }

		// 경로에 공백이 들어갈 수 있으므로 명령어 뒤 전체를 경로로 본다.
		const std::string path = TrimLine(line.substr(cmd.size()));
		const std::string modelName = file::path(path).stem().string();
		const std::shared_ptr<Model> previousGeneration =
			DataSystems->FindCachedModel(modelName);
		const file::path imported = EditorAssetDatabase::Get().ImportSourceAsset(
			path, EditorAssetDatabase::ImportKind::Model);
		if (imported.empty())
		{
			std::printf("[CLI] 모델 임포트 실패: %s\n", path.c_str());
			return;
		}
		DataSystems->LoadModel(imported.string());
		const std::shared_ptr<Model> loadedGeneration =
			DataSystems->FindCachedModel(imported.stem().string());
		const char* cacheResult = previousGeneration && loadedGeneration &&
			previousGeneration != loadedGeneration ? "reloaded" : "loaded";
		std::printf("[CLI] 모델 임포트 및 로드 요청: %s (runtime-cache=%s)\n",
			imported.string().c_str(), cacheResult);
    }

	static void Cmd_terrain_authoring_probe(const ConsoleCommandContext& ctx)
	{
		if (ctx.parts.size() < 3)
		{
			std::printf("[terrain.authoring.probe] usage: <name> <texture|->\n");
			return;
		}

		TerrainAuthoringRequest request{};
		request.destinationDirectory = PathFinder::Relative("Terrain");
		request.name = StringToWstring(ctx.parts[1]);
		request.terrainId = 73;
		request.width = 2;
		request.height = 2;
		request.minHeight = -4.0f;
		request.maxHeight = 8.0f;
		request.heightMap = { -4.0f, 0.5f, 3.0f, 8.0f };

		TerrainAuthoringLayerSnapshot layer{};
		layer.layerId = 0;
		layer.name = "ProbeLayer";
		layer.diffuseTextureSource = ctx.parts[2] == "-"
			? request.destinationDirectory / "__missing_terrain_probe__.png"
			: file::path(ctx.parts[2]);
		layer.tiling = 2.0f;
		layer.splatWeights = { 0.0f, 0.25f, 0.75f, 1.0f };
		request.layers.push_back(std::move(layer));

		TerrainAuthoringResult result{};
		const bool written = AssetAuthoringPort::WriteTerrain(request, result);
		std::printf("[terrain.authoring.probe] %s path=%s guid=%s\n",
			written ? "committed" : "rejected",
			result.descriptorPath.string().c_str(),
			result.guid.ToString().c_str());
	}

	// Foliage 저작 트랜잭션을 실행 중인 Editor에서 그대로 태운다. escape 인자는
	// 목적지가 Foliage 루트를 벗어났을 때 거부되는지 보는 음성 경로다.
	static void Cmd_foliage_authoring_probe(const ConsoleCommandContext& ctx)
	{
		if (ctx.parts.size() < 2)
		{
			std::printf("[foliage.authoring.probe] usage: <name> [escape]\n");
			return;
		}

		const bool escape = ctx.parts.size() >= 3 && ctx.parts[2] == "escape";

		TextAssetAuthoringRequest request{};
		request.destinationDirectory = escape
			? PathFinder::Relative("Terrain") : PathFinder::Relative("Foliage");
		request.name = StringToWstring(ctx.parts[1]);

		FoliageInstance source{};
		source.m_position = { 12.5f, 3.25f, -8.75f };
		source.m_rotation = { 15.f, 90.f, 270.f };
		source.m_scale = { 0.5f, 1.25f, 2.f };
		source.m_foliageTypeID = 7;
		source.m_isCulled = true;
		source.RebuildWorldMatrix();

		MetaYml::Node typesNode(MetaYml::NodeType::Sequence);
		MetaYml::Node instancesNode(MetaYml::NodeType::Sequence);
		instancesNode.push_back(Meta::Serialize(&source));
		MetaYml::Node assetNode;
		assetNode["FoliageAsset"]["Types"] = typesNode;
		assetNode["FoliageAsset"]["Instances"] = instancesNode;
		std::ostringstream payload;
		payload << assetNode;
		request.payload = payload.str();

		TextAssetAuthoringResult result{};
		const bool written = AssetAuthoringPort::WriteFoliage(request, result);
		bool schemaStable = false;
		bool roundTrip = false;
		bool derivedWorld = false;
		if (written)
		{
			try
			{
				const MetaYml::Node published = MetaYml::LoadFile(result.assetPath.string());
				const MetaYml::Node publishedInstances =
					published["FoliageAsset"]["Instances"];
				if (publishedInstances.IsSequence() && 1 == publishedInstances.size())
				{
					const MetaYml::Node instanceNode = publishedInstances[0];
					schemaStable = 4 == instanceNode.size() &&
						instanceNode["m_position"] && instanceNode["m_rotation"] &&
						instanceNode["m_scale"] && instanceNode["m_foliageTypeID"] &&
						!instanceNode["m_isCulled"] && !instanceNode["m_worldMatrix"];

					FoliageInstance loaded{};
					Meta::Deserialize(&loaded, instanceNode);
					roundTrip = loaded.m_position == source.m_position &&
						loaded.m_rotation == source.m_rotation &&
						loaded.m_scale == source.m_scale &&
						loaded.m_foliageTypeID == source.m_foliageTypeID &&
						!loaded.m_isCulled &&
						loaded.m_worldMatrix == math::matrix4x4::identity();
					loaded.RebuildWorldMatrix();
					derivedWorld = math::near_equal(
						loaded.m_worldMatrix, source.m_worldMatrix);
				}
			}
			catch (const std::exception&)
			{
				schemaStable = false;
				roundTrip = false;
				derivedWorld = false;
			}
		}

		const bool verified = written && schemaStable && roundTrip && derivedWorld;
		std::printf("[foliage.authoring.probe] %s path=%s guid=%s fields=%s "
			"roundtrip=%s derived=%s\n",
			written ? (verified ? "committed" : "invalid") : "rejected",
			result.assetPath.string().c_str(),
			result.guid.ToString().c_str(),
			schemaStable ? "4-runtime-absent" : "invalid",
			roundTrip ? "PASS" : "FAIL",
			derivedWorld ? "PASS" : "FAIL");
		if (written && !verified)
			EngineBootstrap::SetExitCode(5);
	}

	// 애니메이터 컨트롤러 json은 카탈로그에 없는 저작 프리셋이다. 이름에 '.'이 든
	// 경우가 잘리지 않는지와 목적지가 AnimatorController 루트를 벗어나면 거부되는지를
	// 본다. 값 왕복(DeserializeControllers)은 태우지 않는다 — 상태 0개 컨트롤러의
	// m_curState 누락과 파싱 실패 시 기존 상태를 지우는 것은 이관 이전부터 있던
	// 별개 결함이라, 여기서 태우면 게이트가 그 결함으로 붉어진다.
	static void Cmd_animator_authoring_probe(const ConsoleCommandContext& ctx)
	{
		if (ctx.parts.size() < 2)
		{
			std::printf("[animator.authoring.probe] usage: <name> [escape]\n");
			return;
		}

		const bool escape = ctx.parts.size() >= 3 && ctx.parts[2] == "escape";
		if (escape)
		{
			UncatalogedAuthoringRequest request{};
			request.destinationPath =
				PathFinder::InputMapPath(ctx.parts[1] + ".json");
			request.payload = "{}\n";
			const bool written =
				AssetAuthoringPort::WriteAnimatorController(request);
			std::printf("[animator.authoring.probe] %s\n",
				written ? "committed" : "rejected");
			return;
		}

		Animator probe;
		const bool saved = probe.SerializeControllers(ctx.parts[1]);
		std::printf("[animator.authoring.probe] save=%s\n",
			saved ? "ok" : "failed");
	}

	// 입력 액션맵은 맵마다 파일 하나다. 이름에 '.'이 든 맵도 잘리지 않는지, 액션이
	// 0개인 맵도 저장·재로드되는지, 그리고 저장한 것이 디렉터리 스캔으로 다시
	// 읽히는지를 함께 본다.
	static void Cmd_inputmap_authoring_probe(const ConsoleCommandContext& ctx)
	{
		if (ctx.parts.size() < 2)
		{
			std::printf("[inputmap.authoring.probe] usage: <save|verify> <name>\n");
			return;
		}

		const std::string& action = ctx.parts[1];
		const std::string name = ctx.parts.size() >= 3 ? ctx.parts[2] : std::string{};

		if (action == "save")
		{
			ActionMap* map = InputActionManagers->AddActionMap(name);
			if (nullptr == map)
			{
				std::printf("[inputmap.authoring.probe] rejected no-map\n");
				return;
			}
			const bool saved = InputActionManagers->SerializeMap(map);
			std::printf("[inputmap.authoring.probe] save=%s\n",
				saved ? "ok" : "failed");
			return;
		}

		if (action == "verify")
		{
			InputActionManagers->LoadManager();
			size_t found = 0;
			for (ActionMap* map : InputActionManagers->m_actionMaps)
			{
				if (map && map->m_name == name) ++found;
			}
			std::printf("[inputmap.authoring.probe] verify found=%zu\n", found);
			return;
		}

		std::printf("[inputmap.authoring.probe] unknown action %s\n", action.c_str());
	}

	// 태그 저작은 편집이 아니라 **종료 시 Finalize**가 디스크에 반영한다. 그 저장이
	// authoring handler 수명 창 안에서 일어나는지는 "추가하고 정상 종료 → 다시 켜서
	// 확인"으로만 증명된다 — 한 프로세스 안에서는 메모리 상태만 보게 된다.
	// SerializationPlan D3-b-L — ShaderMeta **읽기 경로**를 관측 가능하게 만든다.
	//
	// ★ 왜 새로 만들었나. 이 파서의 계약은 `dx12.selftest` 안에만 있다. 그런데 그것은
	//   회귀 세트(run-all)에 **없고**, 자기 하네스(`Invoke-DX12Validation.ps1`)는
	//   vcpkg baseline preflight에 막혀 지금 이 기계에서 돌지 않는다.
	//
	//   변이로 확인했다 — `ValidateMap`의 unknown-field 거부를 무력화하면
	//   `dx12.selftest`는 빨개지지만 `verify-experiment-asset-cooker`는 **초록이다.**
	//   즉 정기적으로 도는 게이트 중 이 경로를 지키는 것이 하나도 없다. ryml 이식
	//   **전에** 자를 먼저 세운다. TagManager에서 같은 순서를 놓쳐 저작 자산을 잃었다.
	//
	// ★ 두 방향을 함께 낸다. 실자산이 파싱되는지(수용)와 잘못된 문서가 거부되는지
	//   (거절). 저작 코퍼스는 전부 유효하므로 수용만 재면 **느슨해지는 이식**을
	//   원리적으로 못 잡는다 — 그리고 그것이 backend 교체에서 가장 흔한 실패다.
	//
	// ★ 거절 사례에는 ryml 고유 위험을 넣었다. `operator[]`는 없는 키에서 **abort**
	//   하므로(어댑터가 `find_child`로 흡수한다) "필수 키 누락"이 조용한 실패가 아니라
	//   프로세스 사망이 될 수 있다. 그 경계를 여기서 상시로 밟는다.
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

	static void Cmd_tag_authoring_probe(const ConsoleCommandContext& ctx)
	{
		// D3-b-L: `list`는 **디스크에서 읽은 결과**를 찍는다. 나머지 동작은
		// 메모리 조작이라 Load 경로를 전혀 재지 않았다 — 그래서 TagManager를
		// ryml로 옮겼을 때 어떤 게이트도 그 변이를 잡지 못했다(실측).
		if (ctx.parts.size() >= 2 && ctx.parts[1] == "list")
		{
			TagManagers->Load();
			const auto& tags = TagManagers->GetTags();
			const auto& layers = TagManagers->GetLayers();
			std::printf("[tag.authoring.probe] loaded tags=%zu layers=%zu\n",
				tags.size(), layers.size());
			for (const std::string& tag : tags)
				std::printf("[tag.authoring.probe] tag=%s\n", tag.c_str());
			for (const std::string& layer : layers)
				std::printf("[tag.authoring.probe] layer=%s\n", layer.c_str());
			return;
		}

		if (ctx.parts.size() < 3)
		{
			std::printf("[tag.authoring.probe] usage: list | <add|has|remove> <name>\n");
			return;
		}

		const std::string& action = ctx.parts[1];
		const std::string& name = ctx.parts[2];

		if (action == "add") TagManagers->AddTag(name);
		else if (action == "remove") TagManagers->RemoveTag(name);
		else if (action != "has")
		{
			std::printf("[tag.authoring.probe] unknown action %s\n", action.c_str());
			return;
		}

		std::printf("[tag.authoring.probe] %s has=%s\n", action.c_str(),
			TagManagers->HasTag(name) ? "true" : "false");
	}

	// 충돌 행렬은 프로젝트 설정 자산이라 meta를 만들지 않는다. 저장 후 다시 읽어
	// 값이 돌아오는지 보고, escape 인자로 설정 루트 밖 목적지가 거부되는지 본다.
	static void Cmd_collisionmatrix_authoring_probe(const ConsoleCommandContext& ctx)
	{
		const bool escape = ctx.parts.size() >= 2 && ctx.parts[1] == "escape";
		if (escape)
		{
			UncatalogedAuthoringRequest request{};
			request.destinationPath =
				PathFinder::Relative("Foliage") / "CollisionMatrix.asset";
			request.payload = "0:\n  0: true\n";
			const bool written = AssetAuthoringPort::WriteCollisionMatrix(request);
			std::printf("[collisionmatrix.authoring.probe] %s\n",
				written ? "committed" : "rejected");
			return;
		}

		auto matrix = PhysicsManagers->GetCollisionMatrix();
		if (matrix.size() < 2 || matrix[0].size() < 2)
		{
			std::printf("[collisionmatrix.authoring.probe] rejected matrix=%zu\n",
				matrix.size());
			return;
		}

		const uint8_t before = matrix[0][1];
		matrix[0][1] = before ? 0 : 1;
		PhysicsManagers->SetCollisionMatrix(matrix);
		if (!PhysicsManagers->SaveCollisionMatrix())
		{
			std::printf("[collisionmatrix.authoring.probe] rejected\n");
			return;
		}

		// 메모리를 되돌린 뒤 파일에서 다시 읽는다. 디스크를 실제로 거치지 않았다면
		// 여기서 뒤집힌 값이 돌아오지 않는다.
		matrix[0][1] = before;
		PhysicsManagers->SetCollisionMatrix(matrix);
		PhysicsManagers->LoadCollisionMatrix();
		const uint8_t reloaded = PhysicsManagers->GetCollisionMatrix()[0][1];

		// 저장소의 CollisionMatrix.asset을 원래대로 돌려놓는다.
		matrix[0][1] = before;
		PhysicsManagers->SetCollisionMatrix(matrix);
		const bool restored = PhysicsManagers->SaveCollisionMatrix();

		std::printf("[collisionmatrix.authoring.probe] committed roundtrip=%s restored=%s\n",
			reloaded != before ? "ok" : "mismatch", restored ? "ok" : "failed");
	}

	// Blackboard는 Foliage와 달리 실제 runtime 타입의 직렬화 경로를 그대로 태운다.
	// key 하나를 넣고 저장한 뒤 같은 이름으로 다시 읽어 값이 살아 돌아오는지 본다.
	static void Cmd_blackboard_authoring_probe(const ConsoleCommandContext& ctx)
	{
		if (ctx.parts.size() < 2)
		{
			std::printf("[blackboard.authoring.probe] usage: <name> [empty|noname]\n");
			return;
		}

		const std::string mode = ctx.parts.size() >= 3 ? ctx.parts[2] : "";
		const bool empty = mode == "empty";
		const bool noName = mode == "noname";

		BlackBoard board;
		if (!empty)
		{
			board.SetValueAsInt("ProbeKey", 4177);
		}

		if (!board.Serialize(noName ? std::string_view{} : ctx.parts[1]))
		{
			std::printf("[blackboard.authoring.probe] rejected\n");
			return;
		}

		BlackBoard reloaded;
		int roundTrip = 0;
		try
		{
			reloaded.Deserialize(ctx.parts[1]);
			if (reloaded.HasKey("ProbeKey"))
				roundTrip = reloaded.GetValueAsInt("ProbeKey");
		}
		catch (const std::exception& exception)
		{
			std::printf("[blackboard.authoring.probe] reload-failed %s\n",
				exception.what());
			return;
		}

		std::printf("[blackboard.authoring.probe] committed keys=%zu roundtrip=%d\n",
			reloaded.GetValues().size(), roundTrip);
	}

	static void Cmd_asset_guid_rename_probe(const ConsoleCommandContext&)
	{
		// D2-c: 새 material payload와 sidecar가 같은 UUIDv4를 갖고, target+meta
		// rename 뒤에도 catalog의 GUID->path 참조가 그대로 새 경로를 가리키는지
		// 한 transaction으로 확인한다. 고정 이름을 쓰지 않아 이전 실패 잔재나
		// 병렬 실행과 충돌하지 않는다.
		const FileGuid requestedGuid = FileGuid::CreateRandomV4();
		std::string suffix = requestedGuid.ToString();
		std::erase(suffix, '-');
		const std::string sourceName = "D2GuidRenameProbe_" + suffix;
		const std::string destinationName = sourceName + "_Renamed";
		const file::path sourcePath = PathFinder::Relative("Materials\\") /
			(sourceName + ".asset");
		const file::path destinationPath = PathFinder::Relative("Materials\\") /
			(destinationName + ".asset");
		const file::path sourceMeta = sourcePath.string() + ".meta";
		const file::path destinationMeta = destinationPath.string() + ".meta";

		auto cleanup = [](const file::path& assetPath)
		{
			DataSystems->ApplyAssetChange({ RuntimeAssetChangeKind::Removed,
				RuntimeAssetType::Material, {}, assetPath });
			std::error_code ignored;
			file::remove(assetPath, ignored);
			ignored.clear();
			file::remove(assetPath.string() + ".meta", ignored);
		};

		Material authored;
		authored.m_name = sourceName;
		authored.m_fileGuid = requestedGuid;
		authored.m_materialInfo.m_roughness = 0.375f;

		bool saved = false;
		bool renamed = false;
		bool identityPreserved = false;
		bool materialRoundTrip = false;
		FileGuid canonicalGuid{};
		try
		{
			saved = EditorAssetDatabase::Get().SaveMaterial(&authored);
			canonicalGuid = DataSystems->GetFileGuid(sourcePath);
			if (saved && canonicalGuid == requestedGuid)
			{
				const FileGuid movedGuid = EditorAssetDatabase::Get().RenameAsset(
					sourcePath, destinationPath);
				renamed = movedGuid == canonicalGuid;
				identityPreserved = renamed
					&& !file::exists(sourcePath) && !file::exists(sourceMeta)
					&& file::is_regular_file(destinationPath)
					&& file::is_regular_file(destinationMeta)
					&& DataSystems->GetFileGuid(sourcePath) == FileGuid{}
					&& DataSystems->GetFileGuid(destinationPath) == canonicalGuid
					&& DataSystems->GetFilePath(canonicalGuid).lexically_normal()
						== destinationPath.lexically_normal();

				if (identityPreserved)
				{
					const YAML::Node persisted = YAML::LoadFile(destinationPath.string());
					Material decoded;
					materialRoundTrip = DataSystems->DeserializeMaterialPayload(
						decoded, Authoring::NodeViewAccess::Make(persisted))
						&& decoded.m_fileGuid == canonicalGuid
						&& decoded.m_materialInfo.m_roughness
							== authored.m_materialInfo.m_roughness
						&& YAML::Dump(persisted) == YAML::Dump(
							DataSystems->SerializeMaterialPayload(decoded));
				}
			}
		}
		catch (const std::exception& exception)
		{
			Debug->LogError("[asset.guid.rename] probe exception: "
				+ std::string(exception.what()));
		}

		cleanup(sourcePath);
		cleanup(destinationPath);
		const bool passed = saved && renamed && identityPreserved
			&& materialRoundTrip
			&& canonicalGuid.IsRandomV4();
		std::printf("[asset.guid.rename] %s guid=%s save=%s move=%s "
			"identity=%s material-roundtrip=%s cleanup=%s\n",
			passed ? "pass" : "fail", canonicalGuid.ToString().c_str(),
			saved ? "yes" : "no", renamed ? "yes" : "no",
			identityPreserved ? "yes" : "no",
			materialRoundTrip ? "yes" : "no",
			(!file::exists(sourcePath) && !file::exists(sourceMeta)
				&& !file::exists(destinationPath) && !file::exists(destinationMeta))
				? "yes" : "no");
		if (!passed) EngineBootstrap::SetExitCode(6);
	}

	static void Cmd_material_corpus_probe(const ConsoleCommandContext& ctx)
	{
		// D2-d: 실제 standalone material corpus를 파일 수정 없이 메모리에서 두 번
		// 왕복한다. UUID version은 전역 strict gate의 책임이고, 여기서는 sidecar
		// 정본과 payload mirror가 같은 identity인지 및 asset reference가 보존되는지만
		// 판정한다.
		if (ctx.parts.size() < 2)
		{
			std::printf("[CLI] 사용법: material.corpus.probe <머티리얼 이름>...\n");
			EngineBootstrap::SetExitCode(6);
			return;
		}

		auto captureReferences = [](const Material& material)
		{
			std::vector<std::string> rows;
			rows.push_back("shader|" + material.m_shaderMetaGuid.ToString());
			for (const MaterialPropertyValue& property : material.m_propertyValues)
			{
				if (property.m_textureGuid == FileGuid{}) continue;
				rows.push_back("texture|" + property.m_name + "|"
					+ property.m_textureGuid.ToString());
			}
			std::ranges::sort(rows);
			return rows;
		};

		size_t passedCount = 0;
		size_t textureReferenceCount = 0;
		for (size_t index = 1; index < ctx.parts.size(); ++index)
		{
			const std::string& name = ctx.parts[index];
			const file::path assetPath = PathFinder::Relative("Materials\\") /
				(name + ".asset");
			bool decoded = false;
			bool identity = false;
			bool shader = false;
			bool textures = false;
			bool stable = false;
			size_t materialTextureReferences = 0;
			try
			{
				const FileGuid catalogGuid = DataSystems->GetFileGuid(assetPath);
				const YAML::Node source = YAML::LoadFile(assetPath.string());
				Material first;
				decoded = DataSystems->DeserializeMaterialPayload(
					first, Authoring::NodeViewAccess::Make(source));
				identity = decoded && catalogGuid != FileGuid{}
					&& first.m_fileGuid == catalogGuid;

				const file::path shaderPath = decoded
					? DataSystems->GetFilePath(first.m_shaderMetaGuid) : file::path{};
				shader = decoded && first.m_shaderMetaGuid != FileGuid{}
					&& !shaderPath.empty() && file::is_regular_file(shaderPath)
					&& shaderPath.extension() == ".shadermeta";

				textures = decoded;
				if (decoded)
				{
					for (const MaterialPropertyValue& property : first.m_propertyValues)
					{
						if (property.m_textureGuid == FileGuid{}) continue;
						++materialTextureReferences;
						const file::path texturePath = DataSystems->GetFilePath(
							property.m_textureGuid);
						if (texturePath.empty() || !file::is_regular_file(texturePath))
							textures = false;
					}
				}

				if (decoded)
				{
					const YAML::Node firstCanonical =
						DataSystems->SerializeMaterialPayload(first);
					Material second;
					const bool decodedAgain = DataSystems->DeserializeMaterialPayload(
						second, Authoring::NodeViewAccess::Make(firstCanonical));
					const YAML::Node secondCanonical = decodedAgain
						? DataSystems->SerializeMaterialPayload(second) : YAML::Node{};
					stable = decodedAgain
						&& second.m_fileGuid == first.m_fileGuid
						&& captureReferences(second) == captureReferences(first)
						&& YAML::Dump(secondCanonical) == YAML::Dump(firstCanonical);
				}
			}
			catch (const std::exception& exception)
			{
				Debug->LogError("[material.corpus] " + name + ": "
					+ exception.what());
			}

			const bool passed = decoded && identity && shader && textures && stable;
			if (passed) ++passedCount;
			textureReferenceCount += materialTextureReferences;
			std::printf("[material.corpus] %s %s identity=%s shader=%s "
				"textures=%zu/%s stable=%s\n",
				name.c_str(), passed ? "pass" : "fail",
				identity ? "yes" : "no", shader ? "yes" : "no",
				materialTextureReferences, textures ? "valid" : "invalid",
				stable ? "yes" : "no");
		}

		const size_t total = ctx.parts.size() - 1;
		const bool passed = passedCount == total;
		std::printf("[material.corpus] %s materials=%zu/%zu textureRefs=%zu\n",
			passed ? "pass" : "fail", passedCount, total, textureReferenceCount);
		if (!passed) EngineBootstrap::SetExitCode(6);
	}

    static void Cmd_model_place(const ConsoleCommandContext& ctx)
    {
        const std::vector<std::string>& parts = ctx.parts;

        if (parts.size() < 2)
        {
            std::printf("[CLI] 사용법: model.place <모델 이름>\n");
            return;
        }

        // 콘텐츠 브라우저에서 씬으로 끌어다 놓는 것과 같은 경로.
        Scene* scene = SceneManagers->GetActiveScene();
        auto model = DataSystems->FindCachedModel(parts[1]);
        if (!scene || !model)
        {
            std::printf("[CLI] 씬 또는 모델을 찾을 수 없음: %s\n", parts[1].c_str());
            return;
        }

        Model::LoadModelToScene(model.get(), *scene);
        std::printf("[CLI] 씬에 배치: %s\n", parts[1].c_str());
    }

    // I5-D4e-2 — 이벤트·루프 오버라이드의 소유 이관 게이트. 코퍼스에 저작분이
    // 0이라 실자산 게이트는 원리적으로 초록이므로([[plan-target-may-be-already-
    // dead]]의 그 함정) 합성으로 판정한다: seed가 합성 오버라이드(루프 false·
    // 이벤트 2)를 주입하고, 저장·재로드 뒤 verify가 ①왕복(Animator 소유로
    // 살아남았는가) ②비오염(공유 자산 m_animations가 불변인가 — 재주입 청산
    // 실증) ③발화 매칭(구간·되감김 규칙이 오버라이드·IsClipLooping을 소비
    // 하는가)을 잰다. 이관은 A/B 스위치와 무관한 무조건 경로라 on/off 대조군
    // 양쪽에서 같은 판정이어야 한다.
    static void Cmd_experiment_animevent(const ConsoleCommandContext& ctx)
    {
        Scene* scene = SceneManagers->GetActiveScene();
        if (nullptr == scene || ctx.parts.size() < 2)
        {
            std::printf("[CLI] 사용법: experiment.animevent seed|verify\n");
            return;
        }
        const bool isSeed = "seed" == ctx.parts[1];

        Animator* animator = nullptr;
        for (const auto& object : scene->m_Entities)
        {
            if (!object || object->IsDestroyMark()) continue;
            // I6-B3 — 후보 선별이 창구를 탄다. legacy 객체 존재를 관문으로
            // 쓰면 대입을 끊는 순간 이 게이트가 "animators=0 skip"으로 조용히
            // 비어 버린다(초록인 채로). 값 대조 arm은 아래에 그대로 둔다 —
            // 그것은 은퇴가 아니라 대조군이고, B4에서 함께 죽는다.
            Animator* candidate = object->GetComponent<Animator>();
            if (nullptr == candidate || 0 == candidate->GetSkeletonSerial())
                continue;
            if (0 == candidate->GetClipCount()) continue;
            animator = candidate;
            break;
        }
        if (nullptr == animator)
        {
            std::printf("[CLI] experiment.animevent %s skip animators=0\n",
                ctx.parts[1].c_str());
            return;
        }

        if (isSeed)
        {
            animator->SetClipLooping(0, false);
            AnimatorClipOverride& clipOverride = animator->EnsureClipOverride(0);
            clipOverride.events.clear();
            KeyFrameEvent early;
            early.m_eventName = "gateEvent25";
            early.m_scriptName = "GateScript";
            early.m_funName = "GateFun25";
            early.key = 0.25f;
            early.frameKey = 25;
            KeyFrameEvent late;
            late.m_eventName = "gateEvent75";
            late.m_scriptName = "GateScript";
            late.m_funName = "GateFun75";
            late.key = 0.75f;
            late.frameKey = 75;
            clipOverride.events.push_back(early);
            clipOverride.events.push_back(late);
            std::printf("[CLI] experiment.animevent seed done clip=0 "
                "loop=false events=2\n");
            return;
        }

        std::vector<std::string> failures;
        // ① 왕복 — 저장·재로드가 Animator 소유 오버라이드를 보존했는가.
        const AnimatorClipOverride* clipOverride = animator->FindClipOverride(0);
        if (nullptr == clipOverride)
        {
            failures.push_back("왕복: 클립 0 오버라이드 부재");
        }
        else
        {
            if (!clipOverride->loopOverride.has_value()
                || false != *clipOverride->loopOverride)
            {
                failures.push_back("왕복: loop 오버라이드 소실");
            }
            if (2 != clipOverride->events.size()
                || "gateEvent25" != clipOverride->events[0].m_eventName
                || "GateFun25" != clipOverride->events[0].m_funName
                || std::abs(clipOverride->events[0].key - 0.25f) > 1e-6f
                || "gateEvent75" != clipOverride->events[1].m_eventName)
            {
                failures.push_back("왕복: 이벤트 필드 소실("
                    + std::to_string(clipOverride->events.size()) + "개)");
            }
        }
        // ② 비오염 — 공유 자산이 불변인가(재주입 청산 실증). 자산 원본 루프는
        // experiment 자산값과 동치여야 한다(둘 다 임포터 산물).
        //
        // ★ I6-B3 — 이 축은 **legacy 대조군**이라 legacy 자산이 없으면 잴 것이
        //   없다. 예전에는 후보 선별이 m_Skeleton 널을 걸러 줘서 여기가
        //   무방비로 역참조했고, B4 예행(대입 절단)에서 정확히 그 자리가
        //   ACCESS_VIOLATION으로 죽었다 — 관문을 창구로 옮기면 그 암묵 보장이
        //   사라진다. 이제 없으면 건너뛰되 **출력 토큰으로 드러낸다**(n/a).
        //   조용히 건너뛰면 대조군이 사라진 채로 초록이 나온다.
        const char* contaminationAxis = "none";
        Skeleton* sharedSkeleton = animator->m_Skeleton;
        if (nullptr == sharedSkeleton || sharedSkeleton->m_animations.empty())
        {
            contaminationAxis = "n/a";
        }
        else
        {
            if (!sharedSkeleton->m_animations[0].m_keyFrameEvent.empty())
            {
                failures.push_back("비오염: 공유 자산에 이벤트 "
                    + std::to_string(
                        sharedSkeleton->m_animations[0].m_keyFrameEvent.size())
                    + "건 주입됨");
            }
            if (animator->m_experimentModel)
            {
                if (const experiment::Skeleton* experimentSkeleton =
                    animator->m_experimentModel->TryGetSkeleton())
                {
                    if (!experimentSkeleton->clips.empty()
                        && sharedSkeleton->m_animations[0].m_isLoop
                            != experimentSkeleton->clips[0].looping)
                    {
                        failures.push_back(
                            "비오염: 공유 자산 m_isLoop가 원본과 다름");
                    }
                }
            }
        }
        // ③ 발화 매칭 — 구간·되감김 규칙이 오버라이드와 IsClipLooping을 본다.
        if (1 != animator->InvokeClipEvents(0, 0.3f, 0.2f))
        {
            failures.push_back("발화: 일반 구간(0.2→0.3) 매칭≠1");
        }
        if (1 != animator->InvokeClipEvents(0, 0.8f, 0.7f))
        {
            failures.push_back("발화: 일반 구간(0.7→0.8) 매칭≠1");
        }
        if (0 != animator->InvokeClipEvents(0, 0.1f, 0.7f))
        {
            failures.push_back("발화: loop=false 되감김이 발화됨");
        }
        animator->SetClipLooping(0, true);
        if (1 != animator->InvokeClipEvents(0, 0.1f, 0.7f))
        {
            failures.push_back("발화: loop=true 되감김(0.75) 매칭≠1");
        }
        animator->SetClipLooping(0, false);
        // ④ 루프 판정 폴백 — 오버라이드 없는 클립은 자산값.
        if (false != animator->IsClipLooping(0))
        {
            failures.push_back("루프: 오버라이드가 정본이 아님");
        }

        if (failures.empty())
        {
            std::printf("[CLI] experiment.animevent verify pass "
                "roundtrip=ok contamination=%s firing=ok\n",
                contaminationAxis);
        }
        else
        {
            std::string joined;
            for (const std::string& failure : failures)
            {
                joined += " [" + failure + "]";
            }
            std::printf("[CLI] experiment.animevent verify fail%s\n",
                joined.c_str());
        }
    }

    // I5-D5a — Foliage 메시의 experiment 핸들 합류 게이트. 코퍼스에 Foliage
    // 저작분이 0이라(착수 정찰 실측) 합성으로 판정한다: seed가 씬에
    // FoliageComponent+타입(Gunner)+인스턴스를 저작 경로(AddFoliageType —
    // 바인딩 지점) 그대로 심고 foliage 자산을 게시하며, 저장·재로드 뒤 verify가
    // ①postLoad 재해석 경로의 바인딩 ②프록시 DrawSource의 핸들 반영
    // (CaptureDrawSources — 실물 함수) ③뷰 완비(stableKey)를 잰다. 렌더러
    // poolFoliage 분기는 헤드리스 관측 밖(라이브 렌더 0프레임 + dx12.scene
    // 하네스에 Foliage 대칭 구성 없음) — 계획서 한계 기록.
    static void Cmd_experiment_foliage(const ConsoleCommandContext& ctx)
    {
        Scene* scene = SceneManagers->GetActiveScene();
        if (nullptr == scene || ctx.parts.size() < 2)
        {
            std::printf("[CLI] 사용법: experiment.foliage seed <자산디렉터리>"
                " | verify experiment|legacy\n");
            return;
        }

        if ("seed" == ctx.parts[1])
        {
            if (ctx.parts.size() < 3)
            {
                std::printf("[CLI] experiment.foliage seed <자산디렉터리>\n");
                return;
            }
            auto model = DataSystems->FindCachedModel("Gunner_F_Mythic");
            if (!model)
            {
                std::printf("[CLI] experiment.foliage seed fail 모델 없음\n");
                return;
            }
            Entity* entity = scene->CreateEntity("GateFoliage",
                GameObjectType::Mesh, 0);
            FoliageComponent* foliage = entity->AddComponent<FoliageComponent>();
            FoliageType type(model->GetMeshShared(0),
                model->GetMaterialShared(0), true, model->name);
            foliage->AddFoliageType(type);
            FoliageInstance instance;
            instance.m_position = { 3.0f, 0.0f, 3.0f };
            instance.m_scale = { 0.1f, 0.1f, 0.1f };
            foliage->AddFoliageInstance(instance);
            foliage->SaveFoliageAsset(file::path(ctx.parts[2]), L"gate_foliage");
            const bool published = FileGuid{} != foliage->m_foliageAssetGuid;
            std::printf("[CLI] experiment.foliage seed %s types=%zu "
                "assetGuid=%s\n", published ? "done" : "fail",
                foliage->GetFoliageTypes().size(),
                published ? "ok" : "nil");
            return;
        }

        const bool expectExperiment = ctx.parts.size() >= 3
            && "experiment" == ctx.parts[2];
        FoliageComponent* foliage = nullptr;
        for (const auto& object : scene->m_Entities)
        {
            if (!object || object->IsDestroyMark()) continue;
            if (FoliageComponent* candidate =
                object->GetComponent<FoliageComponent>())
            {
                foliage = candidate;
                break;
            }
        }
        if (nullptr == foliage)
        {
            std::printf("[CLI] experiment.foliage verify skip components=0\n");
            return;
        }

        std::vector<std::string> failures;
        const auto& types = foliage->GetFoliageTypes();
        if (types.empty()) failures.push_back("왕복: 타입 0(자산 재로드 실패)");
        std::size_t boundTypes = 0;
        std::size_t authoredMaterialTypes = 0, authoredMaterialDraws = 0;
        for (const FoliageType& type : types)
        {
            if (nullptr == type.m_mesh)
            {
                failures.push_back("왕복: m_mesh 재해석 실패");
                continue;
            }
            if (type.m_experimentModel) ++boundTypes;
            // I5-D5c4(S2c-2c) — 재질 저작 정본도 같은 모델에서 잇는다. 메시
            // 핸들만 세면 재질 배선이 끊겨도 초록이다(축이 하나 모자란다).
            if (type.m_authoredMaterial) ++authoredMaterialTypes;
        }
        if (expectExperiment && boundTypes != types.size())
        {
            failures.push_back("바인딩: experiment 핸들 "
                + std::to_string(boundTypes) + "/"
                + std::to_string(types.size()));
        }
        if (!expectExperiment && 0 != boundTypes)
        {
            failures.push_back("바인딩: off인데 experiment 핸들 "
                + std::to_string(boundTypes));
        }
        if (expectExperiment && authoredMaterialTypes != types.size())
        {
            failures.push_back("재질: 저작 정본 "
                + std::to_string(authoredMaterialTypes) + "/"
                + std::to_string(types.size()));
        }
        if (!expectExperiment && 0 != authoredMaterialTypes)
        {
            failures.push_back("재질: off인데 저작 정본 "
                + std::to_string(authoredMaterialTypes));
        }

        // 실물 프록시 사슬 — 생성자·색인·DrawSource 캡처를 제품 함수 그대로.
        FoliageRenderProxy proxy(foliage);
        proxy.RebuildInstanceMap();
        const auto draws = proxy.CaptureDrawSources();
        if (draws.empty()) failures.push_back("프록시: DrawSource 0");
        std::size_t viewCompleteDraws = 0;
        for (const auto& draw : draws)
        {
            if (draw.authoredMaterial) ++authoredMaterialDraws;
            if (nullptr == draw.experimentModel) continue;
            RHIExperimentVertexView view{};
            if (DataSystem::BuildExperimentVertexView(*draw.experimentModel,
                draw.experimentMeshIndex, view) && 0 != view.stableKey)
            {
                ++viewCompleteDraws;
            }
        }
        if (expectExperiment && viewCompleteDraws != draws.size())
        {
            failures.push_back("뷰: 완비 "
                + std::to_string(viewCompleteDraws) + "/"
                + std::to_string(draws.size()));
        }
        if (!expectExperiment && 0 != viewCompleteDraws)
        {
            failures.push_back("뷰: off인데 완비 "
                + std::to_string(viewCompleteDraws));
        }
        // DrawSource가 재질 정본을 나르는가 — 컴포넌트 바인딩만 보면 프록시
        // 복사 누락에 눈멀다(D5-a의 M2 변이가 가른 그 축과 같은 자리).
        if (expectExperiment && authoredMaterialDraws != draws.size())
        {
            failures.push_back("재질 운반: "
                + std::to_string(authoredMaterialDraws) + "/"
                + std::to_string(draws.size()));
        }
        if (!expectExperiment && 0 != authoredMaterialDraws)
        {
            failures.push_back("재질 운반: off인데 "
                + std::to_string(authoredMaterialDraws));
        }

        if (failures.empty())
        {
            std::printf("[CLI] experiment.foliage verify pass mode=%s "
                "types=%zu bound=%zu draws=%zu views=%zu "
                "authoredMat=%zu authoredMatDraws=%zu\n",
                expectExperiment ? "experiment" : "legacy",
                types.size(), boundTypes, draws.size(), viewCompleteDraws,
                authoredMaterialTypes, authoredMaterialDraws);
        }
        else
        {
            std::string joined;
            for (const std::string& failure : failures)
                joined += " [" + failure + "]";
            std::printf("[CLI] experiment.foliage verify fail mode=%s%s\n",
                expectExperiment ? "experiment" : "legacy", joined.c_str());
        }
    }

    // I5-D4e-3 — 본 이름 해석 창구의 전수 A/B 대조. Scene 본 전파가 쓰는
    // 실물 창구(Animator::ResolveBoneIndex)를 BoneComponent 전수에 태우고,
    // legacy FindBone 직접 해석과 인덱스를 대조한다(1:1 계약 실증). 경로
    // 계수(viaExperiment)는 창구 내부의 실분기 관측이다 — 조건 재현이 아니라서
    // experiment 분기 소실이 legacy 폴백으로 조용히 덮여도 여기서 갈린다.
    static void Cmd_experiment_boneresolve(const ConsoleCommandContext& ctx)
    {
        Scene* scene = SceneManagers->GetActiveScene();
        if (nullptr == scene)
        {
            std::printf("[CLI] experiment.boneresolve fail 활성 씬 없음\n");
            return;
        }
        std::size_t boneCount = 0, viaExperimentCount = 0, viaLegacyCount = 0;
        std::size_t mismatchCount = 0, unresolvedCount = 0;
        // I6-B2 — 본 캐시 무효화 신원의 출처. 해석 경로(viaExperiment)와 별개
        // 축이다: 인덱스를 experiment로 풀면서 신원은 legacy 객체 수명에 묶여
        // 있으면 그 객체를 은퇴시킬 수 없다.
        std::size_t serialExperimentCount = 0, serialLegacyCount = 0;
        for (const auto& object : scene->m_Entities)
        {
            if (!object || object->IsDestroyMark()) continue;
            if (nullptr == object->GetComponent<BoneComponent>()) continue;
            const auto& rootObject = scene->TryGetEntity(object->GetRootIndex());
            if (!rootObject) continue;
            Animator* animator = rootObject->GetComponent<Animator>();
            bool serialViaExperiment = false;
            if (nullptr == animator
                || 0 == animator->GetSkeletonSerial(&serialViaExperiment))
            {
                continue;
            }

            const std::string boneName = object->RemoveSuffixNumberTag();
            bool viaExperiment = false;
            const int resolved =
                animator->ResolveBoneIndex(boneName, &viaExperiment);
            int legacyIndex = -1;
            if (animator->m_Skeleton)
            {
                Bone* const bone = animator->m_Skeleton->FindBone(boneName);
                legacyIndex = bone ? bone->m_index : -1;
            }

            ++boneCount;
            if (viaExperiment) ++viaExperimentCount; else ++viaLegacyCount;
            if (serialViaExperiment) ++serialExperimentCount;
            else ++serialLegacyCount;
            if (resolved != legacyIndex) ++mismatchCount;
            if (resolved < 0) ++unresolvedCount;
        }
        const bool passed = boneCount > 0 && 0 == mismatchCount
            && 0 == unresolvedCount;
        std::printf("[CLI] experiment.boneresolve %s bones=%zu experiment=%zu "
            "legacy=%zu mismatch=%zu unresolved=%zu serialExperiment=%zu "
            "serialLegacy=%zu\n",
            passed ? "pass" : (boneCount == 0 ? "skip" : "fail"),
            boneCount, viaExperimentCount, viaLegacyCount, mismatchCount,
            unresolvedCount, serialExperimentCount, serialLegacyCount);
    }

    // I5-D4e-3 — AvatarMask 트리 생성의 A/B 대조. 실물 창구
    // (BuildAvatarBoneMasks — experiment 단일 패스+스택 DFS)와 legacy
    // MakeBoneMask 재귀를 같은 스켈레톤에 돌려 m_BoneMasks의 크기·순서
    // (boneName 열)·자식 계수를 대조한다 — 순서가 저장분 인덱스 대응
    // (ReCreateMask)이라 순서 재현이 곧 저작 호환이다.
    static void Cmd_experiment_animmask(const ConsoleCommandContext& ctx)
    {
        Scene* scene = SceneManagers->GetActiveScene();
        if (nullptr == scene)
        {
            std::printf("[CLI] experiment.animmask fail 활성 씬 없음\n");
            return;
        }
        Animator* animator = nullptr;
        for (const auto& object : scene->m_Entities)
        {
            if (!object || object->IsDestroyMark()) continue;
            Animator* candidate = object->GetComponent<Animator>();
            if (nullptr == candidate || nullptr == candidate->m_Skeleton
                || nullptr == candidate->m_Skeleton->m_rootBone)
            {
                continue;
            }
            animator = candidate;
            break;
        }
        if (nullptr == animator)
        {
            std::printf("[CLI] experiment.animmask skip animators=0\n");
            return;
        }

        AvatarMask channelMask; // 실물 창구 산출
        bool viaExperiment = false;
        BoneMask* channelRoot =
            animator->BuildAvatarBoneMasks(channelMask, &viaExperiment);
        AvatarMask legacyMask;  // legacy 재귀 대조군
        BoneMask* legacyRoot =
            legacyMask.MakeBoneMask(animator->m_Skeleton->m_rootBone);

        std::vector<std::string> failures;
        if (nullptr == channelRoot || nullptr == legacyRoot)
        {
            failures.push_back("루트 마스크 생성 실패");
        }
        else
        {
            if (channelRoot->boneName != legacyRoot->boneName)
                failures.push_back("루트 이름 불일치");
            if (channelMask.m_BoneMasks.size() != legacyMask.m_BoneMasks.size())
            {
                failures.push_back("마스크 계수 불일치("
                    + std::to_string(channelMask.m_BoneMasks.size()) + " vs "
                    + std::to_string(legacyMask.m_BoneMasks.size()) + ")");
            }
            else
            {
                for (std::size_t index = 0;
                    index < channelMask.m_BoneMasks.size(); ++index)
                {
                    if (channelMask.m_BoneMasks[index]->boneName
                            != legacyMask.m_BoneMasks[index]->boneName
                        || channelMask.m_BoneMasks[index]->m_children.size()
                            != legacyMask.m_BoneMasks[index]->m_children.size())
                    {
                        failures.push_back("순서/자식 불일치 idx="
                            + std::to_string(index));
                        break;
                    }
                }
            }
        }

        if (failures.empty())
        {
            std::printf("[CLI] experiment.animmask pass masks=%zu "
                "viaExperiment=%d\n",
                channelMask.m_BoneMasks.size(), viaExperiment ? 1 : 0);
        }
        else
        {
            std::string joined;
            for (const std::string& failure : failures)
                joined += " [" + failure + "]";
            std::printf("[CLI] experiment.animmask fail viaExperiment=%d%s\n",
                viaExperiment ? 1 : 0, joined.c_str());
        }
    }

    // I5-D5b — 에디터 실소비 창구의 전수 A/B. 에디터 UI 자체는 헤드리스
    // 관측 밖이라(--script 라이브는 렌더 0프레임) UI를 재지 않고, UI가
    // 지나게 된 **창구 두 개**를 씬 전수에 태워 legacy 직소비와 대조한다.
    //
    //   clip 축  — Animator::GetClipCount/GetClipName vs
    //              m_Skeleton->m_animations(size·m_name). 개수만 재면 순서가
    //              뒤집혀도 초록이라 **이름을 인덱스별로** 맞춘다: 편집
    //              정본(D4e-2 오버라이드)이 인덱스 축이므로 순서가 어긋나면
    //              다른 클립을 편집하게 된다.
    //   mesh 축  — MeshRenderer::HasRenderableMesh vs (bool)m_Mesh. D4f
    //              이전에는 둘이 동치여야 한다(역브리지가 항상 짝을 만든다).
    //
    // 경로 계수(viaExperiment)는 창구 내부의 실분기 관측이다 — 조건 재현이
    // 아니라서 experiment 분기가 소실돼 legacy 폴백으로 조용히 덮여도 여기서
    // 갈린다.
    static void Cmd_experiment_editorsurface(const ConsoleCommandContext& ctx)
    {
        Scene* scene = SceneManagers->GetActiveScene();
        if (nullptr == scene)
        {
            std::printf("[CLI] experiment.editorsurface fail 활성 씬 없음\n");
            return;
        }

        std::size_t animators = 0, clipViaExperiment = 0, clipViaLegacy = 0;
        std::size_t clipsChecked = 0, clipCountMismatch = 0, clipNameMismatch = 0;
        std::size_t clipFrameMismatch = 0;
        std::size_t renderers = 0, meshViaExperiment = 0, meshViaLegacy = 0;
        std::size_t meshGuardMismatch = 0, meshPresent = 0;
        std::string firstMismatch;

        for (const auto& object : scene->m_Entities)
        {
            if (!object || object->IsDestroyMark()) continue;

            Animator* animator = object->GetComponent<Animator>();
            if (nullptr != animator && nullptr != animator->m_Skeleton)
            {
                ++animators;
                bool viaExperiment = false;
                const std::size_t windowCount =
                    animator->GetClipCount(&viaExperiment);
                if (viaExperiment) ++clipViaExperiment; else ++clipViaLegacy;

                const std::size_t legacyCount =
                    animator->m_Skeleton->m_animations.size();
                if (windowCount != legacyCount)
                {
                    ++clipCountMismatch;
                    if (firstMismatch.empty())
                    {
                        firstMismatch = "clipCount " +
                            std::to_string(windowCount) + "/" +
                            std::to_string(legacyCount);
                    }
                }
                const std::size_t common =
                    windowCount < legacyCount ? windowCount : legacyCount;
                for (std::size_t clip = 0; clip < common; ++clip)
                {
                    ++clipsChecked;
                    const std::string windowName =
                        animator->GetClipName(static_cast<int>(clip));
                    const std::string& legacyName =
                        animator->m_Skeleton->m_animations[clip].m_name;
                    if (windowName != legacyName)
                    {
                        ++clipNameMismatch;
                        if (firstMismatch.empty())
                        {
                            firstMismatch = "clipName[" +
                                std::to_string(clip) + "] " + windowName +
                                "/" + legacyName;
                        }
                    }
                    // frame 축 — 키프레임 수(유니크 키 시각). D5b 실측이 여기서
                    // 역브리지의 정의 어긋남(채널 키 합산)을 잡았다: 이벤트
                    // 저작의 frameKey 상한과 key(0~1 진행률) 환산이 로드
                    // 경로마다 달라져 같은 자산이 다른 시점에 발화했다.
                    const std::size_t windowFrames =
                        animator->GetClipFrameCount(static_cast<int>(clip));
                    const std::size_t legacyFrames =
                        animator->m_Skeleton->m_animations[clip].m_totalKeyFrames;
                    if (windowFrames != legacyFrames)
                    {
                        ++clipFrameMismatch;
                        if (firstMismatch.empty())
                        {
                            firstMismatch = "clipFrames[" +
                                std::to_string(clip) + "] " +
                                std::to_string(windowFrames) + "/" +
                                std::to_string(legacyFrames);
                        }
                    }
                }
            }

            MeshRenderer* renderer = object->GetComponent<MeshRenderer>();
            if (nullptr != renderer)
            {
                ++renderers;
                bool viaExperiment = false;
                const bool windowHas =
                    renderer->HasRenderableMesh(&viaExperiment);
                const bool legacyHas = static_cast<bool>(renderer->m_Mesh);
                if (viaExperiment) ++meshViaExperiment; else ++meshViaLegacy;
                if (windowHas) ++meshPresent;
                if (windowHas != legacyHas)
                {
                    ++meshGuardMismatch;
                    if (firstMismatch.empty())
                    {
                        firstMismatch = std::string("meshGuard ") +
                            (windowHas ? "1/0" : "0/1") + " " +
                            object->m_name.ToString();
                    }
                }
            }
        }

        const bool covered = animators > 0 && renderers > 0 && clipsChecked > 0;
        const bool passed = covered && 0 == clipCountMismatch
            && 0 == clipNameMismatch && 0 == clipFrameMismatch
            && 0 == meshGuardMismatch;
        std::printf("[CLI] experiment.editorsurface %s animators=%zu "
            "clipExperiment=%zu clipLegacy=%zu clips=%zu countMismatch=%zu "
            "nameMismatch=%zu frameMismatch=%zu renderers=%zu "
            "meshExperiment=%zu meshLegacy=%zu "
            "meshPresent=%zu guardMismatch=%zu%s%s\n",
            passed ? "pass" : (covered ? "fail" : "skip"),
            animators, clipViaExperiment, clipViaLegacy, clipsChecked,
            clipCountMismatch, clipNameMismatch, clipFrameMismatch,
            renderers, meshViaExperiment,
            meshViaLegacy, meshPresent, meshGuardMismatch,
            firstMismatch.empty() ? "" : " first=",
            firstMismatch.c_str());
    }

    // I5-D4f-1 — 바운드 축. 역브리지가 legacy 정점 시공을 그만두면
    // RecalculateBounds()가 원본을 잃는다: 바운드는 기본값(빈 AABB·반지름 0)으로
    // 남고 컬링·피킹·그림자 반경이 조용히 틀어진다. D4f-0 예행이 이 눈멂을
    // 실증했다 — legacy 정점 없이도 드로우 9·커버리지 42411이 그대로였다.
    // 지금 게이트의 어느 축도 바운드를 한 번도 읽지 않는다.
    //
    // 셋을 잰다:
    //   ① 절단이 실제로 일어났는가 — legacyVertices (on 0 · off 전량)
    //   ② 바운드가 살아 있는가   — degenerate 0
    //   ③ 두 유도가 같은 값인가  — digest를 ps1이 on/off로 대조한다.
    // ③이 실질 이빨이다. on은 experiment 정본 주입, off는 legacy 정점→min/max
    // 유도라 서로 다른 산출 경로이고, digest가 갈리면 주입이 틀린 것이다.
    // ①②는 각 실행 안에서만 성립한다 — ①이 없으면 절단이 안 일어나도 전부
    // 초록이고(무변경 슬라이스), ②가 없으면 바운드 소실이 조용히 통과한다.
    static void Cmd_experiment_meshbounds(const ConsoleCommandContext& ctx)
    {
        Scene* scene = SceneManagers->GetActiveScene();
        if (nullptr == scene)
        {
            std::printf("[CLI] experiment.meshbounds fail 활성 씬 없음\n");
            return;
        }

        std::size_t meshes = 0, legacyVertices = 0, degenerate = 0;
        std::size_t expBound = 0, boundMismatch = 0;
        std::string firstIssue;
        std::uint32_t digest = 2166136261u;

        const auto account = [&](const std::string& label, Mesh& mesh,
            const experiment::Model* model, std::uint32_t meshIndex)
        {
            ++meshes;
            // 비-const 참조로 부른다 — const 오버로드는 배열을 값으로 복사한다.
            if (!mesh.GetVertices().empty()) ++legacyVertices;

            const math::aabb box = mesh.GetBoundingBox();
            const math::sphere ball = mesh.GetBoundingSphere();
            if (box.is_empty() || !(ball.radius > 0.0f))
            {
                ++degenerate;
                if (firstIssue.empty()) firstIssue = "degenerate:" + label;
            }

            char line[320];
            std::snprintf(line, sizeof(line),
                "%s|%.6f,%.6f,%.6f|%.6f,%.6f,%.6f|%.6f",
                label.c_str(), box.center.x, box.center.y, box.center.z,
                box.extents.x, box.extents.y, box.extents.z, ball.radius);
            for (const char* cursor = line; '\0' != *cursor; ++cursor)
            {
                digest ^= static_cast<unsigned char>(*cursor);
                digest *= 16777619u;
            }

            if (nullptr == model) return;
            const experiment::Mesh* source =
                model->TryGetMesh(experiment::MeshIndex{ meshIndex });
            if (nullptr == source) return;
            ++expBound;
            if (!math::near_equal(box, source->bounds))
            {
                ++boundMismatch;
                if (firstIssue.empty()) firstIssue = "mismatch:" + label;
            }
        };

        for (const auto& object : scene->m_Entities)
        {
            if (!object || object->IsDestroyMark()) continue;

            MeshRenderer* renderer = object->GetComponent<MeshRenderer>();
            if (nullptr != renderer && nullptr != renderer->m_Mesh)
            {
                account(object->m_name.ToString() + "/" +
                    renderer->m_Mesh->GetName(), *renderer->m_Mesh,
                    renderer->m_experimentModel.get(),
                    renderer->m_experimentMeshIndex);
            }

            // Foliage 타입 메시도 같은 브리지 산물이다. 여기 바운드는 인스턴스
            // 컬링(PrimitiveRenderProxy의 뷰별 transformed AABB)이 읽는데,
            // 빈 AABB는 "컬링 안 함"으로 흘러 드로우 계수를 바꾸지 않는다 —
            // 드로우 동수 축이 원리적으로 못 보는 자리라 여기서 잰다.
            FoliageComponent* foliage = object->GetComponent<FoliageComponent>();
            if (nullptr != foliage)
            {
                std::size_t typeIndex = 0;
                for (const FoliageType& type : foliage->GetFoliageTypes())
                {
                    if (type.m_mesh)
                    {
                        account(object->m_name.ToString() + "/foliage" +
                            std::to_string(typeIndex) + "/" +
                            type.m_mesh->GetName(), *type.m_mesh,
                            type.m_experimentModel.get(),
                            type.m_experimentMeshIndex);
                    }
                    ++typeIndex;
                }
            }
        }

        const bool covered = meshes > 0;
        const bool passed = covered && 0 == degenerate && 0 == boundMismatch;
        std::printf("[CLI] experiment.meshbounds %s meshes=%zu "
            "legacyVertices=%zu degenerate=%zu expBound=%zu mismatch=%zu "
            "digest=%08X%s%s\n",
            passed ? "pass" : (covered ? "fail" : "skip"),
            meshes, legacyVertices, degenerate, expBound, boundMismatch,
            digest,
            firstIssue.empty() ? "" : " first=", firstIssue.c_str());
    }

    // I5-D4e-1 — 재생 팔레트 패리티. 활성 씬의 experiment 핸들 보유 Animator
    // 전수에 대해, 같은 시각 입력으로 legacy 재귀(UpdateBone)와 experiment
    // 단일 순회를 제품 함수 그대로 돌려 m_FinalTransforms를 원소 단위 대조한다.
    // 라이브 틱은 실시간이라 비결정 — 이 명령이 결정적 표본(클립 구간 5점)으로
    // 같은 산술을 잰다.
    //
    // 두 축으로 잰다(실측 근거 — Gunner에서 Step 채널 369개, maxErr 0.0068):
    //   linear 축(단정): Step→Linear 강등 사본으로 대조 — legacy는 역브리지가
    //     보간 모드를 버려 항상 Linear를 재생하므로, 이 축이 "산술 재현"의
    //     이빨이다.
    //   step 축(관측): 원본(Step 집행)과의 격차 — 임포터가 보존한 자산 의도를
    //     experiment 경로가 집행해서 생기는 **의도된 격차**라 단정하지 않고
    //     크기만 보고한다(legacy Linear 강등이 알려진 손실이다).
    struct AnimtickAxisResult final
    {
        std::size_t clipCount{};
        std::size_t sampleCount{};
        std::size_t failedEvaluations{};
        double maxError{};
        std::size_t worstClip{}, worstBone{};
        float worstFraction{};
        std::string worstBoneName{};
    };

    static void MeasureAnimtickAxis(AnimationJob& job, Animator& animator,
        const experiment::Skeleton& skeleton, AnimtickAxisResult& result)
    {
        static constexpr float kSampleFractions[]{ 0.f, 0.25f, 0.5f, 0.75f, 0.95f };
        std::vector<math::matrix4x4> legacyPose(MAX_BONES);
        std::vector<math::matrix4x4> experimentPose(MAX_BONES);
        for (std::size_t clip = 0; clip < skeleton.clips.size(); ++clip)
        {
            ++result.clipCount;
            const float duration =
                static_cast<float>(skeleton.clips[clip].durationTicks);
            for (const float fraction : kSampleFractions)
            {
                if (!job.EvaluateParityPose(animator, skeleton,
                    static_cast<int>(clip), duration * fraction,
                    legacyPose.data(), experimentPose.data()))
                {
                    ++result.failedEvaluations;
                    continue;
                }
                ++result.sampleCount;
                for (std::size_t bone = 0; bone < MAX_BONES; ++bone)
                {
                    const float* legacyValues = &legacyPose[bone].m[0][0];
                    const float* experimentValues =
                        &experimentPose[bone].m[0][0];
                    for (int element = 0; element < 16; ++element)
                    {
                        const double error = static_cast<double>(std::abs(
                            legacyValues[element] - experimentValues[element]));
                        if (error > result.maxError)
                        {
                            result.maxError = error;
                            result.worstClip = clip;
                            result.worstBone = bone;
                            result.worstFraction = fraction;
                            result.worstBoneName =
                                bone < skeleton.bones.size()
                                ? skeleton.bones[bone].name : "?";
                        }
                    }
                }
            }
        }
    }

    static void Cmd_experiment_animtick(const ConsoleCommandContext& ctx)
    {
        Scene* scene = SceneManagers->GetActiveScene();
        RenderScene* renderScene = SceneManagers->GetRenderScene();
        if (nullptr == scene || nullptr == renderScene)
        {
            std::printf("[CLI] experiment.animtick fail 활성 씬/렌더 씬 없음\n");
            return;
        }
        AnimationJob& job = renderScene->GetAnimationJob();

        std::size_t animatorCount = 0;
        std::size_t stepChannels = 0;
        AnimtickAxisResult linearAxis{};
        AnimtickAxisResult stepAxis{};

        for (const auto& object : scene->m_Entities)
        {
            if (!object || object->IsDestroyMark()) continue;
            Animator* animator = object->GetComponent<Animator>();
            if (nullptr == animator || nullptr == animator->m_experimentModel)
                continue;
            const experiment::Skeleton* skeleton =
                animator->m_experimentModel->TryGetSkeleton();
            if (nullptr == skeleton) continue;

            ++animatorCount;

            // linear 축 사본 — 제품 함수를 오염시키지 않고 legacy의 Linear
            // 강등을 재현한다(게이트 전용 복사 비용).
            experiment::Skeleton linearSkeleton = *skeleton;
            for (experiment::AnimationClip& clip : linearSkeleton.clips)
            {
                for (experiment::AnimationChannel& channel : clip.channels)
                {
                    if (channel.translationInterpolation
                            == experiment::InterpolationMode::Step
                        || channel.rotationInterpolation
                            == experiment::InterpolationMode::Step
                        || channel.scaleInterpolation
                            == experiment::InterpolationMode::Step)
                    {
                        ++stepChannels;
                    }
                    channel.translationInterpolation =
                        experiment::InterpolationMode::Linear;
                    channel.rotationInterpolation =
                        experiment::InterpolationMode::Linear;
                    channel.scaleInterpolation =
                        experiment::InterpolationMode::Linear;
                }
            }

            MeasureAnimtickAxis(job, *animator, linearSkeleton, linearAxis);
            MeasureAnimtickAxis(job, *animator, *skeleton, stepAxis);
        }

        // 오차 한계 0 — 같은 산술의 재현이라 비트 동일이 실측이다(경계 규약
        // 정정 후 Gunner 10클립 50표본에서 maxErr 0.000000000). 무손실 대조라
        // 근사 허용이 없다(V2 픽셀 diff 0과 같은 결).
        const bool passed = animatorCount > 0 && linearAxis.sampleCount > 0
            && 0 == linearAxis.failedEvaluations && 0.0 == linearAxis.maxError;
        if (linearAxis.maxError > 0.0)
        {
            std::printf("[CLI] experiment.animtick linear worst: clip=%zu "
                "bone=%zu(%s) fraction=%.2f\n",
                linearAxis.worstClip, linearAxis.worstBone,
                linearAxis.worstBoneName.c_str(), linearAxis.worstFraction);
        }
        std::printf("[CLI] experiment.animtick step-divergence: maxErr=%.9f "
            "stepChannels=%zu worstBone=%s (의도된 격차 — 단정 없음)\n",
            stepAxis.maxError, stepChannels, stepAxis.worstBoneName.c_str());
        std::printf("[CLI] experiment.animtick %s animators=%zu clips=%zu "
            "samples=%zu failedEval=%zu maxErr=%.9f\n",
            passed ? "pass" : (animatorCount == 0 ? "skip" : "fail"),
            animatorCount, linearAxis.clipCount, linearAxis.sampleCount,
            linearAxis.failedEvaluations, linearAxis.maxError);
    }

    static void Cmd_script_add(const ConsoleCommandContext& ctx)
    {
        const std::vector<std::string>& parts = ctx.parts;
        const std::string& line = ctx.line;
        const std::string& cmd = ctx.cmd;

        if (parts.size() < 3)
        {
            std::printf("[CLI] 사용법: script.add <오브젝트 이름> <스크립트 타입>\n");
            return;
        }

        Scene* scene = SceneManagers->GetActiveScene();
        if (!scene) { std::printf("[CLI] 활성 씬 없음\n"); return; }

        // 오브젝트 이름에 공백이 흔하다("Main Camera"). 타입은 마지막 토큰으로 보고,
        // 그 앞 전체를 이름으로 취급한다.
        const std::string typeName = parts.back();
        std::string rest = TrimLine(line.substr(cmd.size()));
        const std::string objectName = TrimLine(rest.substr(0, rest.rfind(typeName)));

        auto object = scene->GetEntity(objectName);
        if (!object)
        {
            Debug->LogError("[스크립트] 오브젝트를 찾을 수 없음: " + objectName);
            std::printf("[CLI] 오브젝트를 찾을 수 없음: %s\n", objectName.c_str());
            return;
        }

        // 한 오브젝트에 스크립트를 여럿 붙일 수 있어야 하므로 중복 허용 경로를 쓴다.
        // ScriptComponent가 Awake에서 관리 인스턴스를 만든다.
        const Meta::Type* scriptType = Meta::Find("ScriptComponent");
        if (nullptr == scriptType)
        {
            std::printf("[CLI] ScriptComponent 타입을 찾을 수 없음\n");
            return;
        }

        // K2 스테이지 A: AddComponentAllowMultiple가 raw Component*를 돌려준다 —
        // dynamic_pointer_cast(shared_ptr 전용) 대신 dynamic_cast.
        auto* script = dynamic_cast<ScriptComponent*>(
            object->AddComponentAllowMultiple(*scriptType));
        if (!script)
        {
            std::printf("[CLI] ScriptComponent 추가 실패\n");
            return;
        }

        // m_scriptType은 드레인보다 먼저 세워야 한다 — OnInitialized가 이 값을 보고
        // CreateBehaviour를 부른다(비어 있으면 그냥 돌아간다. ScriptComponent.cpp).
        script->m_scriptType = typeName;

        // (C2-2) 예전에는 여기서 script->OnInitialized()를 직접 불렀다("씬의 초기화
        // 단계는 이미 지나갔을 수 있으므로 여기서 직접 깨운다"). 하지만
        // AddComponentAllowMultiple 안의 AttachComponentLifecycle이 이미 이 컴포넌트를
        // PendingAwake 큐에 넣어 뒀고(State_AwakeCalled 비트는 아직 서지 않은 채),
        // 직접 부르면 그 비트를 세우지 않으므로 다음 프레임 Scene::RegistryDrainAwakeAndStart가
        // 큐에 남은 같은 컴포넌트를 또 한 번 깨운다 — OnInitialized 이중 호출.
        // ScriptComponent::OnInitialized의 `if (HasInstance()) return;` 가드가 보통은
        // 이걸 조용히 삼키지만, 그건 설계가 아니라 우연이다.
        //
        // Api_Prefab_Instantiate(ClrHost.cpp)가 쓰는 것과 같은 관용구로 고친다 — 부착
        // 직후 scene->DrainPendingLifecycle()을 동기로 불러 정상 드레인 경로를 태운다.
        // 이미 깨운 컴포넌트는 State_AwakeCalled로 건너뛰므로 씬 전체를 다시 돌아도 안전하다.
        scene->DrainPendingLifecycle();

        if (!script->HasInstance())
        {
            Debug->LogError("[스크립트] 부착 실패 — 타입=" + typeName);
            std::printf("[CLI] 스크립트 부착 실패 (타입=%s)\n", typeName.c_str());
            return;
        }

        const int id = script->GetInstanceId();
        Debug->LogWarning("[스크립트] " + objectName + " 에 " + typeName + " 부착 (id=" + std::to_string(id) + ")");
        std::printf("[CLI] 부착 완료: %s <- %s (id=%d)\n", objectName.c_str(), typeName.c_str(), id);
    }

    static void Cmd_scene_select(const ConsoleCommandContext& ctx)
    {
        const std::vector<std::string>& parts = ctx.parts;
        const std::string& line = ctx.line;
        const std::string& cmd = ctx.cmd;

        if (parts.size() < 2)
        {
            std::printf("[CLI] 사용법: scene.select <오브젝트 이름>\n");
            return;
        }

        Scene* scene = SceneManagers->GetActiveScene();
        if (!scene) { std::printf("[CLI] 활성 씬 없음\n"); return; }

        const std::string objectName = TrimLine(line.substr(cmd.size()));
        auto object = scene->GetEntity(objectName);
        if (!object)
        {
            std::printf("[CLI] 오브젝트를 찾을 수 없음: %s\n", objectName.c_str());
            return;
        }

        // 인스펙터가 보는 선택 상태를 그대로 바꾼다(에디터에서 클릭한 것과 같은 효과).
		scene->m_selectedEntity = object;
        Debug->LogWarning("[CLI] 선택: " + objectName);
        std::printf("[CLI] 선택: %s\n", objectName.c_str());
    }

    static void Cmd_script_fields(const ConsoleCommandContext& ctx)
    {
        const std::vector<std::string>& parts = ctx.parts;

        if (parts.size() < 2)
        {
            std::printf("[CLI] 사용법: script.fields <인스턴스 id>\n");
            return;
        }

        auto& clr = ClrHost::Get();
        const int id = std::atoi(parts[1].c_str());
        const int count = clr.GetFieldCount(id);

        Debug->LogWarning("[스크립트] 인스턴스 " + parts[1] + " 노출 필드 " + std::to_string(count) + "개");

        for (int i = 0; i < count; ++i)
        {
            const std::string name = clr.GetFieldName(id, i);
            const auto type = clr.GetFieldType(id, i);

            std::string value;
            switch (type)
            {
            case ClrHost::ScriptFieldType::Float:
                value = "float " + std::to_string(clr.GetFieldFloat(id, i));
                break;
            case ClrHost::ScriptFieldType::Int32:
                value = "int " + std::to_string(clr.GetFieldInt32(id, i));
                break;
            case ClrHost::ScriptFieldType::Bool:
                value = std::string("bool ") + (clr.GetFieldBool(id, i) ? "true" : "false");
                break;
            case ClrHost::ScriptFieldType::String:
                value = "string \"" + clr.GetFieldString(id, i) + "\"";
                break;
            case ClrHost::ScriptFieldType::Float2:
            {
                const auto v = clr.GetFieldFloat2(id, i);
                value = "float2 (" + std::to_string(v.x) + ", " + std::to_string(v.y) + ")";
                break;
            }
            case ClrHost::ScriptFieldType::Float3:
            {
                const auto v = clr.GetFieldFloat3(id, i);
                value = "float3 (" + std::to_string(v.x) + ", " + std::to_string(v.y) + ", " + std::to_string(v.z) + ")";
                break;
            }
            case ClrHost::ScriptFieldType::Object:
            {
                Entity* target = clr.GetFieldObject(id, i);
                value = std::string("object ") + (nullptr != target ? target->m_name.ToString() : "(없음)");
                break;
            }
            default:
                value = "(미지원 타입)";
                break;
            }

            Debug->LogWarning("[스크립트]   [" + std::to_string(i) + "] " + name + " = " + value);
        }
        std::printf("[CLI] 필드 %d개 기록\n", count);
    }

    static void Cmd_script_set(const ConsoleCommandContext& ctx)
    {
        const std::vector<std::string>& parts = ctx.parts;
        const std::string& line = ctx.line;
        const std::string& cmd = ctx.cmd;

        if (parts.size() < 4)
        {
            std::printf("[CLI] 사용법: script.set <인스턴스 id> <필드 인덱스> <값>\n");
            return;
        }

        auto& clr = ClrHost::Get();
        const int id = std::atoi(parts[1].c_str());
        const int index = std::atoi(parts[2].c_str());

        // 값에 공백이 들어갈 수 있다(문자열·오브젝트 이름). 인덱스 뒤 전체를 값으로 본다.
        std::string rest = TrimLine(line.substr(cmd.size()));
        rest = TrimLine(rest.substr(parts[1].size()));
        const std::string rawValue = TrimLine(rest.substr(parts[2].size()));

        switch (clr.GetFieldType(id, index))
        {
        case ClrHost::ScriptFieldType::Float:
            clr.SetFieldFloat(id, index, static_cast<float>(std::atof(rawValue.c_str())));
            break;
        case ClrHost::ScriptFieldType::Int32:
            clr.SetFieldInt32(id, index, std::atoi(rawValue.c_str()));
            break;
        case ClrHost::ScriptFieldType::Bool:
            clr.SetFieldBool(id, index, rawValue == "true" || rawValue == "1");
            break;
        case ClrHost::ScriptFieldType::String:
            clr.SetFieldString(id, index, rawValue);
            break;
        case ClrHost::ScriptFieldType::Float2:
        {
            ClrHost::ScriptFloat2 v{};
            sscanf_s(rawValue.c_str(), "%f,%f", &v.x, &v.y);
            clr.SetFieldFloat2(id, index, v);
            break;
        }
        case ClrHost::ScriptFieldType::Float3:
        {
            ClrHost::ScriptFloat3 v{};
            sscanf_s(rawValue.c_str(), "%f,%f,%f", &v.x, &v.y, &v.z);
            clr.SetFieldFloat3(id, index, v);
            break;
        }
        case ClrHost::ScriptFieldType::Object:
        {
            // 오브젝트 참조는 이름으로 지정한다("none"이면 비운다).
            Entity* target = nullptr;
            if (rawValue != "none" && !rawValue.empty())
            {
                if (Scene* activeScene = SceneManagers->GetActiveScene())
                {
                    auto found = activeScene->GetEntity(rawValue);
					target = found;
                }

                if (nullptr == target)
                {
                    std::printf("[CLI] 오브젝트를 찾을 수 없음: %s\n", rawValue.c_str());
                    return;
                }
            }
            clr.SetFieldObject(id, index, target);
            break;
        }
        default:
            std::printf("[CLI] 설정할 수 없는 필드입니다\n");
            return;
        }

        // 인스펙터 편집과 같은 취급 — 바뀐 값을 컴포넌트에 담아 직렬화 대상으로 만든다.
        if (Scene* scene = SceneManagers->GetActiveScene())
        {
            for (const auto& object : scene->m_Entities)
            {
                if (!object) continue;

                auto script = object->GetComponent<ScriptComponent>();
                if (nullptr != script && script->GetInstanceId() == id)
                {
                    script->CaptureFields();
                    break;
                }
            }
        }

        Debug->LogWarning("[스크립트] 필드 설정 — id=" + parts[1] + " [" + parts[2] + "] = " + parts[3]);
        std::printf("[CLI] 필드 설정 완료\n");
    }

    static void Cmd_component_add(const ConsoleCommandContext& ctx)
    {
        const std::vector<std::string>& parts = ctx.parts;
        const std::string& line = ctx.line;
        const std::string& cmd = ctx.cmd;

        if (parts.size() < 3)
        {
            std::printf("[CLI] 사용법: component.add <오브젝트 이름> <컴포넌트 타입>\n");
            return;
        }

        Scene* scene = SceneManagers->GetActiveScene();
        if (!scene) { std::printf("[CLI] 활성 씬 없음\n"); return; }

        // 오브젝트 이름에 공백이 흔하므로 컴포넌트 타입을 마지막 토큰으로 본다.
        const std::string typeName = parts.back();
        std::string rest = TrimLine(line.substr(cmd.size()));
        const std::string objectName = TrimLine(rest.substr(0, rest.rfind(typeName)));

        auto object = scene->GetEntity(objectName);
        if (!object)
        {
            std::printf("[CLI] 오브젝트를 찾을 수 없음: %s\n", objectName.c_str());
            return;
        }

        // 에디터의 Add Component 메뉴와 같은 경로.
        auto found = ComponentFactorys->m_componentTypes.find(typeName);
        if (found == ComponentFactorys->m_componentTypes.end() || nullptr == found->second)
        {
            std::printf("[CLI] 알 수 없는 컴포넌트 타입: %s\n", typeName.c_str());
            return;
        }

        auto component = object->AddComponent(*found->second);
        if (!component)
        {
            std::printf("[CLI] 컴포넌트 추가 실패\n");
            return;
        }

        if (auto* initializable = dynamic_cast<System::IInitializable*>(component))
        {
            initializable->Initialize();
        }

        Debug->LogWarning("[CLI] 컴포넌트 추가: " + objectName + " <- " + typeName);
        std::printf("[CLI] 컴포넌트 추가: %s <- %s\n", objectName.c_str(), typeName.c_str());
    }

    static void Cmd_prefab_instantiate(const ConsoleCommandContext& ctx)
    {
        const std::vector<std::string>& parts = ctx.parts;

        // 실제 게임 콘텐츠(프리팹)를 씬에 소환한다. UI 프리팹의 지연 연결 검증에
        // 필요해 추가했지만, 콘텐츠가 걸린 회귀라면 어디든 쓸 수 있다.
        if (parts.size() < 2)
        {
            std::printf("[CLI] 사용법: prefab.instantiate <프리팹 이름> [인스턴스 이름]\n");
            return;
        }

        Prefab* prefab = PrefabUtilitys->LoadPrefab(parts[1]);
        if (nullptr == prefab)
        {
            Debug->LogError("[CLI] 프리팹을 찾을 수 없음: " + parts[1]);
            std::printf("[CLI] 프리팹을 찾을 수 없음: %s\n", parts[1].c_str());
            return;
        }

        const std::string instanceName = (parts.size() > 2) ? parts[2] : parts[1];
        Entity* instance = PrefabUtilitys->InstantiatePrefab(prefab, instanceName);
        if (nullptr == instance)
        {
            std::printf("[CLI] 인스턴스 생성 실패: %s\n", parts[1].c_str());
            return;
        }

        Debug->LogWarning("[CLI] 프리팹 소환: " + parts[1] + " -> " + instanceName);
        std::printf("[CLI] 프리팹 소환: %s -> %s (index=%d)\n",
            parts[1].c_str(), instanceName.c_str(), static_cast<int>(instance->m_index));
    }

    static void Cmd_prefab_overrides(const ConsoleCommandContext& ctx)
    {
        const std::vector<std::string>& parts = ctx.parts;
        const std::string& line = ctx.line;
        const std::string& cmd = ctx.cmd;

        // prefab.overrides <오브젝트>
        //
        // 프리팹 인스턴스에 기록된 로컬 수정(m_prefabOverrides)을 그대로 덤프한다.
        //
        // ── 이 명령이 왜 먼저 서는가 (P-write S0) ──
        //
        // 지금 이 목록은 **항상 비어 있다.** 채우는 지점이 SeedOverridesFromSnapshot
        // 하나뿐이고, 그 기준인 m_prefabOriginal이 reflect()에 없어 비직렬화라
        // 씬을 다시 열면 근거가 사라진다(PrefabUtility.cpp:62-67의 자백 참고).
        // 그래서 프리팹을 갱신하면 인스턴스의 로컬 수정이 에러도 로그도 없이 덮인다.
        //
        // 기록 배선을 넣기 **전에** 이 명령을 먼저 세우는 이유는, 그래야 게이트가
        // "0이던 것이 1이 됐다"는 전환 자체를 증명할 수 있기 때문이다. 배선을 먼저
        // 넣으면 게이트가 처음부터 통과해, 그 게이트가 무엇을 재는지 알 수 없다.
        if (parts.size() < 2)
        {
            std::printf("[CLI] 사용법: prefab.overrides <오브젝트>\n");
            return;
        }

        Scene* scene = SceneManagers->GetActiveScene();
        if (!scene) { std::printf("[CLI] 활성 씬 없음\n"); return; }

        const std::string objectName = TrimLine(line.substr(cmd.size()));
        auto object = scene->GetEntity(objectName);
        if (!object)
        {
            Debug->LogError("[CLI] 오브젝트를 찾을 수 없음: " + objectName);
            std::printf("[CLI] 오브젝트를 찾을 수 없음: %s\n", objectName.c_str());
            return;
        }

        static const FileGuid nullGuid{};
        const bool isInstance = (object->m_prefabFileGuid != nullGuid);

        std::printf("[prefab.overrides] %s · 인스턴스=%s · 기록 %zu건\n",
            objectName.c_str(),
            isInstance ? "예" : "아니오",
            object->m_prefabOverrides.size());

        for (const auto& ov : object->m_prefabOverrides)
        {
            // 컴포넌트 타입이 비면 Entity 자신의 프로퍼티다.
            // 순번(-1 = 타입 전체, 순번 필드가 없던 시절 데이터)도 함께 찍는다 —
            // 같은 타입 컴포넌트가 여럿일 때 어느 것에 걸린 기록인지 눈으로 갈라야 한다.
            char slot[16]{};
            std::snprintf(slot, sizeof(slot), "#%d", ov.m_componentSlot);
            std::printf("[prefab.overrides]   %s%s.%s = %s\n",
                ov.m_componentType.empty() ? "(Entity)" : ov.m_componentType.c_str(),
                ov.m_componentType.empty() ? "" : slot,
                ov.m_propertyName.c_str(),
                ov.m_valueYaml.c_str());
        }
    }

    static void Cmd_prefab_update(const ConsoleCommandContext& ctx)
    {
        const std::vector<std::string>& parts = ctx.parts;
        const std::string& line = ctx.line;
        const std::string& cmd = ctx.cmd;

        // prefab.update <소스 오브젝트> <프리팹 이름>
        //
        // 소스 오브젝트의 현재 형상으로 프리팹 정의를 다시 뜨고, 그 정의를
        // 등록된 인스턴스 전체에 적용한다(PrefabUtility::UpdateInstances).
        //
        // ★ 불변식: UpdateInstances에는 **반드시 CreatePrefab의 산출물만** 넘긴다.
        // LoadPrefab이 돌려주는 노드를 직접 넘기면 형상이 Sequence로 와서
        // UpdateInstances가 Map으로 전제하고 읽는 자리와 어긋난다. 지금까지 이
        // 경로는 PrefabEditor::Close(CreatePrefab 산출물만 넘긴다)에서만 도달했고,
        // 이 명령이 CLI에서 처음으로 도달 가능하게 만든다 — 그래서 여기에 못박는다.
        if (parts.size() < 3)
        {
            std::printf("[CLI] 사용법: prefab.update <소스 오브젝트> <프리팹 이름>\n");
            return;
        }

        Scene* scene = SceneManagers->GetActiveScene();
        if (!scene) { std::printf("[CLI] 활성 씬 없음\n"); return; }

        // 이름 규칙은 prefab.create/object.rename과 같다 — 마지막 토큰이 프리팹
        // 이름이고 그 앞 전체가 오브젝트 이름이다(엔진이 공백 있는 이름을 만든다).
        const std::string prefabName = parts.back();
        std::string rest = TrimLine(line.substr(cmd.size()));
        const std::string objectName = TrimLine(rest.substr(0, rest.rfind(prefabName)));

        auto source = scene->GetEntity(objectName);
        if (!source)
        {
            Debug->LogError("[CLI] 소스 오브젝트를 찾을 수 없음: " + objectName);
            std::printf("[CLI] 소스 오브젝트를 찾을 수 없음: %s\n", objectName.c_str());
            return;
        }

        // ★ 정체성 승계. CreatePrefab 산출물은 FileGuid가 널이고(Prefab.cpp:25),
        // UpdateInstances는 m_instanceMap[prefab->GetFileGuid()]로 대상을 찾는다
        // (PrefabUtility.cpp:378). 승계하지 않으면 널 키로 조회해 **아무 인스턴스도
        // 찾지 못하고 조용히 0건 적용**된다 — 게이트가 아무것도 재지 못하는 모습으로
        // 나타난다. PrefabEditor::Close(:43)가 같은 이유로 같은 일을 한다.
        Prefab* existing = PrefabUtilitys->LoadPrefab(prefabName);
        if (!existing)
        {
            std::printf("[CLI] 기존 프리팹을 찾을 수 없음(먼저 prefab.create): %s\n", prefabName.c_str());
            return;
        }
        const FileGuid identity = existing->GetFileGuid();

        // ★ 널 identity로는 진행하지 않는다 (2026-08-30).
        //
        // 그대로 두면 SavePrefab이 널을 보고 CreateRandomV4()로 **새 GUID를
        // 발급**하고, 아래 UpdateInstances가 그 새 키로 m_instanceMap을 조회해
        // 아무 인스턴스도 못 찾은 채 조용히 0건 적용한다 — 에러도 로그도 없이
        // 인스턴스만 옛 값으로 남고, 게다가 sidecar가 새 GUID로 덮여 살아 있는
        // 인스턴스 전부가 프리팹에서 영구히 떨어져 나간다. 되돌릴 수 없는 손상을
        // 조용히 저지르느니 여기서 멈추는 편이 낫다.
        //
        // 여기 도달하는 유일한 길은 catalog가 그 순간 항목을 잃는 것이었고, 그
        // 근본(워처의 게시 Delete 오독)과 LoadPrefab의 무조건 덮어쓰기를 함께
        // 고쳤다. 이 가드는 세 번째 방어선이다 — 어느 경로로 널이 오든 막는다.
        if (identity == nullFileGuid)
        {
            Debug->LogError("[CLI] 프리팹 identity를 확인할 수 없어 갱신을 중단한다: " + prefabName);
            std::printf("[CLI] 프리팹 identity 없음(catalog 항목 부재) — 갱신 중단: %s\n",
                prefabName.c_str());
            return;
        }

		Prefab* prefab = PrefabUtilitys->CreatePrefab(source, prefabName);
        if (!prefab)
        {
            std::printf("[CLI] 프리팹 정의 생성 실패: %s\n", prefabName.c_str());
            return;
        }
        prefab->SetFileGuid(identity);

        // ★ 정의를 영속화한다 (P4-b에서 추가). 이 명령은 원래 인스턴스만 갱신하고
        // **정의는 메모리의 임시 객체에만** 두었다 — 디스크도 캐시도 그대로였다.
        // 그래서 이후 그 프리팹을 다시 로드하면 갱신 전 정의가 돌아왔다.
        //
        // P4-b 이전에는 이것이 드러나지 않았다. 아무도 정의를 다시 읽지 않았기
        // 때문이다(인스턴스는 UpdateInstances가 이미 고쳐 놓았다). 중첩 참조 노드가
        // **소환 시점에 정의를 다시 읽게** 되면서 비로소 보였다 — 잠재해 있던
        // 결함을 P4-b가 드러낸 것이지 새로 만든 것이 아니다.
        //
        // 경로 규칙은 prefab.create와 같아야 한다(같은 파일을 가리켜야 하므로).
        const file::path savePath = PathFinder::Relative("Prefabs\\") / (prefabName + ".prefab");
        if (!PrefabUtilitys->SavePrefab(prefab, savePath.string()))
        {
            std::printf("[CLI] 프리팹 정의 저장 실패: %s\n", savePath.string().c_str());
            return;
        }

        // ★ 등록 수와 **실제 적용 수**를 따로 찍는다 (2026-08-30).
        //
        // 예전에는 등록 수 하나만 찍었다 — 그런데 UpdateInstances는
        // m_instanceMap[prefab->GetFileGuid()]로 대상을 찾으므로, identity가
        // 어긋나면 등록부에 인스턴스가 멀쩡히 있어도 0건이 적용된다. 그때도
        // 로그는 "등록 인스턴스 2개에 적용"이라고 말해, 게이트가 읽을 수 있는
        // 유일한 창이 결함을 **정확히 가렸다**. 둘이 벌어지면 그게 신호다.
        const size_t registered = PrefabUtilitys->RegisteredInstanceCount();
        const size_t applied = PrefabUtilitys->UpdateInstances(prefab);

        Debug->LogWarning("[CLI] 프리팹 갱신 적용: " + prefabName + " <- " + objectName);
        std::printf("[prefab.update] %s <- %s · 등록 인스턴스 %zu개 중 %zu개에 적용\n",
            prefabName.c_str(), objectName.c_str(), registered, applied);
    }

    static void Cmd_prefab_status(const ConsoleCommandContext& ctx)
    {
        // 프리팹 연결 진단(트랙 P).
        //
        // 왜 필요한가: 인스턴스가 프리팹과의 연결을 잃어도 화면은 그대로다. 연결은
        // 다음에 프리팹을 고쳐서 반영이 안 될 때에야 드러나므로, 밖에서 볼 수 있는
        // 창이 없으면 왕복 회귀를 판정할 수 없다. bt.status가 BT에 대해 하는 일을
        // 프리팹에 대해 한다.
        //
        // 두 수를 따로 세는 것이 요점이다.
        //   씬 인스턴스 — 오브젝트가 든 m_prefabFileGuid. 직렬화되므로 왕복을 건넌다.
        //   등록        — PrefabUtility의 인스턴스 목록. 메모리에만 있어 왕복에서 끊긴다.
        // 둘이 벌어지면 "저장은 됐는데 연결은 복원되지 않았다"는 뜻이다.
        int sceneInstances = 0;
        if (Scene* scene = SceneManagers->GetActiveScene())
        {
            for (const auto& obj : scene->m_Entities)
            {
                if (obj && obj->m_prefabFileGuid != nullFileGuid)
                {
                    ++sceneInstances;
                }
            }
        }

        char line[256]{};
        std::snprintf(line, sizeof(line),
            "[prefab.status] 씬 인스턴스 %d개 · 등록 %zu개 · 캐시 %zu개",
            sceneInstances,
            PrefabUtilitys->RegisteredInstanceCount(),
            PrefabUtilitys->OwnedPrefabCount());
        std::printf("[CLI] %s\n", line);
        Debug->LogWarning(line);
    }

	static void Cmd_prefab_corpus_digest(const ConsoleCommandContext& ctx)
	{
		// D2-d: 호출자가 지정한 9개 프리팹의 인스턴스 루트 identity와 활성 씬의
		// 전체 prefab identity/override multiset을 저장 전후 비교 가능한 digest로
		// 출력한다. instanceID나 scene index처럼 재로드에서 바뀔 수 있는 값은 섞지
		// 않는다.
		if (ctx.parts.size() < 3)
		{
			std::printf("[CLI] 사용법: prefab.corpus.digest <라벨> <프리팹 이름>...\n");
			EngineBootstrap::SetExitCode(6);
			return;
		}

		Scene* scene = SceneManagers->GetActiveScene();
		if (!scene)
		{
			std::printf("[CLI] 활성 씬 없음\n");
			EngineBootstrap::SetExitCode(6);
			return;
		}

		const std::string& label = ctx.parts[1];
		const size_t expectedRoots = ctx.parts.size() - 2;
		size_t validRoots = 0;
		for (size_t index = 2; index < ctx.parts.size(); ++index)
		{
			const std::string& prefabName = ctx.parts[index];
			Prefab* prefab = PrefabUtilitys->LoadPrefab(prefabName);
			Entity* root = scene->GetEntity("D2Corpus_" + prefabName);
			if (prefab && root && prefab->GetFileGuid() != FileGuid{}
				&& root->m_prefabFileGuid == prefab->GetFileGuid())
			{
				++validRoots;
			}
		}

		std::vector<std::string> rows;
		size_t entityCount = 0;
		size_t overrideCount = 0;
		for (const auto& owner : scene->m_Entities)
		{
			Entity* entity = owner.get();
			if (!entity || entity->m_prefabFileGuid == FileGuid{}) continue;
			++entityCount;
			Entity* parent = scene->TryGetEntity(entity->GetParentIndex());
			const std::string parentName = parent
				? parent->m_name.ToString() : std::string("<none>");
			const std::string prefix = entity->m_name.ToString() + "|" + parentName
				+ "|" + entity->m_prefabFileGuid.ToString();
			rows.push_back("entity|" + prefix);
			for (const PrefabOverride& item : entity->m_prefabOverrides)
			{
				++overrideCount;
				rows.push_back("override|" + prefix + "|" + item.m_componentType
					+ "|" + std::to_string(item.m_componentSlot) + "|"
					+ item.m_propertyName + "|" + item.m_valueYaml);
			}
		}
		std::ranges::sort(rows);

		uint64_t digest = 1469598103934665603ull;
		for (const std::string& row : rows)
		{
			for (const unsigned char byte : row)
			{
				digest ^= byte;
				digest *= 1099511628211ull;
			}
			digest ^= static_cast<unsigned char>('\n');
			digest *= 1099511628211ull;
		}

		const size_t registered = PrefabUtilitys->RegisteredInstanceCount();
		const bool passed = validRoots == expectedRoots
			&& entityCount >= expectedRoots && registered >= expectedRoots;
		std::printf("[prefab.corpus:%s] %s roots=%zu/%zu entities=%zu "
			"overrides=%zu registered=%zu digest=%016llx\n",
			label.c_str(), passed ? "pass" : "fail", validRoots, expectedRoots,
			entityCount, overrideCount, registered,
			static_cast<unsigned long long>(digest));
		if (!passed) EngineBootstrap::SetExitCode(6);
	}

    static void Cmd_window_resize(const ConsoleCommandContext& ctx)
    {
        const std::vector<std::string>& parts = ctx.parts;

        // 해상도 독립 검증용(PHASE 7). 창을 실제로 리사이즈해 엔진의 리사이즈 경로를
        // 그대로 태운다 — g_ClientRect 갱신부터 UI 리플로우까지 실제 흐름을 검증해야
        // 의미가 있다.
        if (parts.size() < 3)
        {
            std::printf("[CLI] 사용법: window.resize <너비> <높이>\n");
            return;
        }

        const int width = std::atoi(parts[1].c_str());
        const int height = std::atoi(parts[2].c_str());
        if (width < 320 || height < 240)
        {
            std::printf("[CLI] 너무 작은 크기입니다: %dx%d\n", width, height);
            return;
        }

        // GetActiveWindow는 창이 포그라운드가 아니면 null을 준다. 스크립트 실행은
        // 대개 백그라운드라 이 경로가 실제로 걸리므로, 프로세스의 보이는 최상위 창을
        // 직접 찾아 대체한다.
        HWND hwnd = ::GetActiveWindow();
        if (nullptr == hwnd)
        {
            ::EnumWindows([](HWND candidate, LPARAM out) -> BOOL
            {
                DWORD pid = 0;
                ::GetWindowThreadProcessId(candidate, &pid);
                if (pid != ::GetCurrentProcessId()) return TRUE;
                if (!::IsWindowVisible(candidate)) return TRUE;
                if (::GetWindow(candidate, GW_OWNER) != nullptr) return TRUE;

                *reinterpret_cast<HWND*>(out) = candidate;
                return FALSE;
            }, reinterpret_cast<LPARAM>(&hwnd));
        }
        if (nullptr == hwnd) { std::printf("[CLI] 창 핸들 없음\n"); return; }

        // 클라이언트 영역이 요청 크기가 되도록 창 전체 크기를 역산한다.
        RECT desired{ 0, 0, width, height };
        const LONG style = ::GetWindowLong(hwnd, GWL_STYLE);
        const LONG exStyle = ::GetWindowLong(hwnd, GWL_EXSTYLE);
        ::AdjustWindowRectEx(&desired, style, FALSE, exStyle);

        ::SetWindowPos(hwnd, nullptr, 0, 0,
            desired.right - desired.left, desired.bottom - desired.top,
            SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);

        // 요청한 크기가 그대로 적용되지 않을 수 있다(모니터보다 큰 창은 잘린다).
        // 요청값만 찍으면 검증에서 엉뚱한 기준을 잡게 되므로 실제 결과를 읽어 보고한다.
        RECT actual{};
        ::GetClientRect(hwnd, &actual);
        const int actualWidth = actual.right - actual.left;
        const int actualHeight = actual.bottom - actual.top;

        const std::string message = (actualWidth == width && actualHeight == height)
            ? "[CLI] 창 크기 변경: " + std::to_string(actualWidth) + "x" + std::to_string(actualHeight)
            : "[CLI] 창 크기 변경(클램프됨): 요청 " + std::to_string(width) + "x" + std::to_string(height) +
              " -> 실제 " + std::to_string(actualWidth) + "x" + std::to_string(actualHeight);

        Debug->LogWarning(message);
        std::printf("%s\n", message.c_str());
    }

    static void Cmd_ui_rect(const ConsoleCommandContext& ctx)
    {
        const std::vector<std::string>& parts = ctx.parts;
        const std::string& line = ctx.line;

        // 오브젝트 이하 전체의 worldRect를 재귀로 찍는다. 이름 대신 *를 주면 씬의
        // 모든 RectTransform을 훑는다 — 해상도를 바꿔 가며, 또는 코드를 고치기
        // 전후로 같은 명령을 돌려 레이아웃 결과를 통째로 대조하기 위한 것이다(PHASE 7).
        if (parts.size() < 2)
        {
            std::printf("[CLI] 사용법: ui.rect <오브젝트 이름 | *>\n");
            return;
        }

        Scene* scene = SceneManagers->GetActiveScene();
        if (!scene) { std::printf("[CLI] 활성 씬 없음\n"); return; }

        // 재귀 깊이 상한 — 계층이 순환하더라도 CLI가 스택을 태우지 않게 한다.
        constexpr int kMaxDepth = 32;

        std::function<void(Entity*, int)> dump = [&](Entity* obj, int depth)
        {
            if (nullptr == obj || depth > kMaxDepth) return;

            if (auto* rect = obj->GetComponent<RectTransformComponent>())
            {
                const auto& world = rect->GetWorldRect();
                const auto& size = rect->GetSizeDelta();
                const auto& anchored = rect->GetAnchoredPosition();
                const auto screenPosition = rect->GetScreenPosition();
                const std::string line =
                    std::string(static_cast<size_t>(depth) * 2, ' ') + obj->m_name.ToString() +
                    " world(" + std::to_string(static_cast<int>(world.x)) + ", " +
                    std::to_string(static_cast<int>(world.y)) + ", " +
                    std::to_string(static_cast<int>(world.width)) + ", " +
                    std::to_string(static_cast<int>(world.height)) + ")" +
                    " sizeDelta(" + std::to_string(static_cast<int>(size.x)) + ", " +
                    std::to_string(static_cast<int>(size.y)) + ")" +
                    " anchor(" + std::to_string(rect->GetAnchorMin().x).substr(0, 4) + "," +
                    std::to_string(rect->GetAnchorMin().y).substr(0, 4) + "-" +
                    std::to_string(rect->GetAnchorMax().x).substr(0, 4) + "," +
                    std::to_string(rect->GetAnchorMax().y).substr(0, 4) + ")" +
                    " pos(" + std::to_string(static_cast<int>(anchored.x)) + ", " +
                    std::to_string(static_cast<int>(anchored.y)) + ")" +
                    " screen(" + std::to_string(static_cast<int>(screenPosition.x)) + ", " +
                    std::to_string(static_cast<int>(screenPosition.y)) + ")" +
                    " scale(" + std::to_string(rect->GetLayoutScale()).substr(0, 5) + ")";

                std::printf("[CLI] %s\n", line.c_str());
                Debug->LogWarning("[ui.rect] " + line);
            }

            for (auto childIndex : obj->GetChildrenIndices())
            {
                dump(obj->OwnerSceneFindIndex(childIndex), depth + 1);
            }
        };

        if (parts[1] == "*")
        {
            // 최상위는 "아무의 자식도 아닌 오브젝트"로 가린다. m_parentIndex만 보고
            // 판별했더니 프리팹 루트 밑의 캔버스가 최상위로도 잡혀 같은 서브트리가
            // 두 번 찍혔다 — 대조에서 개수가 정확히 두 배가 되어 드러났다.
            std::unordered_set<Entity::Index> childIndices;
            for (const auto& obj : scene->m_Entities)
            {
                if (!obj || obj->IsDestroyMark()) continue;
                for (auto childIndex : obj->GetChildrenIndices()) childIndices.insert(childIndex);
            }

            for (const auto& obj : scene->m_Entities)
            {
                if (!obj || obj->IsDestroyMark()) continue;
                if (childIndices.count(obj->m_index)) continue;
                dump(obj.get(), 0);
            }
        }
        else
        {
			Entity* target = scene->GetEntity(parts[1]);
            if (!target) { std::printf("[CLI] 오브젝트 없음: %s\n", parts[1].c_str()); return; }
            dump(target, 0);
        }
    }

    static void Cmd_ui_anchor(const ConsoleCommandContext& ctx)
    {
        const std::vector<std::string>& parts = ctx.parts;
        const std::string& cmd = ctx.cmd;

        // 스트레치 앵커처럼 저작 데이터에 없는 배치를 검증하려면 값을 직접 넣어 봐야 한다.
        // ui.anchor <오브젝트> <minX> <minY> <maxX> <maxY> / ui.size <오브젝트> <x> <y>
        // ui.pos <오브젝트> <x> <y>       — 앵커 기준 로컬 anchoredPosition
        // ui.screenpos <오브젝트> <x> <y> — 좌상단 원점 화면 픽셀 좌표
        const size_t needed = (cmd == "ui.anchor") ? 6 : 4;
        if (parts.size() < needed)
        {
            std::printf("[CLI] 사용법: ui.anchor <오브젝트> <minX> <minY> <maxX> <maxY>"
                " · ui.size <오브젝트> <x> <y> · ui.pos/ui.screenpos <오브젝트> <x> <y>\n");
            return;
        }

        Scene* scene = SceneManagers->GetActiveScene();
        if (!scene) { std::printf("[CLI] 활성 씬 없음\n"); return; }

		Entity* target = scene->GetEntity(parts[1]);
        if (!target) { std::printf("[CLI] 오브젝트 없음: %s\n", parts[1].c_str()); return; }

        auto* rect = target->GetComponent<RectTransformComponent>();
        if (!rect) { std::printf("[CLI] RectTransform 없음: %s\n", parts[1].c_str()); return; }

        if (cmd == "ui.anchor")
        {
            const math::vector2 min{ std::strtof(parts[2].c_str(), nullptr), std::strtof(parts[3].c_str(), nullptr) };
            const math::vector2 max{ std::strtof(parts[4].c_str(), nullptr), std::strtof(parts[5].c_str(), nullptr) };
            rect->SetAnchorMin(min);
            rect->SetAnchorMax(max);
            std::printf("[CLI] %s 앵커 = (%.2f,%.2f)-(%.2f,%.2f)\n",
                parts[1].c_str(), min.x, min.y, max.x, max.y);
        }
        else if (cmd == "ui.pos")
        {
            const math::vector2 pos{ std::strtof(parts[2].c_str(), nullptr),
                                      std::strtof(parts[3].c_str(), nullptr) };
            rect->SetAnchoredPosition(pos);
            std::printf("[CLI] %s anchoredPosition = (%.2f,%.2f)\n",
                parts[1].c_str(), pos.x, pos.y);
        }
        else if (cmd == "ui.screenpos")
        {
            const math::vector2 pos{ std::strtof(parts[2].c_str(), nullptr),
                                      std::strtof(parts[3].c_str(), nullptr) };
            rect->SetScreenPosition(pos);
            std::printf("[CLI] %s screenPosition = (%.2f,%.2f)\n",
                parts[1].c_str(), pos.x, pos.y);
        }
        else
        {
            const math::vector2 size{ std::strtof(parts[2].c_str(), nullptr), std::strtof(parts[3].c_str(), nullptr) };
            rect->SetSizeDelta(size);
            std::printf("[CLI] %s sizeDelta = (%.2f,%.2f)\n", parts[1].c_str(), size.x, size.y);
        }
    }

    static void Cmd_ui_hitbox(const ConsoleCommandContext& ctx)
    {
        const std::string& line = ctx.line;

        // 버튼의 클릭 판정 상자를 rect와 나란히 찍는다. 두 값이 같아야 보이는 곳과
        // 눌리는 곳이 일치한다 — 해상도가 바뀌어도 유지되는지가 검증 대상이다(PHASE 7-7).
        Scene* scene = SceneManagers->GetActiveScene();
        if (!scene) { std::printf("[CLI] 활성 씬 없음\n"); return; }

        int reported = 0;
        for (const auto& owned : scene->m_Entities)
        {
            Entity* owner = owned.get();
            if (nullptr == owner || owner->IsDestroyMark()) continue;

            auto* button = owner->GetComponent<UIButton>();
            if (nullptr == button) continue;

            auto* rect = owner->GetComponent<RectTransformComponent>();
            if (nullptr == rect) continue;

            const auto& hitbox = button->GetHitbox();
            const auto& world = rect->GetWorldRect();
            const std::string line = owner->m_name.ToString() +
                " rect(" + std::to_string(static_cast<int>(world.x)) + ", " +
                std::to_string(static_cast<int>(world.y)) + ", " +
                std::to_string(static_cast<int>(world.width)) + ", " +
                std::to_string(static_cast<int>(world.height)) + ")" +
                " hitbox(" + std::to_string(static_cast<int>(hitbox.x)) + ", " +
                std::to_string(static_cast<int>(hitbox.y)) + ", " +
                std::to_string(static_cast<int>(hitbox.width)) + ", " +
                std::to_string(static_cast<int>(hitbox.height)) + ")";

            std::printf("[CLI] %s\n", line.c_str());
            Debug->LogWarning("[ui.hitbox] " + line);
            ++reported;
        }

        if (0 == reported) std::printf("[CLI] 버튼 없음\n");
    }

	static void Cmd_ui_navprobe(const ConsoleCommandContext&)
	{
		// U7/E7-c 전용 무자산 회귀. 형제+손자 경로를 가진 UI 프리팹을 메모리에서
		// 굽고 두 번 소환해, Navigation이 각 인스턴스 내부에서만 풀리는지와 UI도
		// 매번 새 instanceID를 받는지 함께 본다. 이어서 같은 데이터를 구 navObject
		// 형식으로 되돌려 인메모리 승격 경로도 태운다.
		Scene* scene = SceneManagers->GetActiveScene();
		if (!scene)
		{
			std::printf("[ui.navprobe] FAIL 활성 씬 없음\n");
			return;
		}

		auto authorRoot = scene->CreateEntity("__NavAuthorRoot", GameObjectType::Canvas);
		if (!authorRoot)
		{
			std::printf("[ui.navprobe] FAIL 저작 계층 생성 실패\n");
			return;
		}
		auto source = scene->CreateEntity("__NavSource", GameObjectType::UI, authorRoot->m_index);
		auto branch = scene->CreateEntity("__NavBranch", GameObjectType::UI, authorRoot->m_index);
		if (!source || !branch)
		{
			std::printf("[ui.navprobe] FAIL 저작 계층 생성 실패\n");
			return;
		}
		auto target = scene->CreateEntity("__NavTarget", GameObjectType::UI, branch->m_index);
		if (!target)
		{
			std::printf("[ui.navprobe] FAIL 저작 계층 생성 실패\n");
			return;
		}

		ImageComponent* sourceImage = source->AddComponent<ImageComponent>();
		target->AddComponent<ImageComponent>();
		if (!sourceImage)
		{
			std::printf("[ui.navprobe] FAIL ImageComponent 생성 실패\n");
			return;
		}
		sourceImage->SetNavi(Direction::Right, target);

		auto makeSequenceData = [](Prefab* prefab)
		{
			MetaYml::Node data;
			const MetaYml::Node& authored = prefab->GetPrefabData();
			if (authored.IsSequence()) data = MetaYml::Clone(authored);
			else data.push_back(MetaYml::Clone(authored));
			prefab->SetPrefabData(Authoring::NodeViewAccess::Make(data));
		};

		Prefab* prefab = PrefabUtilitys->CreatePrefab(authorRoot, "__NavProbePrefab");
		if (!prefab)
		{
			std::printf("[ui.navprobe] FAIL 프리팹 생성 실패\n");
			return;
		}
		makeSequenceData(prefab);

		const std::string serialized = MetaYml::Dump(prefab->GetPrefabData());
		const bool schemaOk = serialized.find("navObject") == std::string::npos
			&& serialized.find("parentHops") != std::string::npos
			&& serialized.find("childOrdinals") != std::string::npos
			&& serialized.find("m_gameObjectType") == std::string::npos;

		auto resolveInstance = [scene](Entity* root, Entity*& outSource, Entity*& outTarget,
			ImageComponent*& outSourceImage) -> bool
		{
			outSource = nullptr;
			outTarget = nullptr;
			outSourceImage = nullptr;
			if (!root || root->GetChildrenIndices().size() < 2) return false;

			outSource = scene->TryGetEntity(root->GetChildrenIndices()[0]);
			Entity* instanceBranch = scene->TryGetEntity(root->GetChildrenIndices()[1]);
			if (!outSource || !instanceBranch || instanceBranch->GetChildrenIndices().empty()) return false;
			outTarget = scene->TryGetEntity(instanceBranch->GetChildrenIndices()[0]);
			outSourceImage = outSource ? outSource->GetComponent<ImageComponent>() : nullptr;
			if (!outTarget || !outSourceImage) return false;
			outSourceImage->DeserializeNavi();
			return outSourceImage->GetNextNavi(Direction::Right) == outTarget;
		};

		Entity* instanceA = prefab->Instantiate(scene, "__NavInstanceA");
		Entity* instanceB = prefab->Instantiate(scene, "__NavInstanceB");
		Entity* sourceA = nullptr; Entity* targetA = nullptr; ImageComponent* imageA = nullptr;
		Entity* sourceB = nullptr; Entity* targetB = nullptr; ImageComponent* imageB = nullptr;
		const bool instanceAOk = resolveInstance(instanceA, sourceA, targetA, imageA);
		const bool instanceBOk = resolveInstance(instanceB, sourceB, targetB, imageB);
		const bool isolated = instanceAOk && instanceBOk
			&& imageA->GetNextNavi(Direction::Right) == targetA
			&& imageB->GetNextNavi(Direction::Right) == targetB
			&& targetA != targetB;
		const bool freshIds = sourceA && sourceB && targetA && targetB
			&& sourceA->GetInstanceID() != sourceB->GetInstanceID()
			&& targetA->GetInstanceID() != targetB->GetInstanceID();
		const bool spatialComposition = instanceA
			&& instanceA->GetComponent<Transform>()
			&& instanceA->GetComponent<RectTransformComponent>()
			&& sourceA && !sourceA->GetComponent<Transform>()
			&& sourceA->GetComponent<RectTransformComponent>();

		// 구 navObject 파일 승격: 새 경로 필드를 지우고 저작 대상의 옛 ID를 넣는다.
		MetaYml::Node legacyData = MetaYml::Clone(prefab->GetPrefabData());
		MetaYml::Node sourceComponents = legacyData[0]["children"][0]["m_components"];
		bool legacyFixtureBuilt = false;
		for (auto componentNode : sourceComponents)
		{
			MetaYml::Node navs = componentNode["navigations"];
			if (!navs || !navs.IsSequence() || navs.size() == 0) continue;
			navs[0]["navObject"] = target->GetInstanceID();
			navs[0].remove("parentHops");
			navs[0].remove("childOrdinals");
			legacyFixtureBuilt = true;
			break;
		}

		Prefab* legacyPrefab = PrefabUtilitys->CreatePrefab(authorRoot, "__NavLegacyProbePrefab");
		if (legacyPrefab) legacyPrefab->SetPrefabData(Authoring::NodeViewAccess::Make(legacyData));
		Entity* legacyInstance = legacyPrefab ? legacyPrefab->Instantiate(scene, "__NavLegacyInstance") : nullptr;
		Entity* legacySource = nullptr; Entity* legacyTarget = nullptr; ImageComponent* legacyImage = nullptr;
		const bool legacyOk = legacyFixtureBuilt
			&& resolveInstance(legacyInstance, legacySource, legacyTarget, legacyImage);

		const bool passed = schemaOk && isolated && freshIds && spatialComposition && legacyOk;
		std::printf("[ui.navprobe] %s schema=%s isolated=%s freshIds=%s spatial=%s legacy=%s\n",
			passed ? "PASS" : "FAIL",
			schemaOk ? "PASS" : "FAIL",
			isolated ? "PASS" : "FAIL",
			freshIds ? "PASS" : "FAIL",
			spatialComposition ? "PASS" : "FAIL",
			legacyOk ? "PASS" : "FAIL");
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

    static void Cmd_dx12_selftest(const ConsoleCommandContext& ctx)
    {
        const std::vector<std::string>& parts = ctx.parts;
        const std::string& line = ctx.line;

        // EnhancedSceneRenderer 브링업 자가 검증(PHASE 3-3). 자체 디바이스·큐·펜스로
        // 돌므로 DX11 렌더 스레드와 충돌하지 않는다 — 게임 스레드에서 즉시 실행.
        const std::string outputPath = ResolveTestArtifactPath("DX12",
            (parts.size() > 1) ? parts[1] : std::string("dx12_selftest.png"));

        std::string log;
        const bool passed = DX12Test::RunSelfTest(outputPath, 6, log);

        for (const auto& line : { log })
        {
            std::printf("%s", line.c_str());
        }
        Debug->LogWarning(std::string("[dx12.selftest] ") + (passed ? "통과" : "실패") + "\n" + log);
        std::printf("[CLI] dx12.selftest %s → %s\n", passed ? "통과" : "실패", outputPath.c_str());
    }

    static void Cmd_experiment_model(const ConsoleCommandContext& ctx)
    {
        const std::vector<std::string>& parts = ctx.parts;

        // legacy 로딩과 Experiment 모델 경계를 같은 자산으로 비교하는 패리티 검증.
        // CPU 전용이라 렌더 스레드와 충돌하지 않는다 — 게임 스레드에서 즉시 실행.
        if (parts.size() < 2)
        {
            Debug->LogWarning("[experiment.model] 사용법: experiment.model <모델 경로(공백 없는)>");
            std::printf("[CLI] experiment.model 사용법: experiment.model <모델 경로>\n");
            return;
        }
        const std::string& modelPath = parts[1];

        std::string log;
        const bool passed =
            RenderTest::RunExperimentModelParitySelfTest(modelPath, log);

        std::printf("%s", log.c_str());
        if (passed)
        {
            Debug->LogWarning(std::string("[experiment.model] 통과\n") + log);
        }
        else
        {
            Debug->LogError(std::string("[experiment.model] 실패\n") + log);
        }
        std::printf("[CLI] experiment.model %s → %s\n",
            passed ? "통과" : "실패", modelPath.c_str());
    }

    static void Cmd_experiment_vertexlayout(const ConsoleCommandContext&)
    {
        // I5-D2(V4) — 마스크→RHI 입력 레이아웃 유도 합성 검사. CPU 전용.
        std::string log;
        const bool passed = RenderTest::RunExperimentVertexLayoutSelfTest(log);
        std::printf("%s", log.c_str());
        if (passed)
        {
            Debug->LogWarning(std::string("[experiment.vertexlayout] 통과\n")
                + log);
        }
        else
        {
            Debug->LogError(std::string("[experiment.vertexlayout] 실패\n")
                + log);
        }
        std::printf("[CLI] experiment.vertexlayout %s\n",
            passed ? "통과" : "실패");
    }

    static void Cmd_experiment_modelbridge(const ConsoleCommandContext& ctx)
    {
        // I5-D1a — 역브리지(experiment→legacy) 왕복 검사. CPU 전용.
        const std::vector<std::string>& parts = ctx.parts;
        if (parts.size() < 2)
        {
            Debug->LogWarning("[experiment.modelbridge] 사용법: "
                "experiment.modelbridge <모델 경로(공백 없는)>");
            std::printf("[CLI] experiment.modelbridge 사용법: "
                "experiment.modelbridge <모델 경로>\n");
            return;
        }
        const std::string& modelPath = parts[1];

        std::string log;
        const bool passed =
            RenderTest::RunExperimentModelBridgeSelfTest(modelPath, log);

        std::printf("%s", log.c_str());
        if (passed)
        {
            Debug->LogWarning(std::string("[experiment.modelbridge] 통과\n")
                + log);
        }
        else
        {
            Debug->LogError(std::string("[experiment.modelbridge] 실패\n")
                + log);
        }
        std::printf("[CLI] experiment.modelbridge %s → %s\n",
            passed ? "통과" : "실패", modelPath.c_str());
    }

    static void Cmd_experiment_anim(const ConsoleCommandContext& ctx)
    {
        const std::vector<std::string>& parts = ctx.parts;

        // Experiment clip 재생 배선 검증. CPU 전용 — 게임 스레드에서 즉시 실행.
        if (parts.size() < 2)
        {
            Debug->LogWarning("[experiment.anim] 사용법: experiment.anim <모델 경로(공백 없는)>");
            std::printf("[CLI] experiment.anim 사용법: experiment.anim <모델 경로>\n");
            return;
        }
        const std::string& modelPath = parts[1];

        std::string log;
        const bool passed =
            RenderTest::RunExperimentAnimationPlaybackSelfTest(modelPath, log);

        std::printf("%s", log.c_str());
        if (passed)
        {
            Debug->LogWarning(std::string("[experiment.anim] 통과\n") + log);
        }
        else
        {
            Debug->LogError(std::string("[experiment.anim] 실패\n") + log);
        }
        std::printf("[CLI] experiment.anim %s → %s\n",
            passed ? "통과" : "실패", modelPath.c_str());
    }

    static void Cmd_experiment_import(const ConsoleCommandContext& ctx)
    {
        const std::vector<std::string>& parts = ctx.parts;

        // legacy → ImportedScene → ModelDraft 임포트 경로 검증.
        // CPU 전용 — 게임 스레드에서 즉시 실행.
        if (parts.size() < 2)
        {
            Debug->LogWarning("[experiment.import] 사용법: experiment.import <모델 경로(공백 없는)>");
            std::printf("[CLI] experiment.import 사용법: experiment.import <모델 경로>\n");
            return;
        }
        const std::string& modelPath = parts[1];

        std::string log;
        const bool passed =
            RenderTest::RunExperimentImportPathSelfTest(modelPath, log);

        std::printf("%s", log.c_str());
        if (passed)
        {
            Debug->LogWarning(std::string("[experiment.import] 통과\n") + log);
        }
        else
        {
            Debug->LogError(std::string("[experiment.import] 실패\n") + log);
        }
        std::printf("[CLI] experiment.import %s → %s\n",
            passed ? "통과" : "실패", modelPath.c_str());
    }

    static void Cmd_experiment_gltf(const ConsoleCommandContext& ctx)
    {
        const std::vector<std::string>& parts = ctx.parts;

        // fastgltf 임포터 경로 검증(Assimp 기준선과 비교). CPU 전용.
        if (parts.size() < 2)
        {
            Debug->LogWarning("[experiment.gltf] 사용법: experiment.gltf <glTF/GLB 경로(공백 없는)>");
            std::printf("[CLI] experiment.gltf 사용법: experiment.gltf <경로>\n");
            return;
        }
        const std::string& modelPath = parts[1];

        std::string log;
        const bool passed =
            RenderTest::RunExperimentGltfImportSelfTest(modelPath, log);

        std::printf("%s", log.c_str());
        if (passed)
        {
            Debug->LogWarning(std::string("[experiment.gltf] 통과\n") + log);
        }
        else
        {
            Debug->LogError(std::string("[experiment.gltf] 실패\n") + log);
        }
        std::printf("[CLI] experiment.gltf %s → %s\n",
            passed ? "통과" : "실패", modelPath.c_str());
    }

    static void Cmd_experiment_fbx(const ConsoleCommandContext& ctx)
    {
        const std::vector<std::string>& parts = ctx.parts;

        // ufbx 임포터 경로 검증(Assimp 기준선과 비교). CPU 전용.
        if (parts.size() < 2)
        {
            Debug->LogWarning("[experiment.fbx] 사용법: experiment.fbx <FBX 경로(공백 없는)>");
            std::printf("[CLI] experiment.fbx 사용법: experiment.fbx <경로>\n");
            return;
        }
        const std::string& modelPath = parts[1];

        std::string log;
        const bool passed =
            RenderTest::RunExperimentFbxImportSelfTest(modelPath, log);

        std::printf("%s", log.c_str());
        if (passed)
        {
            Debug->LogWarning(std::string("[experiment.fbx] 통과\n") + log);
        }
        else
        {
            Debug->LogError(std::string("[experiment.fbx] 실패\n") + log);
        }
        std::printf("[CLI] experiment.fbx %s → %s\n",
            passed ? "통과" : "실패", modelPath.c_str());
    }

    static void Cmd_experiment_sampler(const ConsoleCommandContext&)
    {
        // 합성 보간 검사. 자산을 읽지 않으므로 인자가 없고 항상 같은 것을 잰다.
        std::string log;
        const bool passed = RenderTest::RunExperimentSamplerSelfTest(log);

        std::printf("%s", log.c_str());
        if (passed)
        {
            Debug->LogWarning(std::string("[experiment.sampler] 통과\n") + log);
        }
        else
        {
            Debug->LogError(std::string("[experiment.sampler] 실패\n") + log);
        }
        std::printf("[CLI] experiment.sampler %s\n", passed ? "통과" : "실패");
    }

    static void Cmd_experiment_tangent(const ConsoleCommandContext&)
    {
        // mikktspace 탄젠트 생성의 합성 검사. 자산을 읽지 않는다.
        std::string log;
        const bool passed = RenderTest::RunExperimentTangentSelfTest(log);

        std::printf("%s", log.c_str());
        if (passed)
        {
            Debug->LogWarning(std::string("[experiment.tangent] 통과\n") + log);
        }
        else
        {
            Debug->LogError(std::string("[experiment.tangent] 실패\n") + log);
        }
        std::printf("[CLI] experiment.tangent %s\n", passed ? "통과" : "실패");
    }

    static void Cmd_experiment_normal(const ConsoleCommandContext&)
    {
        // 평면 법선 생성의 합성 검사. 자산을 읽지 않는다.
        std::string log;
        const bool passed = RenderTest::RunExperimentNormalSelfTest(log);

        std::printf("%s", log.c_str());
        if (passed)
        {
            Debug->LogWarning(std::string("[experiment.normal] 통과\n") + log);
        }
        else
        {
            Debug->LogError(std::string("[experiment.normal] 실패\n") + log);
        }
        std::printf("[CLI] experiment.normal %s\n", passed ? "통과" : "실패");
    }

    static void Cmd_experiment_cacheopt(const ConsoleCommandContext&)
    {
        // 정점 캐시/페치 최적화의 합성 검사. 자산을 읽지 않는다.
        std::string log;
        const bool passed = RenderTest::RunExperimentCacheOptSelfTest(log);

        std::printf("%s", log.c_str());
        if (passed)
        {
            Debug->LogWarning(std::string("[experiment.cacheopt] 통과\n") + log);
        }
        else
        {
            Debug->LogError(std::string("[experiment.cacheopt] 실패\n") + log);
        }
        std::printf("[CLI] experiment.cacheopt %s\n", passed ? "통과" : "실패");
    }

    static void Cmd_experiment_weld(const ConsoleCommandContext&)
    {
        // 정점 용접의 합성 검사. 자산을 읽지 않는다.
        std::string log;
        const bool passed = RenderTest::RunExperimentWeldSelfTest(log);

        std::printf("%s", log.c_str());
        if (passed)
        {
            Debug->LogWarning(std::string("[experiment.weld] 통과\n") + log);
        }
        else
        {
            Debug->LogError(std::string("[experiment.weld] 실패\n") + log);
        }
        std::printf("[CLI] experiment.weld %s\n", passed ? "통과" : "실패");
    }

    static void Cmd_experiment_cooked(const ConsoleCommandContext& ctx)
    {
        // 인자가 없으면 합성 검사, 있으면 그 자산으로 실자산 왕복까지 돌다.
        std::string log;
        bool passed = RenderTest::RunExperimentCookedSelfTest(log);
        if (ctx.parts.size() > 1)
        {
            // ★ && 로 이어 붙이지 않는다 — 단축 평가로 두 번째가 안 돌면
            //   "합성만 돌고 통과"가 실자산 통과처럼 보인다.
            const bool roundTrip =
                RenderTest::RunExperimentCookedRoundTrip(ctx.parts[1], log);
            passed = passed && roundTrip;
        }

        std::printf("%s", log.c_str());
        if (passed)
        {
            Debug->LogWarning(std::string("[experiment.cooked] 통과\n") + log);
        }
        else
        {
            Debug->LogError(std::string("[experiment.cooked] 실패\n") + log);
        }
        std::printf("[CLI] experiment.cooked %s\n", passed ? "통과" : "실패");
    }

    static void Cmd_experiment_texcook(const ConsoleCommandContext& ctx)
    {
        // 인자가 없으면 합성 검사, <assetRoot> <texture> 두 개면 실자산까지.
        std::string log;
        bool passed = RenderTest::RunExperimentTextureCookSelfTest(log);
        if (ctx.parts.size() > 2)
        {
            // ★ && 로 이어 붙이지 않는다 — 단축 평가로 두 번째가 안 돌면
            //   "합성만 돌고 통과"가 실자산 통과처럼 보인다.
            const bool real = RenderTest::RunExperimentTextureCookReal(
                ctx.parts[1], ctx.parts[2], log);
            passed = passed && real;
        }

        std::printf("%s", log.c_str());
        if (passed)
        {
            Debug->LogWarning(std::string("[experiment.texcook] 통과\n") + log);
        }
        else
        {
            Debug->LogError(std::string("[experiment.texcook] 실패\n") + log);
        }
        std::printf("[CLI] experiment.texcook %s\n", passed ? "통과" : "실패");
    }

    static void Cmd_experiment_smcook(const ConsoleCommandContext& ctx)
    {
        // 인자가 없으면 합성 검사, <assetRoot> <shadermeta> 두 개면 실자산까지.
        std::string log;
        bool passed = RenderTest::RunExperimentShaderMetaCookSelfTest(log);
        if (ctx.parts.size() > 2)
        {
            const bool real = RenderTest::RunExperimentShaderMetaCookReal(
                ctx.parts[1], ctx.parts[2], log);
            passed = passed && real;
        }

        std::printf("%s", log.c_str());
        if (passed)
        {
            Debug->LogWarning(std::string("[experiment.smcook] 통과\n") + log);
        }
        else
        {
            Debug->LogError(std::string("[experiment.smcook] 실패\n") + log);
        }
        std::printf("[CLI] experiment.smcook %s\n", passed ? "통과" : "실패");
    }

    static void Cmd_experiment_matcook(const ConsoleCommandContext& ctx)
    {
        // 인자 없으면 합성. <assetRoot> <material> [model] 이면 실자산까지.
        std::string log;
        bool passed = RenderTest::RunExperimentMaterialCookSelfTest(log);
        if (ctx.parts.size() > 2)
        {
            const bool real = RenderTest::RunExperimentMaterialCookReal(
                ctx.parts[1], ctx.parts[2], log);
            passed = passed && real;
        }
        if (ctx.parts.size() > 3)
        {
            // ★ 모델 쪽이 b2c-3 의 본체다 — 재질 의존과 임베디드 추출.
            const bool model = RenderTest::RunExperimentModelDependencyReal(
                ctx.parts[1], ctx.parts[3], log);
            passed = passed && model;
        }

        std::printf("%s", log.c_str());
        if (passed)
        {
            Debug->LogWarning(std::string("[experiment.matcook] 통과\n") + log);
        }
        else
        {
            Debug->LogError(std::string("[experiment.matcook] 실패\n") + log);
        }
        std::printf("[CLI] experiment.matcook %s\n", passed ? "통과" : "실패");
    }

    static void Cmd_experiment_matparity(const ConsoleCommandContext& ctx)
    {
        // I5-M1 — experiment::Material → 정본 packer CB bytes의 legacy 비트
        // 패리티. 합성(타입 8종) + 실사(Slang reflection layout) 두 leg 다 돈다.
        (void)ctx;
        std::string log;
        bool passed = RenderTest::RunExperimentMaterialParitySelfTest(log);
        passed = RenderTest::RunExperimentMaterialParityReal(log) && passed;

        std::printf("%s", log.c_str());
        if (passed)
        {
            Debug->LogWarning(std::string("[experiment.matparity] 통과\n") + log);
        }
        else
        {
            Debug->LogError(std::string("[experiment.matparity] 실패\n") + log);
        }
        std::printf("[CLI] experiment.matparity %s\n", passed ? "통과" : "실패");
    }

    // I5-D5c1 — 재질 런타임 병행 표현의 전수 A/B, 그리고 **왕복 손실의 실측**.
    //
    // 지금 제품은 저작 문서를 experiment로 읽어 legacy 객체를 만들고, sealing이
    // 매 프레임 그 legacy를 experiment로 되돌린다(experiment→legacy→experiment).
    // D5-c1은 저작 원본을 버리지 않고 MeshRenderer에 병행 보관한다. 이 게이트가
    // 재는 것은 두 가지다:
    //
    //   A/B 동등 — 저작 원본+override로 합성한 재질(B)이 legacy 왕복 결과(A)와
    //     같은가. 값 비교는 코덱 인코딩 텍스트로 한다(수학 타입에 operator==가
    //     없다 — S2c-2a의 diff writer와 같은 규약).
    //   왕복 손실 — 같지 않다면 **어디서** 갈리는가. onlyAuthored는 저작 원본에만
    //     있는 항목이다: 변환기 헤더가 예고한 손실(legacy에 표현이 없는 string
    //     property·texture colorSpace)이 여기 숫자로 나온다. 이 계수가 0이 아니면
    //     D5-c2의 sealing 직행이 **화면을 바꾼다**는 뜻이므로, 그 값을 모르고
    //     넘어가면 c2의 픽셀 차이를 선재 손실과 구분할 수 없다.
    //
    // 이 슬라이스에서 병행 표현의 소비자는 이 게이트뿐이다(렌더는 아직 legacy).
    // I5-D5c2-1 — packing 직전 논리 값의 A/B를 **바이트로** 재기 위한 합성
    // layout. 실제 layout은 셰이더 reflection 산물이라(EnsureShaderMetaVariant —
    // 렌더 패스 컨텍스트 필요) 헤드리스 CLI에서 얻을 수 없다. 그래서 meta 선언
    // 순서대로 offset을 순차 배치한 layout을 만든다.
    //
    // ★ 한계(정직): 이 offset은 제품 GPU 레이아웃이 **아니다**. 이 축이 재는
    //   것은 "두 경로가 같은 논리 값을 packing하는가"이지 "같은 자리에 올리는가"가
    //   아니다. 자리 판정은 D34 계열 픽셀 게이트의 몫이다.
    [[nodiscard]] static bool BuildSyntheticBindingLayout(const ShaderMeta& meta,
        ShaderMetaBindingLayout& outLayout, std::string& outError)
    {
        outLayout = ShaderMetaBindingLayout{};
        outLayout.constantBufferName = "SyntheticMaterialCB";
        std::uint32_t offset = 0;
        for (const ShaderPropertyDesc& desc : meta.properties)
        {
            ShaderMetaPropertyBinding binding;
            binding.name = desc.name;
            binding.propertyType = desc.type;
            if (ShaderPropertyType::Texture2D == desc.type)
            {
                binding.resourceKind = RHIShaderResourceKind::Texture;
                outLayout.properties.push_back(std::move(binding));
                continue;
            }
            const std::size_t size =
                MaterialPropertyPacker::LogicalByteSize(desc.type);
            if (0 == size)
            {
                outError = "논리 크기 0인 property: " + desc.name;
                return false;
            }
            binding.resourceKind = RHIShaderResourceKind::ConstantBuffer;
            binding.byteOffset = offset;
            binding.byteSize = static_cast<std::uint32_t>(size);
            offset += static_cast<std::uint32_t>(size);
            outLayout.properties.push_back(std::move(binding));
        }
        outLayout.constantBufferByteSize = offset;
        return offset > 0;
    }

    // I5-D5c1 — 합성 seed. **코퍼스에 새 정본 저작분이 0이다**(실측: 씬·프리팹의
    // shaderAssetId 0건, ref 표기 0건, standalone 재질 2개 전부 legacy 표기).
    // S2b writer는 ShaderMeta를 아는 재질만 새 정본으로 쓰는데 코퍼스 재질의
    // m_shaderMetaGuid가 전부 nil이라, S2c-2a가 만든 base 참조 저작 경로가
    // 실자산에서 한 번도 돈 적이 없다 — 실자산 게이트는 이 슬라이스에 판별력이
    // 0이고 합성이 필수다(D5-a Foliage와 같은 결론).
    //
    // seed는 저작 경로 그대로 간다: 새 정본 재질 자산을 게시하고(저작 루트
    // 가드를 지나는 WriteTextAssetWithMeta — GUID 발급 포함), 씬 renderer를 그
    // base에 링크한 뒤 override 하나를 얹는다. 저장이 ref 표기를 내고, 재로드가
    // 병행 표현을 채운다.
    static bool SeedAuthoredMaterial(Scene& scene, const file::path& directory,
        std::string& outError)
    {
        // ShaderMeta는 실물을 쓴다 — 지어낸 GUID로는 keywords 정규화도 property
        // 검증도 돌지 않아 seed가 검사 대상을 비껴간다.
        const file::path metaPath = PathFinder::Relative("Shaders\\")
            / "DefaultPassShader" / "GBuffer.shadermeta";
        const FileGuid shaderGuid = DataSystems->GetFileGuid(metaPath);
        if (FileGuid{} == shaderGuid)
        {
            outError = "GBuffer.shadermeta GUID 미해석: " + metaPath.string();
            return false;
        }
        std::string error;
        const ShaderMetaHandle handle =
            DataSystems->LoadShaderMetaHandle(shaderGuid, error);
        const std::shared_ptr<const ShaderMeta> meta =
            DataSystems->ResolveShaderMeta(handle);
        if (!meta) { outError = "ShaderMeta 로드 실패: " + error; return false; }

        // base 저작본 — meta가 아는 숫자 property만 싣는다(모르는 이름은
        // 변환기가 나르지 않아 대조가 무의미해진다).
        // 게시 GUID를 먼저 만든다 — 저작 루트 가드가 preferredGuid의
        // canonical UUIDv4를 요구하고(nil은 거부), 같은 값을 저작본 assetId에도
        // 실어 자산 신원과 문서 신원을 일치시킨다.
        const FileGuid preferredGuid = FileGuid::CreateRandomV4();
        experiment::Material authored;
        authored.name = "GateAuthoredMat";
        authored.assetId.value = preferredGuid.m_guid;
        authored.shaderAssetId.value = shaderGuid.m_guid;
        authored.blendMode = experiment::MaterialBlendMode::Opaque;
        for (const ShaderPropertyDesc& desc : meta->properties)
        {
            if (desc.type != ShaderPropertyType::Float) continue;
            experiment::MaterialProperty property;
            property.name = desc.name;
            property.value = 0.25f;
            authored.properties.push_back(std::move(property));
            if (authored.properties.size() >= 2) break;
        }
        if (authored.properties.empty())
        {
            outError = "meta에 float property가 없다 — seed가 diff를 만들 수 없다";
            return false;
        }

        // ★ texture도 실어야 c3-2의 owner 축이 실제로 무언가를 비교한다.
        //   싣지 않으면 legacy 맵도 resolver도 nullptr을 주고 "동일"로 통과한다
        //   (nullptr끼리 비교하는 눈먼 초록 — 실제로 한 번 그렇게 나왔다).
        const file::path texturePath =
            PathFinder::Relative("Materials\\") / "Cube_Mat_BaseColor.png";
        const FileGuid textureGuid = DataSystems->GetFileGuid(texturePath);
        if (FileGuid{} == textureGuid)
        {
            outError = "seed 텍스처 GUID 미해석: " + texturePath.string();
            return false;
        }
        for (const ShaderPropertyDesc& desc : meta->properties)
        {
            if (ShaderPropertyType::Texture2D != desc.type) continue;
            experiment::MaterialProperty property;
            property.name = desc.name;
            experiment::TextureReference reference;
            reference.assetId.value = textureGuid.m_guid;
            property.value = std::move(reference);
            authored.properties.push_back(std::move(property));
            // I5-D5c5 — 슬롯을 **전부** 싣는다. 한 슬롯(baseColorMap)만
            // 실으면 normalMap이 비어 useNormalMap 유도가 늘 0이 되고, 그
            // 축은 "0과 0을 비교해 통과"가 된다(c3-2가 owner에서 겪은 눈먼
            // 초록의 같은 형태). legacy 사본은 이 저작본을 변환해 만들므로
            // 두 경로가 같은 텍스처를 갖고, owner 대조는 그대로 성립한다.
        }

        YAML::Node document;
        if (!experiment::SerializeMaterialAuthoring(authored, document, error))
        {
            outError = "저작 인코딩 실패: " + error;
            return false;
        }
        YAML::Emitter emitter;
        emitter << document;
        const file::path destination =
            directory / (authored.name + ".asset");
        const FileGuid baseGuid = AssetAuthoringPort::WriteTextAssetWithMeta(
            destination, std::string(emitter.c_str()), preferredGuid);
        if (FileGuid{} == baseGuid)
        {
            outError = "자산 게시 거부(저작 루트 가드): " + destination.string();
            return false;
        }

        // 링크 대상은 씬의 첫 MeshRenderer다. legacy 사본은 base를 변환해
        // 만든다 — 저작 경계의 ref 읽기와 같은 규약(base 소유 사본+override).
        MeshRenderer* target = nullptr;
        for (const auto& object : scene.m_Entities)
        {
            if (!object || object->IsDestroyMark()) continue;
            if (MeshRenderer* candidate = object->GetComponent<MeshRenderer>())
            {
                target = candidate;
                break;
            }
        }
        if (nullptr == target) { outError = "씬에 MeshRenderer가 없다"; return false; }

        auto owned = std::make_shared<Material>();
        if (!ExperimentMaterialMigration::ConvertToLegacyMaterial(authored,
            meta.get(), *owned, error))
        {
            outError = "base legacy 변환 실패: " + error;
            return false;
        }
        // override 하나 — 이것이 씬에 diff로 남아야 ref 표기가 성립한다.
        experiment::MaterialProperty override;
        override.name = authored.properties.front().name;
        override.value = 0.75f;
        if (!ExperimentMaterialMigration::ApplyPropertyToLegacy(*owned,
            override, error))
        {
            outError = "override 적용 실패: " + error;
            return false;
        }
        owned->m_name = authored.name;
        DataSystems->FinalizeMaterialRuntime(*owned);
        target->m_Material = std::move(owned);
        target->m_materialBaseGuid = baseGuid;
        std::printf("[CLI] experiment.matruntime seed done base=%s "
            "property=%s owner=%s\n", authored.name.c_str(),
            override.name.c_str(),
            target->GetOwner() ? target->GetOwner()->m_name.ToString().c_str()
                : "?");
        return true;
    }

    static void Cmd_experiment_matruntime(const ConsoleCommandContext& ctx)
    {
        Scene* scene = SceneManagers->GetActiveScene();
        if (nullptr == scene)
        {
            std::printf("[CLI] experiment.matruntime fail 활성 씬 없음\n");
            return;
        }

        // I5-D5c3 — 편집 반영 축. `MaterialScriptBinding.h`의 계약은 "논리 값
        // 갱신이 곧 화면 갱신"이다(M4 이후 sealing이 매 프레임 논리 값에서 CB
        // bytes를 다시 pack하므로). c2-2가 저작 정본 직행을 넣으면서 그 계약이
        // 저작 재질에서 깨졌다: 편집은 legacy를 바꾸는데 sealing은 인스턴스에서
        // 합성한 값을 쓴다. 이 축이 그 간극을 잰다 — 실물 편집 창구를 태우고
        // 인스턴스 합성 결과가 따라오는지 본다.
        if (ctx.parts.size() >= 2 && "edit" == ctx.parts[1])
        {
            MeshRenderer* target = nullptr;
            for (const auto& object : scene->m_Entities)
            {
                if (!object || object->IsDestroyMark()) continue;
                MeshRenderer* candidate = object->GetComponent<MeshRenderer>();
                if (nullptr != candidate && candidate->m_Material
                    && nullptr != candidate->GetMaterialInstance())
                {
                    target = candidate;
                    break;
                }
            }
            if (nullptr == target)
            {
                std::printf("[CLI] experiment.matruntime edit skip "
                    "저작 정본 보유 renderer 0\n");
                return;
            }

            std::string error;
            const ShaderMetaHandle handle = DataSystems->LoadShaderMetaHandle(
                target->m_Material->m_shaderMetaGuid, error);
            const std::shared_ptr<const ShaderMeta> meta =
                DataSystems->ResolveShaderMeta(handle);
            if (!meta)
            {
                std::printf("[CLI] experiment.matruntime edit fail meta=%s\n",
                    error.c_str());
                return;
            }
            std::string propertyName;
            for (const ShaderPropertyDesc& desc : meta->properties)
            {
                if (ShaderPropertyType::Float != desc.type) continue;
                propertyName = desc.name;
                break;
            }
            if (propertyName.empty())
            {
                std::printf("[CLI] experiment.matruntime edit skip "
                    "float property 0\n");
                return;
            }

            // 실물 편집 창구를 그대로 태운다 — 재구현하면 게이트가 제품이
            // 아니라 자기 사본을 재게 된다.
            constexpr float kEdited = 0.4242f;
            const bool applied = MaterialScriptBinding::SetFloat(
                *target->m_Material, propertyName, kEdited,
                target->GetMaterialInstance());
            const float legacyAfter = MaterialScriptBinding::GetFloat(
                *target->m_Material, propertyName, -1.0f);

            experiment::Material effective;
            float instanceAfter = -1.0f;
            if (target->GetMaterialInstance()->BuildEffectiveMaterial(
                effective, error))
            {
                for (const experiment::MaterialProperty& property
                    : effective.properties)
                {
                    if (property.name != propertyName) continue;
                    if (const float* value =
                        std::get_if<float>(&property.value))
                    {
                        instanceAfter = *value;
                    }
                    break;
                }
            }

            // 편집의 종착점은 프록시다 — 인스턴스만 따라오고 프록시가 옛
            // 스냅샷을 들고 있으면 화면은 여전히 안 바뀐다(c2-2가 만든 그
            // 비대칭). 갱신 커맨드를 실제로 태워 세대 기반 재스냅샷을 잰다.
            RenderScene* renderScene = SceneManagers->GetRenderScene();
            renderScene->UpdateCommand(target);
            // 큐는 렌더 스레드가 비우지만 헤드리스에는 그 틱이 없다 — 게이트가
            // 같은 소비 창구를 직접 태운다(제품과 다른 경로를 만들지 않는다).
            ProxyCommandQueue->Execute(*renderScene,
                renderScene->GetSceneEpoch());
            float proxyAfter = -1.0f;
            for (const auto& proxy :
                SceneManagers->GetRenderScene()->GetPrimitiveProxySnapshot())
            {
                auto* meshProxy = dynamic_cast<MeshRenderProxy*>(proxy.get());
                if (nullptr == meshProxy
                    || meshProxy->m_instancedID != target->GetInstanceID()
                    || !meshProxy->m_authoredMaterial)
                {
                    continue;
                }
                for (const experiment::MaterialProperty& property
                    : meshProxy->m_authoredMaterial->properties)
                {
                    if (property.name != propertyName) continue;
                    if (const float* value = std::get_if<float>(&property.value))
                    {
                        proxyAfter = *value;
                    }
                    break;
                }
                break;
            }

            const bool instanceFollowed =
                std::abs(instanceAfter - kEdited) < 1e-5f;
            const bool proxyFollowed =
                std::abs(proxyAfter - kEdited) < 1e-5f;
            std::printf("[CLI] experiment.matruntime edit %s property=%s "
                "applied=%d legacy=%.4f instance=%.4f proxy=%.4f\n",
                (applied && instanceFollowed && proxyFollowed)
                    ? "pass" : "fail",
                propertyName.c_str(), applied ? 1 : 0, legacyAfter,
                instanceAfter, proxyAfter);
            return;
        }

        if (ctx.parts.size() >= 2 && "seed" == ctx.parts[1])
        {
            if (ctx.parts.size() < 3)
            {
                std::printf("[CLI] experiment.matruntime seed <자산디렉터리>\n");
                return;
            }
            std::string error;
            if (!SeedAuthoredMaterial(*scene, file::path(ctx.parts[2]), error))
            {
                std::printf("[CLI] experiment.matruntime seed fail %s\n",
                    error.c_str());
            }
            return;
        }

        const auto encodeValue = [](const std::string& name,
            const experiment::MaterialPropertyValue& value) -> std::string
        {
            experiment::MaterialProperty property;
            property.name = name;
            property.value = value;
            YAML::Node entry;
            std::string error;
            if (!experiment::SerializeMaterialPropertyValue(property, entry,
                error))
            {
                return "<encode-fail:" + error + ">";
            }
            YAML::Emitter emitter;
            emitter << entry;
            return std::string(emitter.c_str());
        };

        std::size_t renderers = 0, withMaterial = 0, withInstance = 0;
        std::size_t compared = 0, metaMissing = 0, buildFailed = 0;
        std::size_t valueMismatch = 0, onlyAuthored = 0, onlyLegacy = 0;
        std::size_t blendMismatch = 0;
        std::size_t sealCompared = 0, sealByteMismatch = 0;
        std::size_t sealLayoutFailed = 0, sealBuildFailed = 0;
        std::string firstSealMismatch;
        std::size_t meshProxies = 0, proxyAuthored = 0, proxyValueMismatch = 0;
        std::size_t texResolved = 0, texOwnerMismatch = 0, texResolveFailed = 0;
        std::size_t texResolvedOwners = 0;
        std::size_t texCooked = 0, texSourceFallback = 0;
        std::string firstTexMismatch;
        // I5-D5c5 — 저작 단독 시공 축. 기존 2단계(legacy 시공 → 덮어쓰기)와
        // 신규 1단계(BuildSealSourceFromAuthored)가 같은 산출을 내는가.
        std::size_t sealAuthoredBuilt = 0, sealAuthoredFail = 0;
        std::size_t sealAuthoredTexMismatch = 0, sealNormalMapDerived = 0;
        std::size_t sealDeadChannelDelta = 0;
        std::string firstAuthoredSeal;
        std::string firstMismatch;

        for (const auto& object : scene->m_Entities)
        {
            if (!object || object->IsDestroyMark()) continue;
            MeshRenderer* renderer = object->GetComponent<MeshRenderer>();
            if (nullptr == renderer) continue;
            ++renderers;
            if (!renderer->m_Material) continue;
            ++withMaterial;

            experiment::MaterialInstance* instance =
                renderer->GetMaterialInstance();
            if (nullptr == instance) continue;
            ++withInstance;

            // legacy 축은 실물 변환 정본을 그대로 태운다 — 재구현하지 않는다.
            std::string error;
            const ShaderMetaHandle handle = DataSystems->LoadShaderMetaHandle(
                renderer->m_Material->m_shaderMetaGuid, error);
            const std::shared_ptr<const ShaderMeta> meta =
                DataSystems->ResolveShaderMeta(handle);
            if (!meta) { ++metaMissing; continue; }

            experiment::Material legacyRoundTrip;
            experiment::Material authoredEffective;
            if (!ExperimentMaterialMigration::ConvertLegacyMaterial(
                    *renderer->m_Material, *meta, legacyRoundTrip, error)
                || !instance->BuildEffectiveMaterial(authoredEffective, error))
            {
                ++buildFailed;
                if (firstMismatch.empty()) firstMismatch = "build:" + error;
                continue;
            }
            ++compared;

            if (legacyRoundTrip.blendMode != authoredEffective.blendMode)
            {
                ++blendMismatch;
                if (firstMismatch.empty())
                {
                    firstMismatch = "blend " + object->m_name.ToString();
                }
            }

            std::unordered_map<std::string, std::string> legacyValues;
            for (const experiment::MaterialProperty& property
                : legacyRoundTrip.properties)
            {
                legacyValues[property.name] =
                    encodeValue(property.name, property.value);
            }
            for (const experiment::MaterialProperty& property
                : authoredEffective.properties)
            {
                const auto it = legacyValues.find(property.name);
                if (it == legacyValues.end())
                {
                    ++onlyAuthored;
                    if (firstMismatch.empty())
                    {
                        firstMismatch = "onlyAuthored " + property.name;
                    }
                    continue;
                }
                if (it->second != encodeValue(property.name, property.value))
                {
                    ++valueMismatch;
                    if (firstMismatch.empty())
                    {
                        firstMismatch = "value " + property.name;
                    }
                }
                legacyValues.erase(it);
            }
            onlyLegacy += legacyValues.size();
            if (!legacyValues.empty() && firstMismatch.empty())
            {
                firstMismatch = "onlyLegacy " + legacyValues.begin()->first;
            }

            // ── D5-c2-1: packing 직전 논리 값의 바이트 A/B ──
            //
            // 이름 집합 대조(위)는 "저작에만/legacy에만"을 세지만, 누락 property가
            // ApplyDefault(ShaderMeta 기본값)로 채워지므로 **그것만으로는 c2가
            // 화면을 바꾸는지 알 수 없다**. legacy 왕복이 주입한 값과 meta
            // 기본값이 같다면 바이트는 동일하고 c2는 무해하다. 그 판정이 이
            // 축이다.
            ShaderMetaBindingLayout syntheticLayout;
            std::string layoutError;
            if (!BuildSyntheticBindingLayout(*meta, syntheticLayout, layoutError))
            {
                ++sealLayoutFailed;
                continue;
            }
            std::vector<std::uint8_t> legacyBytes, authoredBytes;
            if (!experiment::BuildMaterialPropertyBlock(legacyRoundTrip, *meta,
                    syntheticLayout, legacyBytes, error)
                || !experiment::BuildMaterialPropertyBlock(authoredEffective,
                    *meta, syntheticLayout, authoredBytes, error))
            {
                ++sealBuildFailed;
                if (firstMismatch.empty()) firstMismatch = "sealBuild:" + error;
                continue;
            }
            ++sealCompared;
            if (legacyBytes != authoredBytes)
            {
                ++sealByteMismatch;
                std::size_t firstByte = 0;
                while (firstByte < legacyBytes.size()
                    && firstByte < authoredBytes.size()
                    && legacyBytes[firstByte] == authoredBytes[firstByte])
                {
                    ++firstByte;
                }
                // 어느 property의 자리인지 되짚는다 — 숫자만으로는 c2의 위험
                // 크기를 판단할 수 없다.
                std::string culprit = "?";
                for (const ShaderMetaPropertyBinding& binding
                    : syntheticLayout.properties)
                {
                    if (RHIShaderResourceKind::ConstantBuffer
                        != binding.resourceKind) continue;
                    if (firstByte >= binding.byteOffset
                        && firstByte < binding.byteOffset + binding.byteSize)
                    {
                        culprit = binding.name;
                        break;
                    }
                }
                if (firstSealMismatch.empty())
                {
                    firstSealMismatch = culprit + "@"
                        + std::to_string(firstByte);
                }
            }

            // ── D5-c3-2: texture owner 축 ──
            //
            // sealing이 texture generation owner를 legacy 이름 맵 대신 저작
            // GUID에서(M2 resolver) 얻도록 바꿨다. 이 전환이 **그림을 바꾸지
            // 않는가**를 재는 유일한 방법은 두 경로가 같은 owner를 주는지
            // 보는 것이다 — 다른 텍스처가 오면 화면이 조용히 달라진다.
            ExperimentMaterialSealing::SealSource legacySeal;
            if (ExperimentMaterialSealing::BuildSealSourceFromLegacy(
                *renderer->m_Material, *meta, legacySeal, error))
            {
                ExperimentMaterialSealing::SealSource authoredSeal = legacySeal;
                ExperimentMaterialSealing::ApplyAuthoredMaterial(authoredSeal,
                    authoredEffective);
                std::size_t cooked = 0, sourceFallback = 0;
                if (!ExperimentMaterialSealing::ApplyAuthoredTextures(
                    authoredSeal, *meta, error, &cooked, &sourceFallback))
                {
                    ++texResolveFailed;
                    if (firstTexMismatch.empty())
                    {
                        firstTexMismatch = "resolve:" + error;
                    }
                }
                else
                {
                    texCooked += cooked;
                    texSourceFallback += sourceFallback;
                    for (const auto& legacyTexture : legacySeal.textures)
                    {
                        const auto found = std::find_if(
                            authoredSeal.textures.begin(),
                            authoredSeal.textures.end(),
                            [&](const auto& candidate)
                            {
                                return candidate.propertyName
                                    == legacyTexture.propertyName;
                            });
                        ++texResolved;
                        if (found != authoredSeal.textures.end()
                            && nullptr != found->owner)
                        {
                            ++texResolvedOwners;
                        }
                        if (found == authoredSeal.textures.end()
                            || found->owner != legacyTexture.owner)
                        {
                            ++texOwnerMismatch;
                            if (firstTexMismatch.empty())
                            {
                                firstTexMismatch = legacyTexture.propertyName;
                            }
                        }
                    }
                }

                // ── D5-c5: 저작 단독 시공 ──
                //
                // 제품 경로는 이제 legacy를 읽지 않고 저작본만으로 seal을
                // 짓는다. 그것이 **기존 2단계와 같은 산출**인지를 여기서
                // 잰다 — 갈리면 화면이 조용히 달라진다.
                ExperimentMaterialSealing::SealSource directSeal;
                std::string directError;
                if (!ExperimentMaterialSealing::BuildSealSourceFromAuthored(
                    authoredEffective, *meta, directSeal, directError))
                {
                    ++sealAuthoredFail;
                    if (firstAuthoredSeal.empty())
                        firstAuthoredSeal = "build:" + directError;
                }
                else
                {
                    ++sealAuthoredBuilt;
                    // texture owner는 두 경로가 **반드시** 같아야 한다 —
                    // 같은 resolver를 부르므로 갈리면 시공 순서 결함이다.
                    if (directSeal.textures.size() != authoredSeal.textures.size())
                    {
                        ++sealAuthoredTexMismatch;
                        if (firstAuthoredSeal.empty())
                            firstAuthoredSeal = "texCount";
                    }
                    else
                    {
                        for (std::size_t slot = 0;
                            slot < directSeal.textures.size(); ++slot)
                        {
                            if (directSeal.textures[slot].propertyName
                                    == authoredSeal.textures[slot].propertyName
                                && directSeal.textures[slot].owner
                                    == authoredSeal.textures[slot].owner)
                            {
                                continue;
                            }
                            ++sealAuthoredTexMismatch;
                            if (firstAuthoredSeal.empty())
                            {
                                firstAuthoredSeal = "tex:" +
                                    directSeal.textures[slot].propertyName;
                            }
                        }
                    }
                    // useNormalMap — 인스턴스 채널의 유일한 실소비다. 저작
                    // 유도가 실제로 1을 내야 이 축이 공허하지 않다(seed가
                    // normalMap을 싣는 이유).
                    if (0 != directSeal.useNormalMap) ++sealNormalMapDerived;
                    // 죽은 채널(baseColorFactor/metallic/roughness/flow)은
                    // **판정하지 않고 보고한다** — 제품 셰이더가 CB를 우선하므로
                    // (usePropertyBlock·useLegacyInstanceMaterial) legacy 값과
                    // 갈려도 그림이 바뀌지 않는다. 이 계수는 "legacy 값을
                    // 잃었다"는 사실의 크기다.
                    if (directSeal.baseColorFactor != legacySeal.baseColorFactor
                        || directSeal.metallic != legacySeal.metallic
                        || directSeal.roughness != legacySeal.roughness
                        || directSeal.flow.windVector != legacySeal.flow.windVector
                        || directSeal.flow.uvScroll != legacySeal.flow.uvScroll)
                    {
                        ++sealDeadChannelDelta;
                    }
                }
            }
        }

        // ── D5-c2-2: 프록시 축 ──
        //
        // sealing 직행 자체는 헤드리스 관측 밖이다: --script 라이브는 렌더
        // 0프레임이고 dx12.scene 하네스는 sealing 경로를 타지 않는다(자체 그리기).
        // 그래서 **프록시가 저작 정본을 나르는가**까지를 잰다 — 그 뒤 4줄
        // (poolMesh 반입 → ApplyAuthoredMaterial)은 코드가 미러이고, 값의 동등은
        // 위 바이트 축이 이미 증명했다. 남는 간극은 계획서에 한계로 적는다.
        //
        // 프록시가 나르는 것이 컴포넌트 인스턴스와 **같은 값**인지도 본다 —
        // 프록시는 값 스냅샷이라 인스턴스를 가리키지 않는다(렌더 스레드 안전).
        if (RenderScene* renderScene = SceneManagers->GetRenderScene())
        {
            for (const auto& proxy : renderScene->GetPrimitiveProxySnapshot())
            {
                auto* meshProxy = dynamic_cast<MeshRenderProxy*>(proxy.get());
                if (nullptr == meshProxy) continue;
                ++meshProxies;
                if (!meshProxy->m_authoredMaterial) continue;
                ++proxyAuthored;

                // 같은 renderer의 인스턴스를 찾아 값 대조한다.
                MeshRenderer* owner = nullptr;
                for (const auto& object : scene->m_Entities)
                {
                    if (!object || object->IsDestroyMark()) continue;
                    MeshRenderer* candidate = object->GetComponent<MeshRenderer>();
                    if (nullptr != candidate
                        && candidate->GetInstanceID() == meshProxy->m_instancedID)
                    {
                        owner = candidate;
                        break;
                    }
                }
                experiment::MaterialInstance* instance =
                    (nullptr != owner) ? owner->GetMaterialInstance() : nullptr;
                if (nullptr == instance) { ++proxyValueMismatch; continue; }

                experiment::Material expected;
                std::string error;
                if (!instance->BuildEffectiveMaterial(expected, error))
                {
                    ++proxyValueMismatch;
                    continue;
                }
                bool same = expected.properties.size()
                    == meshProxy->m_authoredMaterial->properties.size()
                    && expected.blendMode
                        == meshProxy->m_authoredMaterial->blendMode;
                if (same)
                {
                    for (const experiment::MaterialProperty& property
                        : expected.properties)
                    {
                        const auto it = std::find_if(
                            meshProxy->m_authoredMaterial->properties.begin(),
                            meshProxy->m_authoredMaterial->properties.end(),
                            [&](const experiment::MaterialProperty& candidate)
                            {
                                return candidate.name == property.name;
                            });
                        if (it == meshProxy->m_authoredMaterial->properties.end()
                            || encodeValue(it->name, it->value)
                                != encodeValue(property.name, property.value))
                        {
                            same = false;
                            break;
                        }
                    }
                }
                if (!same) ++proxyValueMismatch;
            }
        }

        // 판정은 **동등 축만** 한다. onlyAuthored/onlyLegacy는 왕복 손실의
        // 실측이라 여기서 붉히지 않고 계수로 보고한다 — 이 슬라이스는 손실을
        // 없애는 것이 아니라 크기를 아는 것이 목적이다(그 처방은 c2다).
        const bool covered = withInstance > 0 && compared > 0;
        // sealByteMismatch는 **판정하지 않는다** — c2가 화면을 바꾸는 폭의
        // 실측이지 결함이 아니다(단정을 걸면 c2가 고칠 때 거꾸로 붉어진다).
        // 반면 layout/build 실패는 축이 돌지 않았다는 뜻이라 붉힌다.
        const bool passed = covered && 0 == valueMismatch && 0 == blendMismatch
            && 0 == buildFailed && 0 == sealLayoutFailed && 0 == sealBuildFailed
            && sealCompared > 0 && 0 == proxyValueMismatch
            && 0 == texOwnerMismatch && 0 == texResolveFailed
            // D5-c5: 저작 단독 시공이 기존 2단계와 갈리면 실패다.
            && 0 == sealAuthoredFail && 0 == sealAuthoredTexMismatch;
        std::printf("[CLI] experiment.matruntime %s renderers=%zu "
            "withMaterial=%zu withInstance=%zu compared=%zu metaMissing=%zu "
            "buildFailed=%zu valueMismatch=%zu blendMismatch=%zu "
            "onlyAuthored=%zu onlyLegacy=%zu sealCompared=%zu "
            "sealByteMismatch=%zu sealLayoutFailed=%zu sealBuildFailed=%zu "
            "meshProxies=%zu proxyAuthored=%zu proxyValueMismatch=%zu "
            "texResolved=%zu texOwnerMismatch=%zu texResolveFailed=%zu "
            "texResolvedOwners=%zu texCooked=%zu texSourceFallback=%zu "
            "sealAuthored=%zu sealAuthoredFail=%zu sealAuthoredTexMismatch=%zu "
            "normalMapDerived=%zu deadChannelDelta=%zu%s%s%s%s%s%s%s%s\n",
            passed ? "pass" : (covered ? "fail" : "skip"),
            renderers, withMaterial, withInstance, compared, metaMissing,
            buildFailed, valueMismatch, blendMismatch, onlyAuthored, onlyLegacy,
            sealCompared, sealByteMismatch, sealLayoutFailed, sealBuildFailed,
            meshProxies, proxyAuthored, proxyValueMismatch,
            texResolved, texOwnerMismatch, texResolveFailed, texResolvedOwners,
            texCooked, texSourceFallback,
            sealAuthoredBuilt, sealAuthoredFail, sealAuthoredTexMismatch,
            sealNormalMapDerived, sealDeadChannelDelta,
            firstMismatch.empty() ? "" : " first=",
            firstMismatch.c_str(),
            firstSealMismatch.empty() ? "" : " firstSeal=",
            firstSealMismatch.c_str(),
            firstTexMismatch.empty() ? "" : " firstTex=",
            firstTexMismatch.c_str(),
            firstAuthoredSeal.empty() ? "" : " firstAuthoredSeal=",
            firstAuthoredSeal.c_str());
    }

    // I2-E — 임베디드 텍스처 신원. 소스 로드 경로가 모델 sidecar의
    // subAssets.embeddedTextures를 읽어 texture property에 실제 GUID를 싣는가.
    // 예전에는 sourcePath가 빈 임베디드 텍스처가 nil로 떨어져 변환 경계가
    // property를 **통째로 생략**했다(§1.4). 생략은 '없는 property'라 값 대조로는
    // 안 보인다 — 그래서 present/valid를 함께 센다.
    static void Cmd_experiment_embedded(const ConsoleCommandContext& ctx)
    {
        if (ctx.parts.size() < 2)
        {
            std::printf("[CLI] experiment.embedded <모델경로>\n");
            return;
        }
        const std::size_t head = ctx.line.find(ctx.parts[1]);
        std::string pathText = ctx.line.substr(head);
        while (!pathText.empty() && (pathText.back() == ' '
            || pathText.back() == '\r' || pathText.back() == '\t'))
        {
            pathText.pop_back();
        }
        const file::path modelPath = std::filesystem::path(pathText);
        const FileGuid guid = DataSystems->GetFileGuid(modelPath);
        if (FileGuid{} == guid)
        {
            std::printf("[CLI] experiment.embedded fail GUID 미해석 %s\n",
                modelPath.string().c_str());
            return;
        }
        // 제품 경로로 로드한다 — 자체 로더를 새로 만들면 게이트가 제품을
        // 재지 않는다.
        DataSystems->LoadModelGUID(guid);
        const std::shared_ptr<const experiment::Model> model =
            DataSystems->TryGetExperimentModel(guid);
        if (!model)
        {
            std::printf("[CLI] experiment.embedded fail experiment 모델 없음\n");
            return;
        }

        std::size_t textureProps = 0, validAssetId = 0, fallbackOnly = 0;
        std::string firstFallback;
        for (const experiment::Material& material : model->Materials())
        {
            for (const experiment::MaterialProperty& property
                : material.properties)
            {
                const auto* reference =
                    std::get_if<experiment::TextureReference>(&property.value);
                if (nullptr == reference) continue;
                ++textureProps;
                if (reference->assetId.IsValid()) { ++validAssetId; continue; }
                ++fallbackOnly;
                if (firstFallback.empty()) firstFallback = property.name;
            }
        }

        const bool covered = textureProps > 0;
        const bool passed = covered && 0 == fallbackOnly;
        std::printf("[CLI] experiment.embedded %s model=%s materials=%zu "
            "textureProps=%zu validAssetId=%zu fallbackOnly=%zu%s%s\n",
            passed ? "pass" : (covered ? "fail" : "skip"),
            modelPath.filename().string().c_str(), model->Materials().size(),
            textureProps, validAssetId, fallbackOnly,
            firstFallback.empty() ? "" : " first=", firstFallback.c_str());
    }

    // I7-C1 — cooked catalog 관측·마운트. 굽는 쪽(AssetCooker → Derived/ +
    // CEMF → pak)은 D5-b2c에서 다 섰는데 읽는 쪽이 이어져 있지 않아 cooked
    // 경로가 제품에서 한 번도 돌지 않았다(실측 texCooked=0). 이 명령이
    //   ① 기동 마운트 상태를 보이고
    //   ② 게이트가 임시 Derived 트리를 명시적으로 마운트할 창구가 된다.
    // 저작 트리에는 Derived가 없으므로 인자 없는 실행은 미게시(skip)가 정상이다.
    static void Cmd_experiment_catalog(const ConsoleCommandContext& ctx)
    {
        namespace ck = experiment::cooked;
        // 경로에 공백이 있을 수 있어 토큰이 아니라 원문에서 잘라 쓴다.
        if (ctx.parts.size() >= 3 && ctx.parts[1] == "mount")
        {
            const std::size_t head = ctx.line.find("mount");
            std::string rootText = ctx.line.substr(head + 5);
            while (!rootText.empty() && (rootText.front() == ' '
                || rootText.front() == '	')) rootText.erase(0, 1);
            while (!rootText.empty() && (rootText.back() == ' '
                || rootText.back() == '' || rootText.back() == '	'))
            {
                rootText.pop_back();
            }
            const file::path root = std::filesystem::path(rootText);
            std::string error;
            const bool mounted = DataSystems->MountCookedCatalog(root, error);
            std::printf("[CLI] experiment.catalog mount %s root=%s entries=%zu stale=%zu%s%s\n",
                mounted ? "pass" : "fail", root.string().c_str(),
                DataSystems->CookedCatalogEntryCount(),
                DataSystems->CookedCatalogStaleCount(),
                error.empty() ? "" : " error=", error.c_str());
            return;
        }

        // I7-C1 — 제품 바인딩 소비 프로브. "표가 섰다"와 "resolver가 cooked를
        // 골랐다"는 다르다. 실제 texture 자산 경로를 받아 제품 서비스
        // (MakeDataSystemMaterialResolveServices + 마운트된 catalog)로 해석하고
        // cookedTextures 계수를 낸다 — sealing이 매 프레임 타는 그 경로다.
        if (ctx.parts.size() >= 3 && ctx.parts[1] == "probe")
        {
            const auto mounted = DataSystems->GetCookedCatalog();
            const std::size_t head = ctx.line.find("probe");
            std::string pathText = ctx.line.substr(head + 5);
            while (!pathText.empty() && (pathText.front() == ' '
                || pathText.front() == '\t')) pathText.erase(0, 1);
            while (!pathText.empty() && (pathText.back() == ' '
                || pathText.back() == '\r' || pathText.back() == '\t'))
            {
                pathText.pop_back();
            }
            const FileGuid textureGuid =
                DataSystems->GetFileGuid(std::filesystem::path(pathText));
            const file::path metaPath = PathFinder::Relative("Shaders\\")
                / "DefaultPassShader" / "GBuffer.shadermeta";
            const FileGuid shaderGuid = DataSystems->GetFileGuid(metaPath);
            if (FileGuid{} == textureGuid || FileGuid{} == shaderGuid)
            {
                std::printf("[CLI] experiment.catalog probe fail GUID 미해석 "
                    "texture=%d shader=%d\n",
                    FileGuid{} != textureGuid, FileGuid{} != shaderGuid);
                return;
            }

            std::string metaError;
            const ShaderMetaHandle metaHandle =
                DataSystems->LoadShaderMetaHandle(shaderGuid, metaError);
            const std::shared_ptr<const ShaderMeta> meta =
                DataSystems->ResolveShaderMeta(metaHandle);
            if (!meta)
            {
                std::printf("[CLI] experiment.catalog probe fail meta %s\n",
                    metaError.c_str());
                return;
            }

            experiment::Material material;
            material.name = "CookedProbe";
            material.shaderAssetId.value = shaderGuid.m_guid;
            experiment::MaterialProperty property;
            property.name = std::string(
                standard_material::property::BaseColorMap);
            experiment::TextureReference reference;
            reference.assetId.value = textureGuid.m_guid;
            property.value = std::move(reference);
            material.properties.push_back(std::move(property));

            // ★ 제품 sealing이 타는 **그 함수**를 부른다. resolver를 직접
            //   부르면 "binding이 catalog를 쓴다"까지만 증명되고, sealing이
            //   그것을 넘기는지는 안 재진다 — 여기가 실제 소비 지점이다.
            ExperimentMaterialSealing::SealSource source;
            source.material = std::move(material);
            std::size_t cooked = 0, sourceFallback = 0;
            std::string error;
            const bool ok = ExperimentMaterialSealing::ApplyAuthoredTextures(
                source, *meta, error, &cooked, &sourceFallback);
            std::size_t owners = 0;
            for (const auto& texture : source.textures)
            {
                if (nullptr != texture.owner) ++owners;
            }
            std::printf("[CLI] experiment.catalog probe %s catalog=%d "
                "textures=%zu cooked=%zu sourceFallback=%zu%s%s\n",
                (ok && owners > 0) ? "pass" : "fail",
                nullptr != mounted, owners, cooked, sourceFallback,
                error.empty() ? "" : " error=", error.c_str());
            return;
        }

        const auto catalog = DataSystems->GetCookedCatalog();
        if (!catalog)
        {
            // 미게시는 결함이 아니다 — 저작 트리의 정상 상태다.
            std::printf("[CLI] experiment.catalog skip 미게시(Derived 없음)\n");
            return;
        }

        // 씬의 모델 GUID가 실제로 cooked artifact로 해석되는가 — 표가 서 있어도
        // 제품 신원과 맞물리지 않으면 cookedPath는 여전히 빈 경로다.
        std::size_t modelsProbed = 0, modelsResolved = 0;
        if (Scene* scene = SceneManagers->GetActiveScene())
        {
            std::unordered_set<std::uint64_t> seen;
            for (const auto& object : scene->m_Entities)
            {
                if (!object || object->IsDestroyMark()) continue;
                MeshRenderer* renderer = object->GetComponent<MeshRenderer>();
                if (nullptr == renderer) continue;
                if (FileGuid{} == renderer->m_modelGuid) continue;
                if (!seen.insert(renderer->m_modelGuid.m_guid.data[0]).second)
                {
                    continue;
                }
                ++modelsProbed;
                experiment::AssetId id;
                id.value = renderer->m_modelGuid.m_guid;
                if (!catalog->ResolveArtifactPath(id).empty()) ++modelsResolved;
            }
        }

        std::printf("[CLI] experiment.catalog pass entries=%zu models=%zu "
            "materials=%zu textures=%zu shaderMetas=%zu scenes=%zu prefabs=%zu "
            "modelsProbed=%zu modelsResolved=%zu stale=%zu root=%s\n",
            catalog->Size(),
            catalog->CountOfKind(ck::CookedAssetKind::Model),
            catalog->CountOfKind(ck::CookedAssetKind::Material),
            catalog->CountOfKind(ck::CookedAssetKind::Texture),
            catalog->CountOfKind(ck::CookedAssetKind::ShaderMeta),
            catalog->CountOfKind(ck::CookedAssetKind::Scene),
            catalog->CountOfKind(ck::CookedAssetKind::Prefab),
            modelsProbed, modelsResolved, DataSystems->CookedCatalogStaleCount(),
            catalog->DerivedRoot().string().c_str());
    }

    static void Cmd_experiment_matresolve(const ConsoleCommandContext& ctx)
    {
        // I5-M2 — MaterialResolver. 합성(가짜 서비스·호출 계수) + 실사(DataSystem
        // 바인딩) 두 leg 다 돈다.
        (void)ctx;
        std::string log;
        bool passed = RenderTest::RunExperimentMaterialResolveSelfTest(log);
        passed = RenderTest::RunExperimentMaterialResolveReal(log) && passed;

        std::printf("%s", log.c_str());
        if (passed)
        {
            Debug->LogWarning(std::string("[experiment.matresolve] 통과\n") + log);
        }
        else
        {
            Debug->LogError(std::string("[experiment.matresolve] 실패\n") + log);
        }
        std::printf("[CLI] experiment.matresolve %s\n", passed ? "통과" : "실패");
    }

    static void Cmd_experiment_matinstance(const ConsoleCommandContext& ctx)
    {
        // I5-M3 — MaterialInstance. base+override 합성·불변성·CB bytes 동등.
        (void)ctx;
        std::string log;
        const bool passed =
            RenderTest::RunExperimentMaterialInstanceSelfTest(log);

        std::printf("%s", log.c_str());
        if (passed)
        {
            Debug->LogWarning(std::string("[experiment.matinstance] 통과\n") + log);
        }
        else
        {
            Debug->LogError(std::string("[experiment.matinstance] 실패\n") + log);
        }
        std::printf("[CLI] experiment.matinstance %s\n", passed ? "통과" : "실패");
    }

    static void Cmd_experiment_matseal(const ConsoleCommandContext& ctx)
    {
        // I5-M4 — sealing 브리지 패리티. 픽셀 게이트가 못 밟는 MaterialInfo
        // 폴백 경로를 직접 잰다.
        (void)ctx;
        std::string log;
        const bool passed = RenderTest::RunExperimentMaterialSealSelfTest(log);

        std::printf("%s", log.c_str());
        if (passed)
        {
            Debug->LogWarning(std::string("[experiment.matseal] 통과\n") + log);
        }
        else
        {
            Debug->LogError(std::string("[experiment.matseal] 실패\n") + log);
        }
        std::printf("[CLI] experiment.matseal %s\n", passed ? "통과" : "실패");
    }

    static void Cmd_experiment_matcodec(const ConsoleCommandContext& ctx)
    {
        // I5-M5 S0 — experiment 저작 YAML 코덱. 왕복·골든·fail-closed.
        (void)ctx;
        std::string log;
        const bool passed = RenderTest::RunExperimentMaterialCodecSelfTest(log);

        std::printf("%s", log.c_str());
        if (passed)
        {
            Debug->LogWarning(std::string("[experiment.matcodec] 통과\n") + log);
        }
        else
        {
            Debug->LogError(std::string("[experiment.matcodec] 실패\n") + log);
        }
        std::printf("[CLI] experiment.matcodec %s\n", passed ? "통과" : "실패");
    }

    static void Cmd_experiment_matmigrate(const ConsoleCommandContext& ctx)
    {
        // I5-M5 S1 — legacy ↔ experiment 변환 정본 + DataSystem 읽기 이중화.
        (void)ctx;
        std::string log;
        bool passed = RenderTest::RunExperimentMaterialMigrateSelfTest(log);
        passed = RenderTest::RunExperimentMaterialMigrateReal(log) && passed;

        std::printf("%s", log.c_str());
        if (passed)
        {
            Debug->LogWarning(std::string("[experiment.matmigrate] 통과\n") + log);
        }
        else
        {
            Debug->LogError(std::string("[experiment.matmigrate] 실패\n") + log);
        }
        std::printf("[CLI] experiment.matmigrate %s\n", passed ? "통과" : "실패");
    }

    static void Cmd_experiment_matscript(const ConsoleCommandContext& ctx)
    {
        // I5-M5 S3 — CLR property API의 논리 값 경로.
        (void)ctx;
        std::string log;
        bool passed = RenderTest::RunExperimentMaterialScriptSelfTest(log);
        passed = RenderTest::RunExperimentMaterialScriptReal(log) && passed;

        std::printf("%s", log.c_str());
        if (passed)
        {
            Debug->LogWarning(std::string("[experiment.matscript] 통과\n") + log);
        }
        else
        {
            Debug->LogError(std::string("[experiment.matscript] 실패\n") + log);
        }
        std::printf("[CLI] experiment.matscript %s\n", passed ? "통과" : "실패");
    }

    static void Cmd_experiment_scenecook(const ConsoleCommandContext& ctx)
    {
        std::string log;
        bool passed = RenderTest::RunExperimentSceneCookSelfTest(log);
        if (ctx.parts.size() > 2)
        {
            const bool real = RenderTest::RunExperimentSceneCookReal(
                ctx.parts[1], ctx.parts[2], log);
            passed = passed && real;
        }

        std::printf("%s", log.c_str());
        if (passed)
        {
            Debug->LogWarning(std::string("[experiment.scenecook] 통과\n") + log);
        }
        else
        {
            Debug->LogError(std::string("[experiment.scenecook] 실패\n") + log);
        }
        std::printf("[CLI] experiment.scenecook %s\n", passed ? "통과" : "실패");
    }

    static void Cmd_experiment_resolver(const ConsoleCommandContext&)
    {
        // 자산을 읽지 않는다. 가짜 decoder 로 호출 순서와 폴백 관측을 본다.
        std::string log;
        const bool passed = RenderTest::RunExperimentResolverSelfTest(log);

        std::printf("%s", log.c_str());
        if (passed)
        {
            Debug->LogWarning(std::string("[experiment.resolver] 통과\n") + log);
        }
        else
        {
            Debug->LogError(std::string("[experiment.resolver] 실패\n") + log);
        }
        std::printf("[CLI] experiment.resolver %s\n", passed ? "통과" : "실패");
    }

    static void Cmd_experiment_bench(const ConsoleCommandContext& ctx)
    {
        const std::vector<std::string>& parts = ctx.parts;

        // legacy 로드 대 Experiment 경계 비용 측정. CPU 전용 — 게임 스레드 실행.
        if (parts.size() < 2)
        {
            Debug->LogWarning("[experiment.bench] 사용법: experiment.bench <모델 경로> [반복수]");
            std::printf("[CLI] experiment.bench 사용법: experiment.bench <모델 경로> [반복수]\n");
            return;
        }
        const std::string& modelPath = parts[1];
        int iterations = 5;
        if (parts.size() > 2)
        {
            iterations = std::atoi(parts[2].c_str());
            if (iterations <= 0) iterations = 5;
        }

        std::string log;
        const bool passed =
            RenderTest::RunExperimentModelBenchmark(modelPath, iterations, log);

        std::printf("%s", log.c_str());
        if (passed)
        {
            Debug->LogWarning(std::string("[experiment.bench] 완료\n") + log);
        }
        else
        {
            Debug->LogError(std::string("[experiment.bench] 실패\n") + log);
        }
        std::printf("[CLI] experiment.bench %s → %s\n",
            passed ? "완료" : "실패", modelPath.c_str());
    }

    static void Cmd_vk_selftest(const ConsoleCommandContext& ctx)
    {
        const std::vector<std::string>& parts = ctx.parts;

        // Vulkan 골격 자가 검증. 자체 인스턴스·디바이스로 돌므로 DX12 렌더러와
        // 충돌하지 않는다 — dx12.selftest 와 같은 이유다.
        //
        // ★ 이 검사가 재는 것은 '삼각형이 나왔는가'가 아니라 **계약이
        //   맞는가**다. 처음 만들 때는 "Vulkan 이 구현할 수 있는 인터페이스가
        //   IRHIDeviceResources 하나뿐"이라고 여기 적혀 있었다 — 5 가 끝나며
        //   6/7 이 됐고, 검사도 골격 전용 패스 대신 중립 계약
        //   (IRenderDeviceServices + RHIEncoder)으로 그린다. 실제 패스의
        //   대조는 vk.grid 가 한다.
        const std::string outputPath = ResolveTestArtifactPath("Vulkan",
            (parts.size() > 1) ? parts[1] : std::string("vk_selftest.png"));

        std::string log;
        const bool passed = RunVulkanSelfTest(outputPath, log);

        std::printf("%s", log.c_str());
        Debug->LogWarning(std::string("[vk.selftest] ") + (passed ? "통과" : "실패") + "\n" + log);
        std::printf("[CLI] vk.selftest %s → %s\n", passed ? "통과" : "실패", outputPath.c_str());
    }

    static void Cmd_vk_grid(const ConsoleCommandContext& ctx)
    {
        // 그리드 패스를 Vulkan 으로 (5d). EnhancedGridPass 를 한 줄도 안 고치고
        // 돌려 dx12.grid 기준선과 픽셀 대조한다 — 지표 ②(공유 패스)와
        // ③(픽셀 대조)이 처음으로 0 을 벗어나는 검사다.
        std::string log;
        const bool passed = RunVulkanGridTest(log);

        std::printf("%s", log.c_str());
        Debug->LogWarning(std::string("[vk.grid] ") + (passed ? "통과" : "실패") + "\n" + log);
        std::printf("[CLI] vk.grid %s\n", passed ? "통과" : "실패");
    }

    static void Cmd_vk_parallel(const ConsoleCommandContext& ctx)
    {
        std::string log;
        const bool passed = RunVulkanParallelRecordingTest(log);

        std::printf("%s", log.c_str());
        Debug->LogWarning(std::string("[vk.parallel] ")
            + (passed ? "통과" : "실패") + "\n" + log);
        std::printf("[CLI] vk.parallel %s\n", passed ? "통과" : "실패");
    }

    static void Cmd_vk_skybox(const ConsoleCommandContext& ctx)
    {
        // EnhancedSkyBoxPass를 그대로 돌려 b0 + t0 큐브 SRV + 정적 s0가
        // DX12 검사와 같은 면 색을 내는지 대조한다.
        std::string log;
        const bool passed = RunVulkanSkyBoxTest(log);

        std::printf("%s", log.c_str());
        Debug->LogWarning(std::string("[vk.skybox] ") + (passed ? "통과" : "실패") + "\n" + log);
        std::printf("[CLI] vk.skybox %s\n", passed ? "통과" : "실패");
    }

    static void Cmd_vk_ibl(const ConsoleCommandContext& ctx)
    {
        std::string log;
        const bool passed = RunVulkanIBLTest(log);

        std::printf("%s", log.c_str());
        Debug->LogWarning(std::string("[vk.ibl] ") +
            (passed ? "통과" : "실패") + "\n" + log);
        std::printf("[CLI] vk.ibl %s\n", passed ? "통과" : "실패");
    }

    static void Cmd_vk_gizmoicon(const ConsoleCommandContext& ctx)
    {
        std::string log;
        const bool passed = RunVulkanGizmoIconTest(log);

        std::printf("%s", log.c_str());
        Debug->LogWarning(std::string("[vk.gizmoicon] ") +
            (passed ? "통과" : "실패") + "\n" + log);
        std::printf("[CLI] vk.gizmoicon %s\n", passed ? "통과" : "실패");
    }

    static void Cmd_vk_gizmoline(const ConsoleCommandContext& ctx)
    {
        std::string log;
        const bool passed = RunVulkanGizmoLineTest(log);

        std::printf("%s", log.c_str());
        Debug->LogWarning(std::string("[vk.gizmoline] ") +
            (passed ? "통과" : "실패") + "\n" + log);
        std::printf("[CLI] vk.gizmoline %s\n", passed ? "통과" : "실패");
    }

    static void Cmd_vk_wireframe(const ConsoleCommandContext& ctx)
    {
        std::string log;
        const bool passed = RunVulkanWireFrameTest(log);

        std::printf("%s", log.c_str());
        Debug->LogWarning(std::string("[vk.wireframe] ") +
            (passed ? "통과" : "실패") + "\n" + log);
        std::printf("[CLI] vk.wireframe %s\n", passed ? "통과" : "실패");
    }

    static void Cmd_vk_ui(const ConsoleCommandContext& ctx)
    {
        std::string log;
        const bool passed = RunVulkanUITest(log);

        std::printf("%s", log.c_str());
        Debug->LogWarning(std::string("[vk.ui] ") +
            (passed ? "통과" : "실패") + "\n" + log);
        std::printf("[CLI] vk.ui %s\n", passed ? "통과" : "실패");
    }

    static void Cmd_vk_shadow(const ConsoleCommandContext& ctx)
    {
        std::string log;
        const bool passed = RunVulkanShadowTest(log);

        std::printf("%s", log.c_str());
        Debug->LogWarning(std::string("[vk.shadow] ") +
            (passed ? "통과" : "실패") + "\n" + log);
        std::printf("[CLI] vk.shadow %s\n", passed ? "통과" : "실패");
    }

    static void Cmd_vk_gbuffer(const ConsoleCommandContext& ctx)
    {
        std::string log;
        const bool passed = RunVulkanGBufferTest(log);

        std::printf("%s", log.c_str());
        Debug->LogWarning(std::string("[vk.gbuffer] ") +
            (passed ? "통과" : "실패") + "\n" + log);
        std::printf("[CLI] vk.gbuffer %s\n", passed ? "통과" : "실패");
    }

    static void Cmd_vk_forward(const ConsoleCommandContext& ctx)
    {
        std::string result;
        const bool passed = RunVulkanForwardTest(result);
        std::printf("%s", result.c_str());
        Debug->LogWarning(std::string("[vk.forward] ") +
            (passed ? "통과\n" : "실패\n") + result);
        std::printf("[CLI] vk.forward %s\n", passed ? "통과" : "실패");
    }

    static void Cmd_vk_deferred(const ConsoleCommandContext& ctx)
    {
        std::string result;
        const bool passed = RunVulkanDeferredTest(result);
        Debug->LogWarning(std::string("[vk.deferred] ") +
            (passed ? "통과\n" : "실패\n") + result);
        std::printf("[CLI] vk.deferred %s\n", passed ? "통과" : "실패");
    }

    static void Cmd_vk_decal(const ConsoleCommandContext& ctx)
    {
        std::string result;
        const bool passed = RunVulkanDecalTest(result);
        Debug->LogWarning(std::string("[vk.decal] ") +
            (passed ? "통과\n" : "실패\n") + result);
        std::printf("[CLI] vk.decal %s\n", passed ? "통과" : "실패");
    }

    static void Cmd_vk_ssao(const ConsoleCommandContext& ctx)
    {
        std::string result;
        const bool passed = RunVulkanSSAOTest(result);
        Debug->LogWarning(std::string("[vk.ssao] ") +
            (passed ? "통과\n" : "실패\n") + result);
        std::printf("[CLI] vk.ssao %s\n", passed ? "통과" : "실패");
    }

    static void Cmd_vk_sss(const ConsoleCommandContext& ctx)
    {
        std::string result;
        const bool passed = RunVulkanSSSTest(result);
        std::printf("%s", result.c_str());
        Debug->LogWarning(std::string("[vk.sss] ") +
            (passed ? "통과\n" : "실패\n") + result);
        std::printf("[CLI] vk.sss %s\n", passed ? "통과" : "실패");
    }

    static void Cmd_vk_ssr(const ConsoleCommandContext& ctx)
    {
        std::string result;
        const bool passed = RunVulkanSSRTest(result);
        std::printf("%s", result.c_str());
        Debug->LogWarning(std::string("[vk.ssr] ") +
            (passed ? "통과\n" : "실패\n") + result);
        std::printf("[CLI] vk.ssr %s\n", passed ? "통과" : "실패");
    }

    static void Cmd_vk_fog(const ConsoleCommandContext& ctx)
    {
        std::string result;
        const bool passed = RunVulkanVolumetricFogTest(result);
        std::printf("%s", result.c_str());
        Debug->LogWarning(std::string("[vk.fog] ") +
            (passed ? "통과\n" : "실패\n") + result);
        std::printf("[CLI] vk.fog %s\n", passed ? "통과" : "실패");
    }

    static void Cmd_vk_post(const ConsoleCommandContext& ctx)
    {
        std::string result;
        const bool passed = RunVulkanPostChainTest(result);
        std::printf("%s", result.c_str());
        Debug->LogWarning(std::string("[vk.post] ") +
            (passed ? "통과\n" : "실패\n") + result);
        std::printf("[CLI] vk.post %s\n", passed ? "통과" : "실패");
    }

    static void Cmd_vk_ssgi(const ConsoleCommandContext& ctx)
    {
        std::string result;
        const bool passed = RunVulkanSSGITest(result);
        Debug->LogWarning(std::string("[vk.ssgi] ") +
            (passed ? "통과\n" : "실패\n") + result);
        std::printf("[CLI] vk.ssgi %s\n", passed ? "통과" : "실패");
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

    static void Cmd_dx12_psocache(const ConsoleCommandContext& ctx)
    {
        const std::vector<std::string>& parts = ctx.parts;

        // PSO 캐시 자가 검증(PHASE 3-4) — 매니저를 두 번 세워 캐시가 컴파일을
        // 실제로 없애는지 확인한다.
        const std::string cachePath = ResolveTestArtifactPath("DX12/Cache",
            (parts.size() > 1) ? parts[1] : std::string("dx12_pso.cache"));

        std::string log;
        const bool passed = DX12Test::RunPsoCacheTest(cachePath, log);

        std::printf("%s", log.c_str());
        Debug->LogWarning(std::string("[dx12.psocache] ") + (passed ? "통과" : "실패") + "\n" + log);
        std::printf("[CLI] dx12.psocache %s\n", passed ? "통과" : "실패");
    }

    static void Cmd_rhi_uploadsegments(const ConsoleCommandContext& ctx)
    {
        std::string dx12Log;
        const bool dx12Passed = DX12Test::RunUploadSegmentTest(dx12Log);

        std::string vkLog;
        const std::string vkOutput =
            ResolveTestArtifactPath("Vulkan", "rhi_uploadsegments_vk.png");
        const bool vkPassed = RunVulkanSelfTest(vkOutput, vkLog);
        const bool passed = dx12Passed && vkPassed;

        std::printf("[DX12 upload segments]\n%s", dx12Log.c_str());
        std::printf("[Vulkan upload segments]\n%s", vkLog.c_str());
        Debug->LogWarning(std::string("[rhi.uploadsegments] ")
            + (passed ? "통과" : "실패") + "\n" + dx12Log + vkLog);
        std::printf("[CLI] rhi.uploadsegments %s (DX12=%s Vulkan=%s)\n",
            passed ? "통과" : "실패", dx12Passed ? "통과" : "실패",
            vkPassed ? "통과" : "실패");
    }

    static void Cmd_dx12_uploadring(const ConsoleCommandContext& ctx)
    {
        // 업로드 링 자가 검증(PHASE 3-3). 자체 디바이스로 돌므로 DX11 렌더
        // 스레드와 충돌하지 않는다.
        std::string log;
        const bool passed = DX12Test::RunUploadSegmentTest(log);

        std::printf("%s", log.c_str());
        Debug->LogWarning(std::string("[dx12.uploadring] ") + (passed ? "통과" : "실패") + "\n" + log);
        std::printf("[CLI] dx12.uploadring %s\n", passed ? "통과" : "실패");
    }

    static void Cmd_dx12_forward(const ConsoleCommandContext& ctx)
    {
        std::string log;
        const bool passed = DX12Test::RunForwardPlusTest(log);
        const std::string verdict = passed ? "통과" : "실패";

        std::printf("%s", log.c_str());
        Debug->LogWarning("[dx12.forward] " + verdict + "\n" + log);
        std::printf("[CLI] dx12.forward %s\n", verdict.c_str());
    }

    static void Cmd_dx12_forwardshade(const ConsoleCommandContext& ctx)
    {
        std::string log;
        const bool passed = DX12Test::RunForwardPlusShadeTest(log);
        const std::string verdict = passed ? "통과" : "실패";

        std::printf("%s", log.c_str());
        Debug->LogWarning("[dx12.forwardshade] " + verdict + "\n" + log);
        std::printf("[CLI] dx12.forwardshade %s\n", verdict.c_str());
    }

    static void Cmd_dx12_forwardscale(const ConsoleCommandContext& ctx)
    {
        std::string log;
        const bool passed = DX12Test::RunForwardPlusScaleTest(log);
        const std::string verdict = passed ? "완료" : "실패";

        std::printf("%s", log.c_str());
        Debug->LogWarning("[dx12.forwardscale] " + verdict + "\n" + log);
        std::printf("[CLI] dx12.forwardscale %s\n", verdict.c_str());
    }

    static void Cmd_dx12_ssao(const ConsoleCommandContext& ctx)
    {
        std::string log;
        const bool passed = DX12Test::RunSSAOTest(log);
        const std::string verdict = passed ? "통과" : "실패";

        std::printf("%s", log.c_str());
        Debug->LogWarning("[dx12.ssao] " + verdict + "\n" + log);
        std::printf("[CLI] dx12.ssao %s\n", verdict.c_str());
    }

    static void Cmd_dx12_ssaoscale(const ConsoleCommandContext& ctx)
    {
        std::string log;
        const bool passed = DX12Test::RunSSAOScaleTest(log);
        const std::string verdict = passed ? "완료" : "실패";

        std::printf("%s", log.c_str());
        Debug->LogWarning("[dx12.ssaoscale] " + verdict + "\n" + log);
        std::printf("[CLI] dx12.ssaoscale %s\n", verdict.c_str());
    }

    static void Cmd_dx12_post(const ConsoleCommandContext& ctx)
    {
        std::string log;
        const bool passed = DX12Test::RunPostChainTest(log);
        const std::string verdict = passed ? "통과" : "실패";

        std::printf("%s", log.c_str());
        Debug->LogWarning("[dx12.post] " + verdict + "\n" + log);
        std::printf("[CLI] dx12.post %s\n", verdict.c_str());
    }

    static void Cmd_dx12_postscale(const ConsoleCommandContext& ctx)
    {
        std::string log;
        const bool passed = DX12Test::RunPostChainScaleTest(log);
        const std::string verdict = passed ? "완료" : "실패";

        std::printf("%s", log.c_str());
        Debug->LogWarning("[dx12.postscale] " + verdict + "\n" + log);
        std::printf("[CLI] dx12.postscale %s\n", verdict.c_str());
    }

    static void Cmd_dx12_ui(const ConsoleCommandContext& ctx)
    {
        std::string log;
        const bool passed = DX12Test::RunUITest(log);
        const std::string verdict = passed ? "통과" : "실패";

        std::printf("%s", log.c_str());
        Debug->LogWarning("[dx12.ui] " + verdict + "\n" + log);
        std::printf("[CLI] dx12.ui %s\n", verdict.c_str());
    }

    static void Cmd_dx12_grid(const ConsoleCommandContext& ctx)
    {
        // 그리드 패스 검증(PHASE 3-6, Gizmo 계열 첫 슬라이스).
        std::string log;
        const bool passed = DX12Test::RunGridTest(log);
        const std::string verdict = passed ? "통과" : "실패";

        std::printf("%s", log.c_str());
        Debug->LogWarning("[dx12.grid] " + verdict + "\n" + log);
        std::printf("[CLI] dx12.grid %s\n", verdict.c_str());
    }

    static void Cmd_dx12_gizmoline(const ConsoleCommandContext& ctx)
    {
        // 기즈모 라인 패스 검증(PHASE 3-6, Gizmo 계열 2차 슬라이스).
        std::string log;
        const bool passed = DX12Test::RunGizmoLineTest(log);
        const std::string verdict = passed ? "통과" : "실패";

        std::printf("%s", log.c_str());
        Debug->LogWarning("[dx12.gizmoline] " + verdict + "\n" + log);
        std::printf("[CLI] dx12.gizmoline %s\n", verdict.c_str());
    }

    static void Cmd_dx12_gizmoicon(const ConsoleCommandContext& ctx)
    {
        // 기즈모 아이콘 패스 검증(PHASE 3-6, Gizmo 계열 3차 슬라이스).
        std::string log;
        const bool passed = DX12Test::RunGizmoIconTest(log);
        const std::string verdict = passed ? "통과" : "실패";

        std::printf("%s", log.c_str());
        Debug->LogWarning("[dx12.gizmoicon] " + verdict + "\n" + log);
        std::printf("[CLI] dx12.gizmoicon %s\n", verdict.c_str());
    }

    static void Cmd_dx12_wireframe(const ConsoleCommandContext& ctx)
    {
        // 와이어프레임 패스 검증(PHASE 3-6, Gizmo 계열 4차 슬라이스).
        std::string log;
        const bool passed = DX12Test::RunWireFrameTest(log);
        const std::string verdict = passed ? "통과" : "실패";

        std::printf("%s", log.c_str());
        Debug->LogWarning("[dx12.wireframe] " + verdict + "\n" + log);
        std::printf("[CLI] dx12.wireframe %s\n", verdict.c_str());
    }

    static void Cmd_dx12_gizmoscene(const ConsoleCommandContext& ctx)
    {
        // Gizmo 계열 씬 연결 검증(PHASE 3-6, Gizmo 계열 5차 슬라이스).
        std::string log;
        const bool passed = DX12Test::RunGizmoSceneTest(log);
        const std::string verdict = passed ? "통과" : "실패";

        std::printf("%s", log.c_str());
        Debug->LogWarning("[dx12.gizmoscene] " + verdict + "\n" + log);
        std::printf("[CLI] dx12.gizmoscene %s\n", verdict.c_str());
    }

    static void Cmd_dx12_shadowquality(const ConsoleCommandContext& ctx)
    {
        // 그림자 품질 검증(PHASE 3-6 — 경사 비례 편향·캐스케이드 경계 블렌딩).
        std::string log;
        const bool passed = DX12Test::RunShadowQualityTest(log);
        const std::string verdict = passed ? "통과" : "실패";

        std::printf("%s", log.c_str());
        Debug->LogWarning("[dx12.shadowquality] " + verdict + "\n" + log);
        std::printf("[CLI] dx12.shadowquality %s\n", verdict.c_str());
    }

    static void Cmd_dx12_skybox(const ConsoleCommandContext& ctx)
    {
        // 스카이박스 패스 검증(PHASE 3-6).
        std::string log;
        const bool passed = DX12Test::RunSkyBoxTest(log);
        const std::string verdict = passed ? "통과" : "실패";

        std::printf("%s", log.c_str());
        Debug->LogWarning("[dx12.skybox] " + verdict + "\n" + log);
        std::printf("[CLI] dx12.skybox %s\n", verdict.c_str());
    }

    static void Cmd_dx12_ibl(const ConsoleCommandContext& ctx)
    {
        // IBL 생성 체인 검증(PHASE 3-6).
        std::string log;
        const bool passed = DX12Test::RunIBLTest(log);
        const std::string verdict = passed ? "통과" : "실패";

        std::printf("%s", log.c_str());
        Debug->LogWarning("[dx12.ibl] " + verdict + "\n" + log);
        std::printf("[CLI] dx12.ibl %s\n", verdict.c_str());
    }

    static void Cmd_dx12_sss(const ConsoleCommandContext& ctx)
    {
        // SSS 패스 검증(PHASE 3-6, 미구현 패스 이식 1차).
        std::string log;
        const bool passed = DX12Test::RunSSSTest(log);
        const std::string verdict = passed ? "통과" : "실패";

        std::printf("%s", log.c_str());
        Debug->LogWarning("[dx12.sss] " + verdict + "\n" + log);
        std::printf("[CLI] dx12.sss %s\n", verdict.c_str());
    }

    static void Cmd_dx12_decal(const ConsoleCommandContext& ctx)
    {
        // 데칼 패스 검증(PHASE 3-6, 미구현 패스 이식 2차).
        std::string log;
        const bool passed = DX12Test::RunDecalTest(log);
        const std::string verdict = passed ? "통과" : "실패";

        std::printf("%s", log.c_str());
        Debug->LogWarning("[dx12.decal] " + verdict + "\n" + log);
        std::printf("[CLI] dx12.decal %s\n", verdict.c_str());
    }

    static void Cmd_dx12_ssr(const ConsoleCommandContext& ctx)
    {
        // SSR 패스 검증(PHASE 3-6, 미구현 패스 이식 3차).
        std::string log;
        const bool passed = DX12Test::RunSSRTest(log);
        const std::string verdict = passed ? "통과" : "실패";

        std::printf("%s", log.c_str());
        Debug->LogWarning("[dx12.ssr] " + verdict + "\n" + log);
        std::printf("[CLI] dx12.ssr %s\n", verdict.c_str());
    }

    static void Cmd_dx12_fog(const ConsoleCommandContext& ctx)
    {
        // 볼류메트릭 포그 패스 검증(PHASE 3-6, 미구현 패스 이식 4차).
        std::string log;
        const bool passed = DX12Test::RunVolumetricFogTest(log);
        const std::string verdict = passed ? "통과" : "실패";

        std::printf("%s", log.c_str());
        Debug->LogWarning("[dx12.fog] " + verdict + "\n" + log);
        std::printf("[CLI] dx12.fog %s\n", verdict.c_str());
    }

    static void Cmd_dx12_skinning(const ConsoleCommandContext& ctx)
    {
        // GBuffer 스키닝 검증(PHASE 3-6).
        std::string log;
        const bool passed = DX12Test::RunSkinningTest(log);
        const std::string verdict = passed ? "통과" : "실패";

        std::printf("%s", log.c_str());
        Debug->LogWarning("[dx12.skinning] " + verdict + "\n" + log);
        std::printf("[CLI] dx12.skinning %s\n", verdict.c_str());
    }

    static void Cmd_dx12_iblshade(const ConsoleCommandContext& ctx)
    {
        // IBL 앰비언트 소비 검증(PHASE 3-6).
        std::string log;
        const bool passed = DX12Test::RunIBLShadeTest(log);
        const std::string verdict = passed ? "통과" : "실패";

        std::printf("%s", log.c_str());
        Debug->LogWarning("[dx12.iblshade] " + verdict + "\n" + log);
        std::printf("[CLI] dx12.iblshade %s\n", verdict.c_str());
    }

    static void Cmd_dx12_ssgi(const ConsoleCommandContext& ctx)
    {
        std::string log;
        const bool passed = DX12Test::RunSSGITest(log);
        const std::string verdict = passed ? "통과" : "실패";

        std::printf("%s", log.c_str());
        Debug->LogWarning("[dx12.ssgi] " + verdict + "\n" + log);
        std::printf("[CLI] dx12.ssgi %s\n", verdict.c_str());
    }

    static void Cmd_camera_editor(const ConsoleCommandContext& ctx)
    {
        const std::vector<std::string>& parts = ctx.parts;

        // camera.editor match|follow on|follow off|status
        //
        // 씬 뷰와 게임 뷰가 서로 다른 시점이면 두 그림의 차이가 시점 탓인지
        // 렌더 탓인지 갈리지 않는다. 시점을 통일해 두면 남는 차이가 곧
        // 렌더 경로의 차이다.
        const std::string action = (parts.size() >= 2) ? parts[1] : "status";

        if (action == "match")
        {
            if (ConsoleCommandSystem::MatchEditorCameraToGameCamera())
            {
                std::printf("[CLI] camera.editor match — 게임 카메라 자세로 맞춤\n");
            }
            else
            {
                std::printf("[CLI] camera.editor match 실패: 게임 카메라가 없다\n");
            }
        }
        else if (action == "follow")
        {
            const std::string mode = (parts.size() >= 3) ? parts[2] : "on";
            g_editorCameraFollowsGame = (mode != "off" && mode != "0");
            // 켤 때 한 번 맞춰 둔다 — 다음 프레임을 기다리지 않고 바로 보인다.
            if (g_editorCameraFollowsGame) ConsoleCommandSystem::MatchEditorCameraToGameCamera();
            std::printf("[CLI] camera.editor follow %s\n",
                g_editorCameraFollowsGame ? "on" : "off");
        }
        else
        {
            const Camera* editorCamera = EditorSessionState::Get().EditorCamera();
            Scene* activeScene = SceneManagers->GetActiveScene();
            CameraComponent* gameCamera = (nullptr != activeScene)
                ? activeScene->Cameras().GetPrimaryCamera() : nullptr;

            const auto describe = [](const char* label, uint64_t viewId,
                const FrameCameraSnapshot* snapshot)
            {
                if (nullptr == snapshot) { std::printf("  %s: 없음\n", label); return; }
                std::printf("  %s: view %llu · pos(%.3f %.3f %.3f)"
                    " · forward(%.3f %.3f %.3f) · fov %.1f\n",
                    label, static_cast<unsigned long long>(viewId),
                    snapshot->eyePosition.x, snapshot->eyePosition.y, snapshot->eyePosition.z,
                    snapshot->forward.x, snapshot->forward.y, snapshot->forward.z, snapshot->fov);
            };

            std::printf("[CLI] camera.editor status (follow %s)\n",
                g_editorCameraFollowsGame ? "on" : "off");
            const FrameCameraSnapshot editorSnapshot = (nullptr != editorCamera)
                ? editorCamera->CaptureFrameSnapshot() : FrameCameraSnapshot{};
            const FrameCameraSnapshot gameSnapshot = (nullptr != gameCamera)
                ? gameCamera->CaptureFrameSnapshot() : FrameCameraSnapshot{};
            describe("에디터", kEnhancedEditorViewId,
                nullptr != editorCamera ? &editorSnapshot : nullptr);
            describe("게임  ", nullptr != gameCamera ? gameCamera->GetInstanceID() : 0,
                nullptr != gameCamera ? &gameSnapshot : nullptr);
        }
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

    static void Cmd_render_livecheck(const ConsoleCommandContext& ctx)
    {
        const std::vector<std::string>& parts = ctx.parts;

        const uint32_t expectedWidth = (parts.size() >= 3)
            ? static_cast<uint32_t>((std::max)(0, std::atoi(parts[1].c_str())))
            : ScreenResizeBus::Get().GetWidth();
        const uint32_t expectedHeight = (parts.size() >= 3)
            ? static_cast<uint32_t>((std::max)(0, std::atoi(parts[2].c_str())))
            : ScreenResizeBus::Get().GetHeight();

		const RenderBackend configured = RuntimeSettings::Get().GetRenderBackend();
        const EnhancedLiveBackend scene = EnhancedSceneRenderer::GetLiveBackend();
        const bool backendMatch =
            ((RenderBackend::DX12 == configured && EnhancedLiveBackend::DX12 == scene) ||
             (RenderBackend::Vulkan == configured && EnhancedLiveBackend::Vulkan == scene)) &&
            ((RenderBackend::DX12 == configured &&
                0 == std::strcmp(GetImGuiHost().GetBackendName(), "DX12")) ||
             (RenderBackend::Vulkan == configured &&
                0 == std::strcmp(GetImGuiHost().GetBackendName(), "Vulkan")));

        std::string log;
        const bool displayPassed = EnhancedSceneRenderer::RunLiveDisplayRegression(
            expectedWidth, expectedHeight, log);
        const bool passed = backendMatch && displayPassed;
        std::printf("[render.livecheck] backend configured=%s scene=%s imgui=%s — %s\n",
            RenderBackendName(configured),
            EnhancedLiveBackend::Vulkan == scene ? "vulkan" : "dx12",
            GetImGuiHost().GetBackendName(), backendMatch ? "일치" : "불일치");
        std::printf("%s", log.c_str());
        std::printf("[CLI] render.livecheck %s\n", passed ? "통과" : "실패");
        Debug->LogWarning(std::string("[render.livecheck] ") +
            (passed ? "통과\n" : "실패\n") + log);
    }

    static void Cmd_dx12_bench11(const ConsoleCommandContext& ctx)
    {
        // DX11 vs DX12 API 오버헤드 실측 — 마이그레이션 전제 검증.
        // 전용 디바이스 둘을 새로 세우므로 에디터 씬과 무관하게 언제든 돈다.
        std::string log;
        const bool passed = DX12Test::RunApiOverheadBench(log);
        const std::string verdict = passed ? "통과" : "실패";

        std::printf("%s", log.c_str());
        Debug->LogWarning("[dx12.bench11] " + verdict + "\n" + log);
        std::printf("[CLI] dx12.bench11 %s\n", verdict.c_str());
    }

    static void Cmd_dx12_encoderbench(const ConsoleCommandContext& ctx)
    {
        // 인코더 오버헤드 실측 — R3 착수 조건(RhiBoundaryPlan §5).
        // 자체 디바이스를 세우므로 에디터 씬과 무관하게 언제든 돈다.
        // Release로 재야 의미가 있다(Debug는 검증 레이어가 vtable 비용을 덮는다).
        std::string log;
        const bool passed = DX12Test::RunEncoderOverheadBench(log);
        const std::string verdict = passed ? "완료" : "실패";

        std::printf("%s", log.c_str());
        Debug->LogWarning("[dx12.encoderbench] " + verdict + "\n" + log);
        std::printf("[CLI] dx12.encoderbench %s\n", verdict.c_str());
    }

    static void Cmd_dx12_scene(const ConsoleCommandContext& ctx)
    {
        // 씬 연결 검증(PHASE 3-6). 활성 씬의 카메라와 프록시를 DX12로 그린다.
        std::string log;
        const bool passed = DX12Test::RunSceneBindingTest(log);
        const std::string verdict = passed ? "통과" : "실패";

        std::printf("%s", log.c_str());
        Debug->LogWarning("[dx12.scene] " + verdict + "\n" + log);
        std::printf("[CLI] dx12.scene %s\n", verdict.c_str());
    }

    static void Cmd_render_rtinfo(const ConsoleCommandContext& ctx)
    {
        const std::string& line = ctx.line;

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

    static void Cmd_render_post(const ConsoleCommandContext& ctx)
    {
        // 기존 명령은 DX11 SceneRenderer가 소비하는 구 전역 설정만 바꿨다.
        // EnhancedRenderer에는 전달되지 않아 성공처럼 출력되는 무효 명령이므로
        // 새 DX12 런타임 튜닝 API가 생기기 전까지 명시적으로 차단한다.
        std::printf("[CLI] render.post — DX11 레거시 제어는 비활성화됨; Enhanced PostChain 튜닝 API가 필요하다\n");
    }

    static void Cmd_render_exposure(const ConsoleCommandContext& ctx)
    {
        // 기존 구현은 SceneRenderer가 갱신하는 DX11 ToneMapPass의 정적 값을
        // 읽었다. 단독 모드에서 그 값을 출력하면 정상처럼 보이는 오래된 0값을
        // 진단값으로 오인하게 되므로 새 DX12 계측이 붙기 전까지 차단한다.
        std::printf("[CLI] render.exposure — DX11 레거시 진단은 비활성화됨; PIX의 Enhanced PostChain을 확인한다\n");
    }

    static void Cmd_dx12_resize(const ConsoleCommandContext& ctx)
    {
        // 크기 추종 검증(해상도 슬라이스).
        std::string log;
        const bool passed = DX12Test::RunScreenResizeTest(log);
        const std::string verdict = passed ? "통과" : "실패";

        std::printf("%s", log.c_str());
        Debug->LogWarning("[dx12.resize] " + verdict + "\n" + log);
        std::printf("[CLI] dx12.resize %s\n", verdict.c_str());
    }

    static void Cmd_dx12_parallel(const ConsoleCommandContext& ctx)
    {
        // 커맨드 기록 병렬화 검증(PHASE 3-6).
        std::string log;
        const bool passed = DX12Test::RunParallelRecordTest(log);
        const std::string verdict = passed ? "통과" : "실패";

        std::printf("%s", log.c_str());
        Debug->LogWarning("[dx12.parallel] " + verdict + "\n" + log);
        std::printf("[CLI] dx12.parallel %s\n", verdict.c_str());
    }

    static void Cmd_dx12_gbuffer(const ConsoleCommandContext& ctx)
    {
        // GBuffer 패스 검증(PHASE 3-6).
        std::string log;
        const bool passed = DX12Test::RunGBufferTest(log);
        const std::string verdict = passed ? "통과" : "실패";

        std::printf("%s", log.c_str());
        Debug->LogWarning("[dx12.gbuffer] " + verdict + "\n" + log);
        std::printf("[CLI] dx12.gbuffer %s\n", verdict.c_str());
    }

    static void Cmd_dx12_rendergraph(const ConsoleCommandContext& ctx)
    {
        // 렌더 그래프 자가 검증(PHASE 3-5).
        std::string log;
        const bool passed = DX12Test::RunRenderGraphTest(log);

        std::printf("%s", log.c_str());
        Debug->LogWarning(std::string("[dx12.rendergraph] ") + (passed ? "통과" : "실패") + "\n" + log);
        std::printf("[CLI] dx12.rendergraph %s\n", passed ? "통과" : "실패");
    }

    static void Cmd_dx12_descriptorheap(const ConsoleCommandContext& ctx)
    {
        // completion 기반 descriptor page recycler·샘플러 힙 자가 검증.
        std::string log;
        const bool passed = DX12Test::RunDescriptorHeapTest(log);

        std::printf("%s", log.c_str());
        Debug->LogWarning(std::string("[dx12.descriptorheap] ") + (passed ? "통과" : "실패") + "\n" + log);
        std::printf("[CLI] dx12.descriptorheap %s\n", passed ? "통과" : "실패");
    }

    static void Cmd_window_info(const ConsoleCommandContext& ctx)
    {
        // 엔진이 실제로 인식하는 클라이언트 크기. window.resize가 리사이즈 경로까지
        // 도달했는지를 UI 계산과 같은 출처(화면 크기 버스)로 확인한다.
        const uint32_t clientW = ScreenResizeBus::Get().GetWidth();
        const uint32_t clientH = ScreenResizeBus::Get().GetHeight();
        std::printf("[CLI] 클라이언트 영역: %ux%u\n", clientW, clientH);
        Debug->LogWarning("[CLI] 클라이언트 영역: " +
            std::to_string(clientW) + "x" + std::to_string(clientH));
    }

    static void Cmd_ui_status(const ConsoleCommandContext& ctx)
    {
        // 지연 연결 상태를 숫자로 본다. 레지스트리 등록 수와 그중 캔버스가 연결된 수,
        // 그리고 씬의 캔버스 목록 — 검증에서 눈으로 대조할 기준선이다.
        Scene* scene = SceneManagers->GetActiveScene();
        if (!scene) { std::printf("[CLI] 활성 씬 없음\n"); return; }

        int imageLinked = 0;
        for (auto* image : UIManagers->Images) { if (image && image->GetOwnerCanvas()) ++imageLinked; }
        int textLinked = 0;
        for (auto* text : UIManagers->Texts) { if (text && text->GetOwnerCanvas()) ++textLinked; }
        int spriteLinked = 0;
        for (auto* sprite : UIManagers->SpriteSheets) { if (sprite && sprite->GetOwnerCanvas()) ++spriteLinked; }

        // 캔버스별 소속 UI 수까지 보여 준다 — 오연결(엉뚱한 캔버스에 붙음)은
        // 총합만 봐서는 안 보이고, 캔버스별 분포가 어긋나야 드러난다.
        std::string canvasNames;
        for (const auto& canvasHandle : scene->GetCanvases())
        {
            // 캔버스 캐시는 핸들이다(트랙 E5-R2) — 씬에서 떠난 것은 여기서 걸러진다.
            if (Entity* canvas = scene->Resolve(canvasHandle))
            {
                if (!canvasNames.empty()) canvasNames += ", ";
                canvasNames += canvas->m_name.ToString();

                if (Canvas* canvasComponent = canvas->GetComponent<Canvas>())
                {
                    canvasNames += "(" + std::to_string(canvasComponent->UIObjs.size()) + ")";
                }
            }
        }

        char line[512]{};
        std::snprintf(line, sizeof(line),
            "[UI 상태] Image %d/%zu 연결 · Text %d/%zu 연결 · Sprite %d/%zu 연결 · 캔버스 %zu개 [%s]",
            imageLinked, UIManagers->Images.size(),
            textLinked, UIManagers->Texts.size(),
            spriteLinked, UIManagers->SpriteSheets.size(),
            scene->GetCanvases().size(), canvasNames.c_str());

        Debug->LogWarning(line);
        std::printf("%s\n", line);
    }

    static void Cmd_dump_crash(const ConsoleCommandContext& ctx)
    {
        const std::vector<std::string>& parts = ctx.parts;

        // 크래시 처리 자체를 검증하기 위한 명령. 종류별로 경로가 달라서
        // (SEH / abort / terminate) 각각 실제로 덤프가 남는지 확인해야 한다.
        const std::string kind = (parts.size() > 1) ? parts[1] : std::string("access");

        Debug->LogWarning("[CLI] 의도적 크래시: " + kind);
        Log::FlushNow();

        if ("abort" == kind)
        {
            std::abort();
        }
        else if ("terminate" == kind)
        {
            std::terminate();
        }
        else if ("throw" == kind)
        {
            // 처리되지 않은 C++ 예외 → std::terminate 경로
            throw std::runtime_error("dump.crash throw");
        }
        else if ("access" == kind)
        {
            volatile int* p = nullptr;
            *p = 1;
        }
        else
        {
            std::printf("[CLI] 사용법: dump.crash <access|abort|terminate|throw>\n");
        }
    }

    static void Cmd_dump_list(const ConsoleCommandContext& ctx)
    {
        const std::vector<std::string>& parts = ctx.parts;
        const std::string& line = ctx.line;
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

    static void Cmd_animator_state(const ConsoleCommandContext& ctx)
    {
        const std::vector<std::string>& parts = ctx.parts;
        const std::string& line = ctx.line;
        const std::string& cmd = ctx.cmd;

        // 애니메이션 상태와 거기 붙는 스크립트는 원래 컨트롤러 편집기에서 만든다.
        // 상태 스크립트 바인딩을 검증할 방법이 없으므로 편집기가 하는 일을 CLI로 대신한다.
        if (parts.size() < 4)
        {
            std::printf("[CLI] 사용법: animator.state <오브젝트 이름> <상태 이름> <스크립트 타입>\n");
            return;
        }

        Scene* scene = SceneManagers->GetActiveScene();
        if (!scene) { std::printf("[CLI] 활성 씬 없음\n"); return; }

        const std::string behaviourName = parts.back();
        const std::string stateName = parts[parts.size() - 2];
        std::string rest = TrimLine(line.substr(cmd.size()));
        const std::string objectName = TrimLine(rest.substr(0, rest.rfind(stateName)));

        auto object = scene->GetEntity(objectName);
        if (!object) { std::printf("[CLI] 오브젝트를 찾을 수 없음: %s\n", objectName.c_str()); return; }

        Animator* animator = object->GetComponent<Animator>();
        if (nullptr == animator) { std::printf("[CLI] Animator가 없음: %s\n", objectName.c_str()); return; }

        if (animator->m_animationControllers.empty())
        {
            animator->CreateController("CliController");
        }

        auto controller = animator->m_animationControllers.front();
        if (!controller) { std::printf("[CLI] 컨트롤러 생성 실패\n"); return; }

        AnimationState* state = controller->CreateState(stateName, -1);
        if (nullptr == state) { std::printf("[CLI] 상태 생성 실패: %s\n", stateName.c_str()); return; }

        state->SetBehaviour(behaviourName);
        if (nullptr == state->behaviour)
        {
            Debug->LogError("[CLI] 상태 스크립트를 찾을 수 없음: " + behaviourName);
            std::printf("[CLI] 상태 스크립트를 찾을 수 없음: %s\n", behaviourName.c_str());
            return;
        }

        // 현재 상태로 만들어 두면 다음 프레임부터 Update가 돈다.
        // (전이 조건을 CLI로 짜기는 과하므로, 진입은 여기서 직접 흉내 낸다)
        controller->m_curState = state;
        state->behaviour->Enter();

        Debug->LogWarning("[CLI] 애니메이션 상태 추가: " + objectName + " · " + stateName + " <- " + behaviourName);
        std::printf("[CLI] 애니메이션 상태 추가: %s · %s <- %s\n",
            objectName.c_str(), stateName.c_str(), behaviourName.c_str());
    }

    static void Cmd_animator_exit(const ConsoleCommandContext& ctx)
    {
        const std::vector<std::string>& parts = ctx.parts;
        const std::string& line = ctx.line;
        const std::string& cmd = ctx.cmd;

        // 상태에서 빠져나가는 것까지 확인하려면 전이 조건을 짜야 하는데 CLI로는 과하다.
        // 상태 머신이 전이 때 하는 일(Exit 호출 + 현재 상태 비우기)만 흉내 낸다.
        if (parts.size() < 2)
        {
            std::printf("[CLI] 사용법: animator.exit <오브젝트 이름>\n");
            return;
        }

        Scene* scene = SceneManagers->GetActiveScene();
        if (!scene) { std::printf("[CLI] 활성 씬 없음\n"); return; }

        const std::string objectName = TrimLine(line.substr(cmd.size()));
        auto object = scene->GetEntity(objectName);
        if (!object) { std::printf("[CLI] 오브젝트를 찾을 수 없음: %s\n", objectName.c_str()); return; }

        Animator* animator = object->GetComponent<Animator>();
        if (nullptr == animator || animator->m_animationControllers.empty())
        {
            std::printf("[CLI] Animator 또는 컨트롤러가 없음: %s\n", objectName.c_str());
            return;
        }

        auto controller = animator->m_animationControllers.front();
        if (!controller || nullptr == controller->m_curState)
        {
            std::printf("[CLI] 현재 상태 없음\n");
            return;
        }

        if (controller->m_curState->behaviour) controller->m_curState->behaviour->Exit();
        controller->m_curState = nullptr;

        Debug->LogWarning("[CLI] 애니메이션 상태 종료: " + objectName);
        std::printf("[CLI] 애니메이션 상태 종료: %s\n", objectName.c_str());
    }

    static void Cmd_animator_param(const ConsoleCommandContext& ctx)
    {
        const std::vector<std::string>& parts = ctx.parts;
        const std::string& line = ctx.line;
        const std::string& cmd = ctx.cmd;

        // 애니메이터 파라미터는 원래 컨트롤러 편집기에서 선언한다. 스크립트에서는 만들 수 없어
        // 바인딩을 검증할 방법이 없으므로, 편집기가 하는 일을 CLI로 대신한다.
        if (parts.size() < 4)
        {
            std::printf("[CLI] 사용법: animator.param <오브젝트 이름> <파라미터 이름> <bool|float|int|trigger>\n");
            return;
        }

        Scene* scene = SceneManagers->GetActiveScene();
        if (!scene) { std::printf("[CLI] 활성 씬 없음\n"); return; }

        // 오브젝트 이름에 공백이 흔하므로 뒤의 두 토큰을 파라미터 이름·타입으로 본다.
        const std::string typeName = parts.back();
        const std::string paramName = parts[parts.size() - 2];
        std::string rest = TrimLine(line.substr(cmd.size()));
        const std::string objectName = TrimLine(rest.substr(0, rest.rfind(paramName)));

        auto object = scene->GetEntity(objectName);
        if (!object)
        {
            std::printf("[CLI] 오브젝트를 찾을 수 없음: %s\n", objectName.c_str());
            return;
        }

        Animator* animator = object->GetComponent<Animator>();
        if (nullptr == animator)
        {
            std::printf("[CLI] Animator가 없음: %s\n", objectName.c_str());
            return;
        }

        if ("bool" == typeName)         animator->AddParameter(paramName, false, ValueType::Bool);
        else if ("float" == typeName)   animator->AddParameter(paramName, 0.f,   ValueType::Float);
        else if ("int" == typeName)     animator->AddParameter(paramName, 0,     ValueType::Int);
        else if ("trigger" == typeName) animator->AddParameter(paramName, false, ValueType::Trigger);
        else
        {
            std::printf("[CLI] 알 수 없는 파라미터 타입: %s\n", typeName.c_str());
            return;
        }

        Debug->LogWarning("[CLI] 애니메이터 파라미터 추가: " + objectName + " <- " + paramName + " (" + typeName + ")");
        std::printf("[CLI] 애니메이터 파라미터 추가: %s <- %s (%s)\n", objectName.c_str(), paramName.c_str(), typeName.c_str());
    }

    static void Cmd_component_list(const ConsoleCommandContext& ctx)
    {
        const std::vector<std::string>& parts = ctx.parts;

        // 붙일 수 있는 컴포넌트 타입을 훑어본다(콜라이더 이름 확인용).
        const std::string filter = (parts.size() > 1) ? parts[1] : std::string{};

        int count = 0;
        for (const auto& [typeName, type] : ComponentFactorys->m_componentTypes)
        {
            if (!filter.empty() && typeName.find(filter) == std::string::npos) continue;

            Debug->LogWarning("[CLI]   " + typeName);
            ++count;
        }
        std::printf("[CLI] 컴포넌트 타입 %d개 기록\n", count);
    }

    static void Cmd_prefab_create(const ConsoleCommandContext& ctx)
    {
        const std::vector<std::string>& parts = ctx.parts;
        const std::string& line = ctx.line;
        const std::string& cmd = ctx.cmd;

        if (parts.size() < 3)
        {
            std::printf("[CLI] 사용법: prefab.create <오브젝트 이름> <프리팹 이름>\n");
            return;
        }

        Scene* scene = SceneManagers->GetActiveScene();
        if (!scene) { std::printf("[CLI] 활성 씬 없음\n"); return; }

        // 오브젝트 이름에 공백이 흔하므로 프리팹 이름을 마지막 토큰으로 본다.
        const std::string prefabName = parts.back();
        std::string rest = TrimLine(line.substr(cmd.size()));
        const std::string objectName = TrimLine(rest.substr(0, rest.rfind(prefabName)));

        auto object = scene->GetEntity(objectName);
        if (!object)
        {
            std::printf("[CLI] 오브젝트를 찾을 수 없음: %s\n", objectName.c_str());
            return;
        }

		Prefab* prefab = PrefabUtilitys->CreatePrefab(object, prefabName);
        if (!prefab)
        {
            std::printf("[CLI] 프리팹 생성 실패\n");
            return;
        }

        const file::path path = PathFinder::Relative("Prefabs\\") / (prefabName + ".prefab");
        if (!PrefabUtilitys->SavePrefab(prefab, path.string()))
        {
            std::printf("[CLI] 프리팹 저장 실패: %s\n", path.string().c_str());
            return;
        }

        Debug->LogWarning("[CLI] 프리팹 생성: " + prefabName + " <- " + objectName);
        std::printf("[CLI] 프리팹 생성: %s\n", path.string().c_str());
    }

    static void Cmd_script_reload(const ConsoleCommandContext& ctx)
    {
        auto& clr = ClrHost::Get();
        if (!clr.IsReady())
        {
            std::printf("[CLI] CLR이 준비되지 않았습니다\n");
            return;
        }

        Scene* scene = SceneManagers->GetActiveScene();
        std::vector<ScriptComponent*> scripts;

        // 1) 값을 챙기고 인스턴스 참조를 끊는다. 하나라도 남으면 언로드가 실패한다.
        if (scene)
        {
            for (const auto& object : scene->m_Entities)
            {
                if (!object) continue;

                auto script = object->GetComponent<ScriptComponent>();
                if (nullptr != script)
                {
                    script->PrepareForReload();
                    scripts.push_back(script);
                }
            }
        }

        // 2) 어셈블리 교체
        if (!clr.ReloadScripts())
        {
            Debug->LogError("[스크립트] 리로드 실패");
            std::printf("[CLI] 리로드 실패\n");
            return;
        }

        // 3) 인스턴스를 다시 만들고 챙겨 둔 값을 되돌린다
        int restored = 0;
        for (ScriptComponent* script : scripts)
        {
            script->OnInitialized();
            if (script->HasInstance()) ++restored;
        }

        // 언로드 완료 여부는 여기서 묻지 않는다. 리로드 호출 스택이 아직 살아 있어
        // 항상 "잔존"으로 나온다. 몇 프레임 뒤 script.status로 확인할 것.
        Debug->LogWarning("[스크립트] 리로드 완료 — 복원 " + std::to_string(restored) + "/" +
            std::to_string(scripts.size()));
        std::printf("[CLI] 리로드 완료: %d/%zu 복원 (언로드 확인은 script.status)\n",
            restored, scripts.size());
    }

    static void Cmd_script_status(const ConsoleCommandContext& ctx)
    {
        auto& clr = ClrHost::Get();
        const bool stale = clr.IsPreviousContextAlive();

        Debug->LogWarning(std::string("[스크립트] CLR ") + (clr.IsReady() ? "준비됨" : "비활성") +
            " · 활성 스크립트 " + std::to_string(clr.LastActiveCount()) + "개" +
            " · 이전 어셈블리 " + (stale ? "잔존(참조 누수)" : "정리됨"));
        std::printf("[CLI] CLR %s, 활성 스크립트 %d개, 이전 어셈블리 %s\n",
            clr.IsReady() ? "준비됨" : "비활성", clr.LastActiveCount(),
            stale ? "잔존(참조 누수)" : "정리됨");
    }

    static void Cmd_play(const ConsoleCommandContext& ctx)
    {
        const std::string& cmd = ctx.cmd;

        // 에디터의 재생/정지 버튼과 같은 경로(SceneManager::Editor가 다음 프레임에 처리).
        SceneManagers->SetGameStart(cmd == "play");
        std::printf("[CLI] %s 요청\n", cmd.c_str());
    }

    // 재생 상태를 관측한다. 재생 진입은 좌표를 바꾸지 않고 phase만 바꾸므로,
    // transform digest만으로는 "play가 무시됐는데 상태가 그대로라 통과"가 성립한다.
    // 왕복 게이트가 전이 자체를 확인할 수 있어야 그 뒤의 복원 단정이 의미를 갖는다.
    static void Cmd_play_state(const ConsoleCommandContext& ctx)
    {
        (void)ctx;
        const Scene* scene = SceneManagers->GetActiveScene();
        std::printf("[play.state] gameStart=%d paused=%d editorSceneLoaded=%d "
            "pending=%d entities=%zu\n",
            SceneManagers->IsGameStart() ? 1 : 0,
            SceneManagers->IsGamePaused() ? 1 : 0,
            SceneManagers->IsEditorSceneLoaded() ? 1 : 0,
            SceneManagers->HasPendingSceneStructureChange() ? 1 : 0,
            scene ? scene->m_Entities.size() : 0u);
    }

    // ── 파이프라인 구성 프로브 (E4 게이트용) ──
    //
    // E4는 Editor 렌더 패스를 RenderCore 밖으로 옮기고, 판정 기준은
    // "Grid/Gizmo는 Scene View에만 기여하고 Player pipeline에는 node 자체가 없다"다.
    // 그런데 패스가 **어느 뷰에 조립되는지**를 밖에서 볼 수단이 없었다. 패스 내부
    // 렌더링은 dx12.*/vk.* 자가 검사가 리드백으로 픽셀까지 재지만, 그것은 격리된
    // 합성 씬이라 조립 결과는 안 본다.
    //
    // LivePipelineDesc::Dump()는 이미 있고 디버그 스냅샷에도 실려 있었는데
    // 아무도 찍지 않았다. 한 줄씩 파싱 가능한 형태로 내보낸다.
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
    static void Cmd_undo_state(const ConsoleCommandContext& ctx)
    {
        const std::vector<std::string>& parts = ctx.parts;
        const std::string label = (parts.size() >= 2) ? parts[1] : "state";

        std::printf("[undo.state:%s] isGameMode=%d gameStart=%d "
            "editUndo=%zu editRedo=%zu gameUndo=%zu gameRedo=%zu\n",
            label.c_str(),
            Meta::UndoManager::GetInstance()->m_isGameMode ? 1 : 0,
            SceneManagers->IsGameStart() ? 1 : 0,
            Meta::UndoManager::GetInstance()->EditUndoDepth(),
            Meta::UndoManager::GetInstance()->EditRedoDepth(),
            Meta::UndoManager::GetInstance()->GameUndoDepth(),
            Meta::UndoManager::GetInstance()->GameRedoDepth());
    }

    // 에디터의 Ctrl+Z / Ctrl+Y와 같은 호출.
    static void Cmd_undo_redo(const ConsoleCommandContext& ctx)
    {
        if (ctx.cmd == "undo") Meta::UndoManager::GetInstance()->Undo();
        else                   Meta::UndoManager::GetInstance()->Redo();
        std::printf("[CLI] %s 실행\n", ctx.cmd.c_str());
    }

    // 기존 object.create는 Undo 스택을 건드리지 않는다. 그 성질에 이미 여러 게이트가
    // 기대고 있으므로 바꾸지 않고, 이력을 쌓는 별도 명령을 둔다.
    static void Cmd_object_create_undoable(const ConsoleCommandContext& ctx)
    {
        const std::vector<std::string>& parts = ctx.parts;
        if (parts.size() < 2)
        {
            std::printf("[CLI] 사용법: object.create.undoable <이름>\n");
            return;
        }

        Scene* scene = SceneManagers->GetActiveScene();
        if (!scene) { std::printf("[CLI] 활성 씬 없음\n"); return; }

        Meta::UndoManager::GetInstance()->Execute(
            std::make_unique<Meta::CreateEntityCommand>(
                scene, parts[1], GameObjectType::Empty));
        std::printf("[CLI] undo 기록과 함께 생성: %s\n", parts[1].c_str());
    }

    // 선택 상태 관측. scene.select가 실제로 먹었는지, 정지가 선택을 어떻게 하는지
    // 둘 다 이것으로 본다.
    //
    // ⚠ m_selectedEntity(단일)와 m_selectedEntities(복수)를 **따로** 찍는다.
    //   scene.select는 단일만 대입하고 벡터는 건드리지 않아 둘이 어긋나 있다.
    //   합쳐서 찍으면 그 어긋남이 가려진다 — 지금 동작을 정직하게 못 박는다.
    static void Cmd_scene_selection(const ConsoleCommandContext& ctx)
    {
        const std::vector<std::string>& parts = ctx.parts;
        const std::string label = (parts.size() >= 2) ? parts[1] : "selection";

        Scene* scene = SceneManagers->GetActiveScene();
        if (!scene)
        {
            std::printf("[selection:%s] 활성 씬 없음\n", label.c_str());
            return;
        }

        const Entity* primary = scene->m_selectedEntity;
        std::printf("[selection:%s] primary=%s multi=%zu\n",
            label.c_str(),
            primary ? primary->m_name.ToString().c_str() : "(none)",
            scene->m_selectedEntities.size());

        for (const Entity* entity : scene->m_selectedEntities)
        {
            std::printf("[selection:%s] multi|%s\n", label.c_str(),
                entity ? entity->m_name.ToString().c_str() : "(null)");
        }
    }

    static void Cmd_lifecycle_trace(const ConsoleCommandContext& ctx)
    {
        const std::vector<std::string>& parts = ctx.parts;

        // 생명주기 호출 순서를 받아 적는다(PHASE 9-0).
        //
        // PHASE 9는 이 순서를 만들어 내는 기구를 통째로 바꾼다. 지금 순서는 델리게이트의
        // 우선순위 정렬과 등록 시점이 만드는 창발적 결과라 코드로는 알 수 없다 —
        // 교체 전에 받아 적어 두어야 교체 후 "동작이 같다"를 주장할 수 있다.
        const std::string mode = (parts.size() >= 2) ? parts[1] : "status";

        if (mode == "on")
        {
            // 틱 단계(Update·LateUpdate·FixedUpdate)를 적을 프레임 수.
            // 한 프레임 안의 순서가 알고 싶은 것이지 반복 횟수가 아니라서 예산을 둔다.
            const int frames = (parts.size() >= 3) ? std::atoi(parts[2].c_str()) : 3;
            Lifecycle::Trace::Enable(frames);
            std::printf("[CLI] lifecycle.trace on — 틱 %d프레임\n", frames);
        }
        else if (mode == "off")
        {
            Lifecycle::Trace::Disable();
            std::printf("[CLI] lifecycle.trace off — %zu건 보관\n", Lifecycle::Trace::Count());
        }
        else if (mode == "clear")
        {
            Lifecycle::Trace::Clear();
            std::printf("[CLI] lifecycle.trace clear\n");
        }
        else
        {
            std::printf("[CLI] lifecycle.trace — %s · %zu건 · 틱 잔여 %d프레임\n",
                Lifecycle::Trace::IsEnabled() ? "기록 중" : "정지",
                Lifecycle::Trace::Count(),
                Lifecycle::Trace::RemainingTickFrames());
        }
    }

    static void Cmd_lifecycle_registry(const ConsoleCommandContext& ctx)
    {
        // 상태 조회만 남았다(PHASE 9-3에서 델리게이트를 철거해 경로가 하나다).
        // 진단 가치는 그대로다 — 각 단계 리스트의 크기가 곧 '무엇이 매 프레임 도는가'이고,
        // 마스크 표 크기는 등록 목록이 실제로 채워졌는지를 알려 준다.
        Scene* scene = SceneManagers->GetActiveScene();
        std::printf("[CLI] lifecycle — 마스크 표 %zu종\n", Lifecycle::Registry::Count());

        if (nullptr != scene)
        {
            const auto counts = scene->GetRegistryCounts();
            std::printf("[CLI]   pendingAwake %zu · pendingStart %zu\n",
                counts.pendingAwake, counts.pendingStart);

            // 트랙 L4 래칫 측정용 — 명시 구독(Schedule().Subscribe) 대 암묵 구독
            // (RegisterComponent 경유)의 잔존 수. 통합 단계에서 배선.
            const auto subCounts = scene->GetSubscriptionCounts();
            std::printf("[CLI]   구독 잔존 — 암묵 %zu · 명시 %zu\n",
                subCounts.implicitCount, subCounts.explicitCount);
        }
    }

    static void Cmd_lifecycle_dump(const ConsoleCommandContext& ctx)
    {
        const std::vector<std::string>& parts = ctx.parts;

        const std::string path = ResolveTestArtifactPath("Traces",
            (parts.size() >= 2) ? parts[1] : std::string("lifecycle_trace.tsv"));
        const size_t count = Lifecycle::Trace::Count();

        if (0 == count)
        {
            // 빈 파일을 성공으로 흘려보내면 "기준선을 떴다"고 착각한 채 다음으로 넘어간다.
            // 3-6에서 겪은 조용한 통과와 같은 부류라 여기서 실패로 못 박는다.
            std::printf("[CLI] lifecycle.dump 실패 — 기록 0건 (lifecycle.trace on 을 먼저 부를 것)\n");
            return;
        }

        if (Lifecycle::Trace::Dump(path))
        {
            std::printf("[CLI] lifecycle.dump %s — %zu건\n", path.c_str(), count);
        }
        else
        {
            std::printf("[CLI] lifecycle.dump 실패 — 파일을 열 수 없다: %s\n", path.c_str());
        }
    }

    static void Cmd_lifecycle_stress(const ConsoleCommandContext& ctx)
    {
        const std::vector<std::string>& parts = ctx.parts;

        // 파괴·생성을 몰아쳐 수명 경로를 흔든다(PHASE 9-0의 ASan 재현용).
        //
        // 지금은 프레임 경계에서 파괴가 일어나는 경로만 흔든다. "순회 도중 파괴"와
        // "Update 안에서 AddComponent" 같은 재진입 재현은 9-1의 레지스트리가 선
        // 뒤에 붙인다 — 지금 구조에는 그 지점을 안전하게 잡을 자리가 없다.
        const std::string mode = (parts.size() >= 2) ? parts[1] : "";
        const int count = (parts.size() >= 3) ? std::atoi(parts[2].c_str()) : 8;

        Scene* scene = SceneManagers->GetActiveScene();
        if (!scene) { std::printf("[CLI] 활성 씬 없음\n"); return; }

        if (mode == "destroy")
        {
            int marked = 0;
            // 루트(0번)는 건드리지 않는다. 씬 구조가 무너지면 이후 명령이 전부 의미를 잃는다.
            for (size_t i = 1; i < scene->m_Entities.size() && marked < count; ++i)
            {
                const auto& owned = scene->m_Entities[i];
                if (!owned || owned->IsDestroyMark()) continue;
				scene->DestroyEntity(owned.get());
                ++marked;
            }
            std::printf("[CLI] lifecycle.stress destroy — %d개 파괴 표시\n", marked);
        }
        else if (mode == "churn")
        {
            // 파괴와 생성을 같은 프레임에 섞는다. 인덱스 재사용 경로가 여기서 드러난다.
            int marked = 0;
            for (size_t i = 1; i < scene->m_Entities.size() && marked < count; ++i)
            {
                const auto& owned = scene->m_Entities[i];
                if (!owned || owned->IsDestroyMark()) continue;
				scene->DestroyEntity(owned.get());
                ++marked;
            }
            for (int i = 0; i < count; ++i)
            {
                scene->CreateEntity("StressChurn_" + std::to_string(i));
            }
            std::printf("[CLI] lifecycle.stress churn — 파괴 %d · 생성 %d\n", marked, count);
        }
        else if (mode == "reentrant" || mode == "reentrant-destroy" || mode == "reentrant-add")
        {
            // 순회 한복판에서 터뜨린다(PHASE 9-9).
            //
            // 위 destroy/churn은 프레임 경계에서 일어나므로 R1·R2를 시험하지 못한다 —
            // 그 둘은 "순회하는 도중에 대상이 죽으면?"이라는 질문이고, 답하려면
            // 실제로 순회 중이어야 한다.
            const auto kind =
                (mode == "reentrant-destroy") ? Scene::StressKind::Destroy :
                (mode == "reentrant-add")     ? Scene::StressKind::AddComponent :
                                                Scene::StressKind::Both;
            scene->ArmReentrancyStress(kind, count);
            std::printf("[CLI] lifecycle.stress %s — 다음 Update 순회 한복판에서 %d건 발화\n",
                mode.c_str(), count);
        }
        else
        {
            std::printf("[CLI] lifecycle.stress destroy|churn|reentrant|reentrant-destroy|reentrant-add [개수]\n");
        }
    }

    static void Cmd_scene_dump(const ConsoleCommandContext& ctx)
    {
        const std::vector<std::string>& parts = ctx.parts;

        // 활성 씬의 오브젝트 계층을 로그로 남긴다. 재생/정지 전후로 찍어 비교하면
        // 무엇이 사라졌는지가 그대로 드러난다.
        Scene* scene = SceneManagers->GetActiveScene();
        if (!scene)
        {
            std::printf("[CLI] 활성 씬 없음\n");
            return;
        }

        const std::string label = (parts.size() > 1) ? parts[1] : std::string("dump");
        Debug->LogWarning("[씬 덤프] " + label + " · 오브젝트 " +
            std::to_string(scene->m_Entities.size()) + "개");

        for (const auto& object : scene->m_Entities)
        {
            if (!object) continue;
            const auto& p = object->Transform_().position;
            char position[96]{};
            std::snprintf(position, sizeof(position), "(%.3f, %.3f, %.3f)", p.x, p.y, p.z);

            Debug->LogWarning("[씬 덤프]   " + object->m_name.ToString() +
                " (index=" + std::to_string(object->m_index) +
                ", parent=" + std::to_string(object->GetParentIndex()) +
                ", 컴포넌트 " + std::to_string(object->m_components.size()) + "개"
                ", pos=" + position + ")");
        }
        std::printf("[CLI] 씬 덤프 기록: %s (%zu개)\n", label.c_str(), scene->m_Entities.size());
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

    static void Cmd_assets_unload(const ConsoleCommandContext& ctx)
    {
        DataSystems->UnloadUnusedAssets();
        std::printf("[CLI] 사용하지 않는 에셋 정리 요청\n");
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

    static void Cmd_bt_status(const ConsoleCommandContext& ctx)
    {
        const std::string& cmd = ctx.cmd;

        // 행동 트리 진단(PHASE 9-8 완료 기준 1·3).
        //
        // 왜 필요한가: 트리 생성·틱은 실패할 때만 로그를 남긴다. 그래서 "트리가 안
        // 서서 AI가 가만히 있다"와 "정상 동작"이 밖에서 구분되지 않는다 — 둘 다
        // 무음이다. 회귀 세트도 BT를 쓰는 씬을 열지 않으므로, 전부 통과해도 BT
        // 코드는 한 줄도 실행되지 않은 채 통과할 수 있다. 즉 BT에 대한 양성 증거가
        // 없었고, 이 명령이 그 자리를 메운다.
        if (cmd == "bt.reset")
        {
            ClrHost::Get().ResetBehaviorTreeStats();
            ClrHost::Get().ResetAICrossings();
            std::printf("[CLI] BT 누계 초기화 (트리는 그대로)\n");
            return;
        }

        ClrHost::ScriptBTStats bt{};
        if (!ClrHost::Get().GetBehaviorTreeStats(bt))
        {
            // 조용히 0을 찍지 않는다 — "트리 0개"와 "지표를 못 읽었다"는 전혀 다른
            // 상황인데 같은 숫자로 보이면 진단이 거꾸로 간다.
            std::printf("[CLI] BT 지표 없음 — 스크립트 계층 비활성이거나 구 어셈블리\n");
            return;
        }

        const ClrHost::AICrossingCounters& x = ClrHost::Get().AICrossings();

        char line[512]{};
        std::snprintf(line, sizeof(line),
            "[bt.status] 트리 %d개 · 노드 타입 %d종 · 틱 %llu회 · 건너뜀 %llu회",
            bt.treeCount, bt.nodeTypeCount,
            static_cast<unsigned long long>(bt.tickCount),
            static_cast<unsigned long long>(bt.skippedCount));
        std::printf("[CLI] %s\n", line);
        Debug->LogWarning(line);

        // 크로싱 비율이 이 재설계의 핵심 주장이다 — 트리가 몇 개든 프레임당 1회.
        // 분모는 FlushAITicks 호출 수(= 흘려보낸 프레임 수)이고, 큐가 빈 프레임도
        // 포함한다. 그래서 비율은 1을 넘을 수 없고, 넘으면 배선이 깨진 것이다.
        char cross[512]{};
        const double perFrame = (0 == x.flushCalls)
            ? 0.0 : static_cast<double>(x.crossings) / static_cast<double>(x.flushCalls);
        const double perCrossing = (0 == x.crossings)
            ? 0.0 : static_cast<double>(x.ticksDelivered) / static_cast<double>(x.crossings);
        std::snprintf(cross, sizeof(cross),
            "[bt.status] 경계 통과 %llu회 / 프레임 %llu (프레임당 %.2f) · 전달 틱 %llu건 "
            "(크로싱당 %.1f · 최대 배치 %llu)",
            static_cast<unsigned long long>(x.crossings),
            static_cast<unsigned long long>(x.flushCalls), perFrame,
            static_cast<unsigned long long>(x.ticksDelivered), perCrossing,
            static_cast<unsigned long long>(x.maxBatch));
        std::printf("[CLI] %s\n", cross);
        Debug->LogWarning(cross);
    }

    static void Cmd_gc_collect(const ConsoleCommandContext& ctx)
    {
        // 씬 전환이 자동으로 부르는 것과 같은 경로를 손으로 부른다.
        // 벤치에서 "전환 없이도 회수되는가"를 가르는 데 쓴다.
        ClrHost::Get().CollectManagedHeap();
        std::printf("[CLI] 관리 힙 확정 수집 요청\n");
    }

    static void Cmd_log_flush(const ConsoleCommandContext& ctx)
    {
        Log::FlushNow();
        std::printf("[CLI] 로그 flush\n");
    }

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

    using Table = std::unordered_map<std::string, ConsoleCommandHandler>;

    const Table& GetTable()
    {
        static const Table table = []
        {
            Table t;
            auto reg = [&t](std::initializer_list<const char*> names,
                            ConsoleCommandHandler fn)
            {
                for (const char* n : names)
                {
                    // 같은 이름을 두 번 등록하면 조용히 한쪽이 먹힌다.
                    const bool inserted = t.emplace(n, fn).second;
                    if (!inserted) { std::printf("[CLI] 명령 이름 중복 등록: %s\n", n); }
                }
            };

            reg({ "help" }, &Cmd_help);
            reg({ "quit", "exit" }, &Cmd_quit);
            reg({ "game.pak" }, &Cmd_game_pak);
            reg({ "wait" }, &Cmd_wait);
            reg({ "scene.load", "scene.switch" }, &Cmd_scene_load);
            reg({ "scene.new" }, &Cmd_scene_new);
            reg({ "scene.ddol" }, &Cmd_scene_ddol);
			reg({ "ai.status" }, &Cmd_ai_status);
            reg({ "scene.save" }, &Cmd_scene_save);
            reg({ "object.create" }, &Cmd_object_create);
            reg({ "object.rename" }, &Cmd_object_rename);
            reg({ "object.transform" }, &Cmd_object_transform);
            reg({ "object.parent" }, &Cmd_object_parent);
            reg({ "object.rootref" }, &Cmd_object_rootref);
            reg({ "object.duplicate" }, &Cmd_object_duplicate);
            reg({ "scene.hierarchycheck" }, &Cmd_scene_hierarchycheck);
            reg({ "render.matmode" }, &Cmd_render_matmode);
            reg({ "object.property" }, &Cmd_object_property);
            reg({ "model.load" }, &Cmd_model_load);
			reg({ "terrain.authoring.probe" }, &Cmd_terrain_authoring_probe);
			reg({ "foliage.authoring.probe" }, &Cmd_foliage_authoring_probe);
			reg({ "blackboard.authoring.probe" }, &Cmd_blackboard_authoring_probe);
			reg({ "asset.guid.rename.probe" }, &Cmd_asset_guid_rename_probe);
			reg({ "material.corpus.probe" }, &Cmd_material_corpus_probe);
			reg({ "collisionmatrix.authoring.probe" },
				&Cmd_collisionmatrix_authoring_probe);
			reg({ "shadermeta.probe" }, &Cmd_shadermeta_probe);
			reg({ "tag.authoring.probe" }, &Cmd_tag_authoring_probe);
			reg({ "inputmap.authoring.probe" }, &Cmd_inputmap_authoring_probe);
			reg({ "animator.authoring.probe" }, &Cmd_animator_authoring_probe);
            reg({ "model.place" }, &Cmd_model_place);
            reg({ "script.add" }, &Cmd_script_add);
            reg({ "scene.select" }, &Cmd_scene_select);
            reg({ "script.fields" }, &Cmd_script_fields);
            reg({ "script.set" }, &Cmd_script_set);
            reg({ "component.add" }, &Cmd_component_add);
            reg({ "prefab.instantiate" }, &Cmd_prefab_instantiate);
            reg({ "prefab.status" }, &Cmd_prefab_status);
			reg({ "prefab.corpus.digest" }, &Cmd_prefab_corpus_digest);
            reg({ "prefab.overrides" }, &Cmd_prefab_overrides);
            reg({ "prefab.update" }, &Cmd_prefab_update);
            reg({ "window.resize" }, &Cmd_window_resize);
            reg({ "ui.rect" }, &Cmd_ui_rect);
            reg({ "ui.anchor", "ui.size", "ui.pos", "ui.screenpos" }, &Cmd_ui_anchor);
            reg({ "ui.hitbox" }, &Cmd_ui_hitbox);
			reg({ "ui.navprobe" }, &Cmd_ui_navprobe);
            reg({ "pix.capture" }, &Cmd_pix_capture);
            reg({ "dx12.selftest" }, &Cmd_dx12_selftest);
            reg({ "vk.selftest" }, &Cmd_vk_selftest);
            reg({ "vk.grid" }, &Cmd_vk_grid);
            reg({ "vk.parallel" }, &Cmd_vk_parallel);
            reg({ "vk.skybox" }, &Cmd_vk_skybox);
            reg({ "vk.ibl" }, &Cmd_vk_ibl);
            reg({ "vk.gizmoicon" }, &Cmd_vk_gizmoicon);
            reg({ "vk.gizmoline" }, &Cmd_vk_gizmoline);
            reg({ "vk.wireframe" }, &Cmd_vk_wireframe);
            reg({ "vk.ui" }, &Cmd_vk_ui);
            reg({ "vk.shadow" }, &Cmd_vk_shadow);
            reg({ "vk.gbuffer" }, &Cmd_vk_gbuffer);
            reg({ "vk.forward" }, &Cmd_vk_forward);
            reg({ "vk.deferred" }, &Cmd_vk_deferred);
            reg({ "vk.decal" }, &Cmd_vk_decal);
            reg({ "vk.ssao" }, &Cmd_vk_ssao);
            reg({ "vk.sss" }, &Cmd_vk_sss);
            reg({ "vk.ssr" }, &Cmd_vk_ssr);
            reg({ "vk.fog" }, &Cmd_vk_fog);
            reg({ "vk.post" }, &Cmd_vk_post);
            reg({ "vk.ssgi" }, &Cmd_vk_ssgi);
            reg({ "profile.selftest" }, &Cmd_profile_selftest);
            reg({ "experiment.model" }, &Cmd_experiment_model);
            reg({ "experiment.modelbridge" }, &Cmd_experiment_modelbridge);
            reg({ "experiment.vertexlayout" }, &Cmd_experiment_vertexlayout);
            reg({ "experiment.anim" }, &Cmd_experiment_anim);
            reg({ "experiment.animtick" }, &Cmd_experiment_animtick);
            reg({ "experiment.animevent" }, &Cmd_experiment_animevent);
            reg({ "experiment.boneresolve" }, &Cmd_experiment_boneresolve);
            reg({ "experiment.foliage" }, &Cmd_experiment_foliage);
            reg({ "experiment.animmask" }, &Cmd_experiment_animmask);
            reg({ "experiment.editorsurface" }, &Cmd_experiment_editorsurface);
            reg({ "experiment.meshbounds" }, &Cmd_experiment_meshbounds);
            reg({ "experiment.import" }, &Cmd_experiment_import);
            reg({ "experiment.gltf" }, &Cmd_experiment_gltf);
            reg({ "experiment.fbx" }, &Cmd_experiment_fbx);
            reg({ "experiment.sampler" }, &Cmd_experiment_sampler);
            reg({ "experiment.tangent" }, &Cmd_experiment_tangent);
            reg({ "experiment.normal" }, &Cmd_experiment_normal);
            reg({ "experiment.cooked" }, &Cmd_experiment_cooked);
            reg({ "experiment.weld" }, &Cmd_experiment_weld);
            reg({ "experiment.cacheopt" }, &Cmd_experiment_cacheopt);
            reg({ "experiment.texcook" }, &Cmd_experiment_texcook);
            reg({ "experiment.smcook" }, &Cmd_experiment_smcook);
            reg({ "experiment.matcook" }, &Cmd_experiment_matcook);
            reg({ "experiment.matparity" }, &Cmd_experiment_matparity);
            reg({ "experiment.matresolve" }, &Cmd_experiment_matresolve);
            reg({ "experiment.matruntime" }, &Cmd_experiment_matruntime);
            reg({ "experiment.matinstance" }, &Cmd_experiment_matinstance);
            reg({ "experiment.matseal" }, &Cmd_experiment_matseal);
            reg({ "experiment.matcodec" }, &Cmd_experiment_matcodec);
            reg({ "experiment.matmigrate" }, &Cmd_experiment_matmigrate);
            reg({ "experiment.matscript" }, &Cmd_experiment_matscript);
            reg({ "experiment.scenecook" }, &Cmd_experiment_scenecook);
            reg({ "experiment.resolver" }, &Cmd_experiment_resolver);
            reg({ "experiment.catalog" }, &Cmd_experiment_catalog);
            reg({ "experiment.embedded" }, &Cmd_experiment_embedded);
            reg({ "experiment.bench" }, &Cmd_experiment_bench);
            reg({ "profile.stats" }, &Cmd_profile_stats);
            reg({ "dx12.psocache" }, &Cmd_dx12_psocache);
            reg({ "rhi.uploadsegments" }, &Cmd_rhi_uploadsegments);
            reg({ "dx12.uploadring" }, &Cmd_dx12_uploadring);
            reg({ "dx12.forward" }, &Cmd_dx12_forward);
            reg({ "dx12.forwardshade" }, &Cmd_dx12_forwardshade);
            reg({ "dx12.forwardscale" }, &Cmd_dx12_forwardscale);
            reg({ "dx12.ssao" }, &Cmd_dx12_ssao);
            reg({ "dx12.ssaoscale" }, &Cmd_dx12_ssaoscale);
            reg({ "dx12.post" }, &Cmd_dx12_post);
            reg({ "dx12.postscale" }, &Cmd_dx12_postscale);
            reg({ "dx12.ui" }, &Cmd_dx12_ui);
            reg({ "dx12.grid" }, &Cmd_dx12_grid);
            reg({ "dx12.gizmoline" }, &Cmd_dx12_gizmoline);
            reg({ "dx12.gizmoicon" }, &Cmd_dx12_gizmoicon);
            reg({ "dx12.wireframe" }, &Cmd_dx12_wireframe);
            reg({ "dx12.gizmoscene" }, &Cmd_dx12_gizmoscene);
            reg({ "dx12.shadowquality" }, &Cmd_dx12_shadowquality);
            reg({ "dx12.skybox" }, &Cmd_dx12_skybox);
            reg({ "dx12.ibl" }, &Cmd_dx12_ibl);
            reg({ "dx12.sss" }, &Cmd_dx12_sss);
            reg({ "dx12.decal" }, &Cmd_dx12_decal);
            reg({ "dx12.ssr" }, &Cmd_dx12_ssr);
            reg({ "dx12.fog" }, &Cmd_dx12_fog);
            reg({ "dx12.skinning" }, &Cmd_dx12_skinning);
            reg({ "dx12.iblshade" }, &Cmd_dx12_iblshade);
            reg({ "dx12.ssgi" }, &Cmd_dx12_ssgi);
            reg({ "camera.editor" }, &Cmd_camera_editor);
            reg({ "render.backend" }, &Cmd_render_backend);
            reg({ "dx12.live" }, &Cmd_dx12_live);
            reg({ "render.livecheck" }, &Cmd_render_livecheck);
            reg({ "dx12.bench11" }, &Cmd_dx12_bench11);
            reg({ "dx12.encoderbench" }, &Cmd_dx12_encoderbench);
            reg({ "dx12.scene" }, &Cmd_dx12_scene);
            reg({ "render.rtinfo" }, &Cmd_render_rtinfo);
            reg({ "render.post" }, &Cmd_render_post);
            reg({ "render.exposure" }, &Cmd_render_exposure);
            reg({ "dx12.resize" }, &Cmd_dx12_resize);
            reg({ "dx12.parallel" }, &Cmd_dx12_parallel);
            reg({ "dx12.gbuffer" }, &Cmd_dx12_gbuffer);
            reg({ "dx12.rendergraph" }, &Cmd_dx12_rendergraph);
            reg({ "dx12.descriptorheap" }, &Cmd_dx12_descriptorheap);
            reg({ "window.info" }, &Cmd_window_info);
            reg({ "ui.status" }, &Cmd_ui_status);
            reg({ "dump.crash" }, &Cmd_dump_crash);
            reg({ "dump.list", "dump.show" }, &Cmd_dump_list);
            reg({ "animator.state" }, &Cmd_animator_state);
            reg({ "animator.exit" }, &Cmd_animator_exit);
            reg({ "animator.param" }, &Cmd_animator_param);
            reg({ "component.list" }, &Cmd_component_list);
            reg({ "prefab.create" }, &Cmd_prefab_create);
            reg({ "script.reload" }, &Cmd_script_reload);
            reg({ "script.status" }, &Cmd_script_status);
            reg({ "play", "stop" }, &Cmd_play);
            reg({ "play.state" }, &Cmd_play_state);
            reg({ "pipeline.nodes" }, &Cmd_pipeline_nodes);
            reg({ "undo.state" }, &Cmd_undo_state);
            reg({ "undo", "redo" }, &Cmd_undo_redo);
            reg({ "object.create.undoable" }, &Cmd_object_create_undoable);
            reg({ "scene.selection" }, &Cmd_scene_selection);
            reg({ "lifecycle.trace" }, &Cmd_lifecycle_trace);
            reg({ "lifecycle.registry" }, &Cmd_lifecycle_registry);
            reg({ "lifecycle.dump" }, &Cmd_lifecycle_dump);
            reg({ "lifecycle.stress" }, &Cmd_lifecycle_stress);
            reg({ "scene.dump" }, &Cmd_scene_dump);
            reg({ "gpu.baseline" }, &Cmd_gpu_baseline);
            reg({ "gpu.census", "gpu.delta" }, &Cmd_gpu_census);
            reg({ "assets.unload" }, &Cmd_assets_unload);
            reg({ "gc.stats", "gc.delta" }, &Cmd_gc_stats);
            reg({ "mem.stats", "mem.delta", "mem.reset", "mem.hook" }, &Cmd_mem_stats);
            reg({ "bt.status", "bt.reset" }, &Cmd_bt_status);
            reg({ "gc.collect" }, &Cmd_gc_collect);
            reg({ "log.flush" }, &Cmd_log_flush);
            reg({ "render.shadowinfo" }, &Cmd_render_shadowinfo);
            reg({ "crash.status" }, &Cmd_crash_status);
            reg({ "crash.test" }, &Cmd_crash_test);
            reg({ "reflect.golden" }, [](const ConsoleCommandContext& c) { HandleReflectGolden(c.parts); });
            reg({ "perf.reflect" }, [](const ConsoleCommandContext& c) { HandlePerfReflect(c.parts); });
            reg({ "scene.dirtytraversal" }, [](const ConsoleCommandContext& c) { HandleSceneDirtyTraversal(c.parts); });
            reg({ "scene.bonecache" }, [](const ConsoleCommandContext& c) { HandleSceneBoneCache(c.parts); });
            reg({ "prefab.objectguid" }, [](const ConsoleCommandContext& c) { HandlePrefabObjectGuid(c.parts); });
            reg({ "scene.traversalbench" }, [](const ConsoleCommandContext& c) { HandleSceneTraversalBench(c.parts); });
            reg({ "scene.bonedump" }, [](const ConsoleCommandContext& c) { HandleSceneBoneDump(c.parts); });
            reg({ "scene.transformdigest" }, [](const ConsoleCommandContext& c) { HandleSceneTransformDigest(c.parts); });
            reg({ "serialize.bench" }, [](const ConsoleCommandContext& c) { HandleSerializeBench(c.parts); });
            reg({ "serialize.nodeequal" }, [](const ConsoleCommandContext& c) { HandleSerializeNodeEqual(c.parts); });
            reg({ "serialize.parsercompare" }, [](const ConsoleCommandContext& c) { HandleSerializeParserCompare(c.parts); });
            reg({ "serialize.rymlerror" }, [](const ConsoleCommandContext& c) { HandleSerializeRymlError(c.parts); });
            reg({ "serialize.scalarparity" }, [](const ConsoleCommandContext& c) { HandleSerializeScalarParity(c.parts); });
            reg({ "serialize.adapterparity" }, [](const ConsoleCommandContext& c) { HandleSerializeAdapterParity(c.parts); });
            reg({ "scene.proxybench" }, [](const ConsoleCommandContext& c) { HandleSceneProxyBench(c.parts); });

            return t;
        }();
        return table;
    }
}

void ConsoleCommandSystem::Execute(const std::string& line)
{
    if (line.empty()) return;

    const auto parts = Split(line);
    if (parts.empty()) return;

    const std::string& cmd = parts[0];

    const auto& table = ConsoleCmd::GetTable();
    const auto it = table.find(cmd);
    if (it == table.end())
    {
        std::printf("[CLI] 알 수 없는 명령: %s  ('help' 참고)\n", cmd.c_str());
        return;
    }

    const ConsoleCommandContext ctx{ cmd, parts, line, *this };
    it->second(ctx);
}

void ConsoleCommandSystem::RequestQuit() noexcept
{
    m_quitRequested.store(true, std::memory_order_release);
}

void ConsoleCommandSystem::SetWaitFrames(int frames) noexcept
{
    m_waitFrames = frames;
}

bool ConsoleCommandSystem::IsEditorCameraFollowing() noexcept
{
    return g_editorCameraFollowsGame;
}

bool ConsoleCommandSystem::MatchEditorCameraToGameCamera()
{
    EditorCameraRig* editorRig = EditorSessionState::Get().CameraRig();
    Scene* activeScene = SceneManagers->GetActiveScene();
    CameraComponent* gameCamera = (nullptr != activeScene)
        ? activeScene->Cameras().GetPrimaryCamera() : nullptr;
    if (nullptr == editorRig || nullptr == gameCamera)
    {
        return false;
    }

    editorRig->ApplySnapshot(gameCamera->CaptureFrameSnapshot());
    return true;
}

void ConsoleCommandSystem::PrintHelp() const
{
    std::printf(
        "\n[CLI] 사용 가능한 명령\n"
        "  scene.new [이름]     빈 씬을 만들어 활성화한다(기능 테스트 씬 저작용)\n"
        "  scene.load <경로>    씬을 로드한다(활성 씬은 그대로)\n"
        "  scene.switch <경로>  씬을 로드하고 활성 씬으로 교체한다(언로드 유발)\n"
        "  scene.save <경로>    활성 씬을 .creator로 저장한다\n"
        "  scene.dump [라벨]    활성 씬의 오브젝트 계층을 로그에 남긴다\n"
        "  scene.dirtytraversal [0|1]  S2 A/B 토글 — dirty만 재계산(1,기본)/항상 재계산(0)\n"
        "  scene.bonecache [0|1]       E7-b A/B 토글 — 뼈 인덱스 캐시(1,기본)/매 프레임 FindBone(0)\n"
        "  scene.ddol <이름>           오브젝트를 DontDestroyOnLoad로 — 씬 이송 경로 시험용\n"
        "  scene.traversalbench <오브젝트수> <프레임수>  AllUpdateWorldMatrix 시간 측정(정지/10%이동, 0=합성 없이 현재 씬·정지만)\n"
        "  scene.bonedump [개수]        대조 덤프 — 뼈 오브젝트 이름 vs 스켈레톤 뼈 이름(조회 실패 진단)\n"
        "  scene.transformdigest [라벨]  활성 씬 전체의 트랜스폼 값 다이제스트(저장·재로드 대조용)\n"
        "  game.pak             Release Player 패키지를 빌드·검증 후 Build/Staging에 게시한다\n"
        "  model.load <경로>    모델을 에셋으로 임포트한다(fbx/gltf/glb/obj)\n"
		"  material.corpus.probe <이름>...  standalone material identity/reference 왕복\n"
		"  terrain.authoring.probe <이름> <텍스처|->  Terrain writer 트랜잭션 회귀 검사\n"
        "  model.place <이름>   임포트한 모델을 활성 씬에 배치한다\n"
        "  object.create <이름> [타입]  빈 오브젝트를 만든다(Empty/Light/Camera/Mesh)\n"
        "  object.rename <이전> <새>  오브젝트 이름을 바꾼다(같은 모델 여러 번 배치용)\n"
        "  object.transform <이름> <px py pz> [rx ry rz] [sx sy sz]  변환을 지정한다(회전은 도)\n"
		"  object.rootref <오브젝트> [루트|-]  Bone형 same-scene root 참조를 설정/조회한다\n"
		"  prefab.corpus.digest <라벨> <이름>...  prefab identity/override 왕복 digest\n"
        "  object.property <오브젝트> <컴포넌트> <필드> <값>  리플렉션으로 프로퍼티를 설정한다\n"
        "  play / stop          에디터의 재생·정지와 같은 동작\n"
        "  lifecycle.trace on [틱프레임]|off|clear|status  생명주기 호출 순서를 받아 적는다\n"
        "  lifecycle.registry on|off|status  생명주기 디스패치 경로 전환(9-1, 씬 재로드 필요)\n"
        "  lifecycle.dump [파일]  기록을 TSV로 쓴다(기록 0건이면 실패로 끝난다)\n"
        "  lifecycle.stress destroy|churn|reentrant [개수]  수명 경로를 흔든다(reentrant는 순회 한복판)\n"
        "  gc.stats|gc.delta [라벨]  관리 힙 지표(수집 횟수·힙 크기). delta는 첫 호출을 기준선으로\n"
        "  gc.collect           관리 힙 확정 수집(씬 전환이 자동으로 부르는 그 경로)\n"
        "  mem.stats|mem.delta [라벨]  CRT 힙의 live 블록·바이트\n"
        "  mem.reset            churn 누계와 기준선을 0으로 — 구간 측정용\n"
        "  mem.hook on|stack|off|status  CRT 할당 호출 계수. stack은 호출 스택까지(느리다)\n"
        "  mem.hook top [N]     할당 호출 지점 상위 N개를 심볼과 함께(stack 모드 필요)\n"
        "  bt.status            행동 트리 지표(트리 수·틱 누계·프레임당 경계 통과)\n"
        "  bt.reset             BT 누계만 0으로(트리는 그대로) — 구간 측정용\n"
        "  camera.editor match|follow on|off|status  에디터 카메라를 게임 카메라와 같은 시점으로\n"
        "  window.resize <너비> <높이>  창 클라이언트 크기를 바꾼다(해상도 검증용)\n"
        "  window.info          엔진이 인식하는 클라이언트 크기를 출력한다\n"
        "  profile.selftest     CPU 프로파일러 특성화 검사(중첩·멀티스레드·프레임경계·용량초과)\n"
        "                       ★ 프레임 경계를 넘으므로 라이브 캡처를 교란한다\n"
        "  profile.stats        프로파일러 자체 비용과 용량 소진(교란 없음)\n"
        "  experiment.model <경로>  legacy↔Experiment 모델 로딩 패리티 검증(검증 이슈·구조 diff·설계 갭 실측)\n"
        "  experiment.anim <경로>   Experiment clip 재생 배선 검증(legacy 참조 팔레트 파리티·포즈 변화)\n"
        "  experiment.import <경로> 임포트 경로 검증(legacy→ImportedScene→ModelDraft, 손실 계수·경로 비교)\n"
        "  experiment.gltf <경로>   fastgltf 임포터 검증(Assimp 기준선 대비 삼각형·AABB·이름 집합)\n"
        "  experiment.fbx <경로>    ufbx 임포터 검증(experiment.gltf 와 같은 게이트)\n"
        "  experiment.sampler       보간 합성 검사(Step/Linear 계단·강등·키 뭉침, 자산 무관)\n"
        "  experiment.tangent       탄젠트 합성 검사(mikktspace 축·handedness·이음매 분리, 자산 무관)\n"
        "  experiment.normal        평면 법선 합성 검사(감김·면 분리·퇴화 처리, 자산 무관)\n"
        "  experiment.weld                정점 용접 — 합치는가/안 합치는가(실자산은 0건이라 이것만이 증거다)\n"
        "  experiment.cacheopt            정점 캐시/페치 순서 — ACMR 이 실제로 낮아지는가 + 기하 보존\n"
        "  experiment.texcook [루트 텍스처]  텍스처 쿠킹 — GUID 주소·내용 해시·fail-closed(실자산은 .dds 미포함)\n"
        "  experiment.smcook [루트 메타]    ShaderMeta 쿠킹 — 정본 파서 검증·source 해소(실자산엔 거부 사례 0)\n"
        "  experiment.matcook [루트 재질 모델] 재질 의존 폐포 — standalone 재질 + 모델의 임베디드 texture 추출\n"
        "  experiment.scenecook [루트 씬]   scene/prefab 의존 추출 — 자기참조 제외·못 그린 참조 계수\n"
        "  experiment.resolver             CookedThenSource resolver — 호출 순서와 폴백 관측(자산 무관)\n"
        "  experiment.catalog [Derived부모] CEMF catalog — 전 GUID 해석·내용 검증·폐포 위상 순서\n"
        "  experiment.cooked [경로]        쿠킹 포맷 왕복 무손실·거부 동작(경로를 주면 실자산 왕복까지)\n"
        "  experiment.bench <경로> [반복]  legacy 로드 대 Experiment 경계 비용(브리지·Validate·게시·포즈 샘플링)\n"
        "  dx12.selftest [파일]  DX12 브링업 자가 검증(삼각형 렌더 → PNG)\n"
        "  vk.selftest [파일]    Vulkan 골격 자가 검증(디바이스·중립 서비스 경로·스왑체인 → PNG)\n"
        "  vk.parallel          Vulkan RenderGraph 병렬 command pool·제출·픽셀 검증\n"
        "  vk.grid              그리드 패스를 Vulkan 으로 — dx12.grid 와 픽셀 대조(5d)\n"
        "  vk.skybox            스카이박스를 Vulkan 으로 — 큐브 SRV·정적 샘플러 픽셀 대조\n"
        "  vk.ibl               IBL 생성기를 Vulkan 으로 — 면·밉·LUT DX12 픽셀 대조\n"
        "  vk.gizmoicon         실제 Camera Gizmo PNG — 2D SRV·root instance 픽셀 대조\n"
        "  vk.gizmoline         GizmoLine 공용 패스 — line-list 전체 RGBA DX12/Vulkan 대조\n"
        "  vk.wireframe         WireFrame 공용 패스 — non-solid fill·skinning DX12/Vulkan 대조\n"
        "  vk.ui                UI 공용 패스 — layer·blend·texture batch DX12/Vulkan 대조\n"
        "  vk.shadow            Shadow 공용 패스 — depth array·mesh DX12/Vulkan 대조\n"
        "  vk.gbuffer           GBuffer 공용 패스 — MRT5·texture·sampler·mesh DX12/Vulkan 대조\n"
        "  vk.forward           Forward+ 공용 패스 — compute·buffer·blend·mesh DX12/Vulkan 대조\n"
        "  vk.deferred          Deferred 공용 패스 — GBuffer consume·fullscreen DX12/Vulkan 대조\n"
        "  vk.decal             Decal 공용 패스 — GBuffer snapshot·depth-read·MRT blend 대조\n"
        "  vk.ssao              SSAO 공용 패스 — depth/normal compute·filter DX12/Vulkan 대조\n"
        "  vk.sss               SSS 공용 패스 — 2축 blur·depth gate 전체 픽셀 대조\n"
        "  vk.ssr               SSR 공용 패스 — ray hit·metal/thickness/bitmask 픽셀 대조\n"
        "  vk.fog               VolumetricFog 공용 패스 — 3D scatter/history/composite 대조\n"
        "  vk.post              PostChain 공용 패스 — bloom/tonemap/vignette/FXAA 대조\n"
        "  vk.ssgi              SSGI 공용 패스 — Hi-Z·temporal·filter·composite 대조\n"
        "  dx12.psocache [파일]  PSO 캐시 자가 검증(2회차 컴파일 0건)\n"
        "  rhi.uploadsegments  DX12/Vulkan 완료점 기반 업로드 세그먼트 공통 검증\n"
        "  dx12.uploadring      구 명령 별칭(DX12 업로드 세그먼트 검증)\n"
        "  dx12.descriptorheap  descriptor version recycler 검증(completion·Abort·격리·넘침)\n"
        "  dx12.rendergraph     렌더 그래프 검증(순서·흐름·배리어·컬링·실행)\n"
        "  dx12.gbuffer         GBuffer 패스 검증(입력조립·MRT5·깊이·그래프 배리어)\n"
        "  dx12.resize          크기 추종 검증(DX11 정책·DX12 리사이즈·리사이즈 후 렌더)\n"
        "  dx12.parallel        커맨드 기록 병렬화 검증(링 원자성·순차 대비 동일성)\n"
        "  render.exposure      자동 노출이 무엇을 재고 무엇을 결정했는지\n"
        "  render.post           비활성 레거시 명령(DX11 포스트 설정)\n"
        "  render.rtinfo        창·뷰포트·추종 텍스처 크기를 나란히 찍는다\n"
        "  dx12.scene           씬 연결 검증(카메라 스냅샷·메시 업로드·실제 드로우)\n"
        "  dx12.grid            그리드 패스 검증(라인·셀 내부·밀도·카메라 반응)\n"
        "  dx12.gizmoline       기즈모 라인 패스 검증(도형 정점 수·픽셀·드로우 병합)\n"
        "  dx12.gizmoicon       기즈모 아이콘 패스 검증(빌보드 회전·알파 상한·배칭)\n"
        "  dx12.wireframe       와이어프레임 패스 검증(변·내부 비채움·인스턴싱·메시 캐시)\n"
        "  dx12.gizmoscene      Gizmo 씬 연결 검증(밀봉 복사·4패스 체인·타깃 공유)\n"
        "  dx12.shadowquality   그림자 품질 검증(경사 비례 편향·캐스케이드 경계 블렌딩 A/B)\n"
        "  dx12.skybox          스카이박스 패스 검증(면 방향·원평면 밀어넣기·전면 커버)\n"
        "  dx12.ibl             IBL 생성 체인 검증(rect→cube·조도·프리필터·BRDF LUT)\n"
        "  dx12.iblshade        IBL 앰비언트 소비 검증(끔=검정·조도 방향성·금속 정반사)\n"
        "  dx12.skinning        GBuffer 스키닝 검증(본 이동·가중 혼합·비스킨드 불변)\n"
        "  dx12.sss             SSS 패스 검증(번짐·축 분리·표면 추종·에너지)\n"
        "  dx12.decal           데칼 패스 검증(상자 판정·하늘 게이트·원본 혼합 3종·배칭)\n"
        "  dx12.ssr             SSR 패스 검증(반사 발생·금속 마스크·두께 게이트·비트플래그)\n"
        "  dx12.fog             볼류메트릭 포그 검증(산란·누적 투과율·시간축 히스토리·합성)\n"
        "  dx12.bench11         DX11 vs DX12 API 오버헤드 실측(전제 검증 · Release 전용)\n"
        "  dx12.encoderbench    인코더 오버헤드 실측(R3 착수 조건 · Release 전용)\n"
        "  dx12.live on|status      EnhancedRenderer 메인 런타임 상태\n"
        "  render.livecheck [너비 높이]  resize·다중 뷰·표시 슬롯 회전 회귀 판정\n"
        "  render.backend status      부팅 시 고정된 scene/ImGui RHI 조회(변경은 Settings)\n"
        "  pix.capture begin|end|status  PIX 주입 실행의 명시적 GPU 캡처 경계\n"
        "  ui.rect <오브젝트|*>  오브젝트 이하의 worldRect·sizeDelta·앵커·배율을 출력한다\n"
        "  ui.anchor <오브젝트> <minX> <minY> <maxX> <maxY>  앵커를 직접 지정한다\n"
        "  ui.size <오브젝트> <x> <y>  sizeDelta를 직접 지정한다\n"
        "  ui.hitbox            버튼의 rect와 클릭 판정 상자를 나란히 출력한다\n"
        "  script.add <오브젝트> <타입>  C# 스크립트를 오브젝트에 부착한다\n"
        "  script.fields <id>   스크립트의 노출 필드와 현재 값을 확인한다\n"
        "  script.set <id> <인덱스> <값>  노출 필드 값을 바꾼다\n"
        "  script.reload        게임 스크립트 어셈블리를 다시 로드한다(핫리로드)\n"
        "  script.status        CLR 상태와 활성 스크립트 수를 확인한다\n"
        "  gpu.baseline         현재 상태를 기준선으로 삼는다\n"
        "  gpu.census [라벨]    VRAM과 엔진 에셋 수를 로그에 기록\n"
        "  gpu.delta [라벨]     기준선 대비 증감을 기록(씬 왕복 누수 확인용)\n"
        "  assets.unload        사용하지 않는 에셋 캐시 정리\n"
        "  wait <프레임>        지정 프레임만큼 다음 명령을 미룬다\n"
        "  log.flush            로그를 디스크에 즉시 반영\n"
        "  render.shadowinfo    그림자 캐스케이드 계산 결과를 출력한다(스냅샷 검증용)\n"
        "  crash.status         크래시 덤프 기록자 등록 여부와 덤프 경로를 확인한다\n"
        "  crash.test <종류>    일부러 죽여 덤프 경로를 검증한다(av|abort|terminate|throw)\n"
        "  quit                 에디터 종료\n"
        "\n실행 인자: --exec \"<명령>\"  |  --script <파일>  |  --console\n"
        "           --heapcheck  CRT 디버그 힙 전수 검사(힙 손상을 손상 시점에서 잡는다, 매우 느림)\n\n");
}

void ConsoleCommandSystem::Shutdown()
{
    m_running.store(false, std::memory_order_release);

    if (!m_stdinThread.joinable()) return;

    // 블로킹 중인 콘솔 읽기를 깨운다.
    //
    // 예전에는 깨우지 않고 그냥 detach했다. 그러면 이 스레드는 getline 안 —
    // 즉 CRT의 stdio·iostream 내부 — 에 갇힌 채로 남고, 프로세스가 죽을 때
    // ExitProcess가 그 임의 지점에서 스레드를 강제 종료한다. 하필 힙 락을 쥔
    // 순간이었다면 뒤이은 종료 절차가 그 위에서 힙을 만지게 되고, 결과는
    // 종료 구간의 간헐적 힙 손상(0xC0000374)이다. 회귀 세트가 전부 CLI
    // 실행이라 이 경로에서만 나타났고, 에디터를 손으로 쓸 때는 보이지 않았다.
    ::CancelSynchronousIo(static_cast<HANDLE>(m_stdinThread.native_handle()));

    // 취소가 항상 듣는다는 보장은 없어서(콘솔 구현에 따라 다르다) 기한을 둔다.
    // 종료가 통째로 막히는 것은 더 나쁘다.
    constexpr auto kJoinTimeout = std::chrono::milliseconds(500);
    const bool exited = m_stdinDoneFuture.valid()
        && m_stdinDoneFuture.wait_for(kJoinTimeout) == std::future_status::ready;

    if (exited)
    {
        m_stdinThread.join();
        return;
    }

    // 최후의 수단. 여기까지 오면 예전과 같은 위험이 남지만, 적어도 스크립트
    // 실행 경로(--script/--exec)에는 이 스레드가 아예 없다.
    std::fputs("[CLI] 표준 입력 스레드를 회수하지 못했다 - 종료 중 위험 구간.\n", stdout);
    std::fflush(stdout);
    m_stdinThread.detach();
}
