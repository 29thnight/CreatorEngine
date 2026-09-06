#pragma once
#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

#include "../Graph/EnhancedRenderGraph.h"
#include "../Graph/EnhancedRenderPass.h"

// 라이브 파이프라인 기술(記述) — LivePipelineDesc (PHASE 3-10, 슬라이스 1).
//
// ── 이것은 그래프가 아니다 ──
//
// 살아 있는 그래프는 EnhancedRenderGraph 하나다(옛 RenderGraphBuilder는 참조
// 0으로 제거됐다). 이 층은 순서를 유도하지 않는다 — 노드 목록의 순서가 곧
// 실행 순서이고, 그것은 3-5가 정한 계약("실행 순서 = 선언 순서")을 위층에서
// 그대로 잇는 것이다. 순서를 유도하지 않으므로 그래프가 아니라 기술이다.
//
//   LivePipelineDesc      영속. 무엇을 · 어떤 순서로 · 꺼지면 어떻게 흘릴지  ← 조립
//   EnhancedRenderGraph   프레임당. 그 선언을 받아 배리어 · 컬링 · 실행       ← 실행
//
// 그래프에 흡수하지 않은 근거 셋(LivePipelineDescPlan.md §0):
//   ① 어휘가 다르다 — AddPass는 패스 객체가 아니라 해석이 끝난 핸들과 람다를
//      받는다. EnhancedRenderPass도, 수명도, 활성 조건도 그래프는 모른다.
//   ② 수명이 정반대다 — 그래프는 프레임마다·슬롯마다 새로 서고(최대 6개 동시),
//      조립은 파이프라인이 설 때 한 번 정해진다.
//   ③ 그래프를 직접 세우는 자가 검증이 30여 곳이다. 거기 패스 수명·활성
//      조건을 얹으면 검증된 호출 지점이 전부 흔들린다.
//
// ── 왜 필요한가 ──
//
// 배선이 RenderOnce에 하드코딩돼 있었다. 패스 하나를 이으려면 흩어진 접점
// 10종을 찾아 고쳐야 했고, 그중 순서를 사람이 지켜야 하는 곳이 넷인데
// (Initialize · Shutdown 역순 · PrepareFrame · Declare) 셋은 틀려도 컴파일러가
// 침묵한다. 같은 사실("SSS는 Forward 뒤")이 세 곳에 따로 적히고 셋을 잇는
// 것은 사람의 기억뿐이었다.

/// 블랙보드 슬롯 이름. 문자열 오타는 Validate가 세울 때 잡지만(발행 안 된
/// 슬롯 읽기 = 오류), 상수로 두면 오타 자체가 컴파일 오류가 된다.
///
/// 여기(공개 헤더)에 있는 이유: 슬롯 이름은 노드 사이의 계약이고, 파이프라인에
/// 노드를 기여하는 Host(IRenderFeatureContributor)도 같은 계약으로 잇는다.
/// 기여자끼리만 쓰는 슬롯(예: 기즈모 깊이)은 이 어휘에 넣지 않는다 — Core가
/// 몰라야 하는 이름이다.
namespace LiveSlots
{
    constexpr const char* kGBufferDiffuse    = "GBuffer.Diffuse";
    constexpr const char* kGBufferMetalRough = "GBuffer.MetalRough";
    constexpr const char* kGBufferNormal     = "GBuffer.Normal";
    constexpr const char* kGBufferEmissive   = "GBuffer.Emissive";
    constexpr const char* kGBufferBitmask    = "GBuffer.Bitmask";
    constexpr const char* kGBufferDepth      = "GBuffer.Depth";

    constexpr const char* kShadowMap         = "Shadow.Map";
    constexpr const char* kAmbientOcclusion  = "Scene.AmbientOcclusion";

    /// 라이팅 결과. Deferred가 발행하고 SkyBox·SSGI·Forward+·SSS·SSR·Fog가
    /// 차례로 수정한다 — 지금 코드의 litColor 체인이 이 슬롯 하나다.
    constexpr const char* kLitColor          = "Scene.LitColor";

    /// 포스트 체인 뒤의 LDR. 화면 UI와 기여 오버레이가 이 위에 얹힌다.
    constexpr const char* kDisplayLdr        = "Display.LDR";
}

/// 뷰의 표시 성격(E4-2). viewIndex로 갈음할 수 없다 — 뷰 배정은 회전·교체가
/// 있어 인덱스가 카메라 정체를 말하지 않는다. 이름이 중립인 이유: RenderCore는
/// "에디터"를 모른다 — 씬 오버레이 노드를 기여하는 것은 Host다.
namespace LiveViewFlags
{
    /// 이 뷰에 화면 공간 게임 UI를 합성한다(플레이어가 볼 화면).
    constexpr uint32_t kScreenSpaceUI = 1u << 0;

    /// Host가 기여한 씬 오버레이 노드가 이 뷰에 그린다(저작 보조 뷰).
    constexpr uint32_t kSceneOverlay  = 1u << 1;
}

/// 프레임마다 비우고 다시 채우는 이름 → 핸들 표.
///
/// 값 복사가 싼 RGHandle만 담는다. 사이드밴드(그림자 행렬 · IBL 포인터 ·
/// 데칼 목록)는 담지 않는다 — 일반화하려면 타입 소거가 필요한데 얻는 것은
/// 덤프 한 줄이고 잃는 것은 컴파일 타임 타입 검사다. 사이드밴드는 순서
/// 오류가 포인터 타입으로 잡히므로 이 층의 동기인 '조용한 순서 실패'가
/// 애초에 없다.
class LiveBlackboard
{
public:
    void Reset() { m_slots.clear(); }

    /// 발행·수정 공용. 수정이 별도 함수가 아닌 이유는 값이 갈리지 않아서다 —
    /// 무엇이 발행이고 무엇이 수정인지는 노드의 writes/modifies가 말하고,
    /// Validate가 그것으로 판정한다.
    void Set(const std::string& slot, RGHandle handle) { m_slots[slot] = handle; }

    /// 없으면 무효 핸들. 슬롯 이름 오타는 여기서 조용히 무효가 되지만,
    /// Validate가 파이프라인을 세울 때 이미 잡았어야 하는 종류다.
    RGHandle Get(const std::string& slot) const
    {
        const auto found = m_slots.find(slot);
        return (m_slots.end() == found) ? RGHandle{} : found->second;
    }

private:
    std::unordered_map<std::string, RGHandle> m_slots;
};

/// 프레임마다 달라지는 값. 노드 정의는 정적이어야 하므로 이런 것은 캡처가
/// 아니라 인자로 흐른다.
///
/// sharedTarget이 여기 있는 이유: 표시 슬롯은 프레임마다 회전한다(뷰당 3개).
/// 노드가 그것을 캡처하면 노드 정의가 프레임에 묶여 데이터화의 의미가 없어진다.
struct LiveFrameBinding
{
    uint32_t         viewIndex{ 0 };
    RHITextureHandle sharedTarget{};
    RHIReadback      readbackTarget{};

    // 뷰의 표시 성격(LiveViewFlags 비트 조합). 화면 UI는 kScreenSpaceUI 뷰에만,
    // Host가 기여한 씬 오버레이는 kSceneOverlay 뷰에만 그린다 — 게임 뷰는
    // 플레이어가 볼 화면이라 저작 보조물이 나오면 안 된다(E4-2에서
    // isEditorView를 중립 비트로 교체했다).
    uint32_t        viewFlags{ 0 };
};

/// 패스 하나의 어댑터. 데이터 절반(검증·덤프의 근거)과 접착 절반(타입)이 있다.
struct LivePassNode
{
    std::string name;

    // ── 데이터 절반 ──

    /// 앞선 노드가 발행해 두어야 읽을 수 있다.
    std::vector<std::string> reads;

    /// 이 노드가 처음 발행한다. 같은 슬롯을 둘이 발행하면 Validate가 잡는다.
    std::vector<std::string> writes;

    /// 읽고 → 같은 이름으로 재발행한다.
    ///
    /// ★ 꺼질 수 있는 노드는 이것만 가질 수 있다(Validate가 강제한다).
    ///   꺼졌을 때 소비자가 끊기지 않는 것은 슬롯 값이 그대로 남을 때뿐이고,
    ///   그것이 지금 배선의 `X.GetOutput().IsValid() ? X : 이전값` 폴백이
    ///   하던 일이다 — 규약을 코드가 아니라 규칙으로 올린다.
    std::vector<std::string> modifies;

    /// 뷰마다 인스턴스를 따로 두는가(시간축 상태를 가진 패스 — SSGI · Fog).
    ///
    /// InitializeAll이 뷰 수만큼 돌고, PrepareAll/DeclareAll이 viewIndex로
    /// 고른다. 공유 패스는 뷰가 둘이어도 한 번만 선다.
    bool perView{ false };

    // ── 접착 절반 ──

    /// 비활성이면 declare를 건너뛴다. 비면 항상 활성.
    std::function<bool()> active;

    /// 이 노드가 미는 패스. viewIndex로 뷰당 인스턴스를 고른다(공유면 무시).
    ///
    /// 이것만 있으면 Initialize/PrepareFrame/Shutdown의 기본 구현이 성립한다 —
    /// 그 셋을 따로 적는 노드는 특별한 사정이 있는 것뿐이다(포그의 지연
    /// 초기화, 데칼의 SetDecals 선행). 패스가 없는 노드(live_present)는 비운다.
    std::function<EnhancedRenderPass*(uint32_t viewIndex)> instance;

    /// 기본(instance->Initialize)을 대신할 초기화. 포그처럼 처음 켜질 때까지
    /// 자원을 잡지 않아야 하는 노드가 빈 람다를 넣어 기본을 끈다.
    std::function<bool(const EnhancedFrameContext&, std::string&, uint32_t viewIndex)> initialize;

    /// 기본(instance->PrepareFrame)을 대신할 프레임 준비.
    std::function<bool(const EnhancedFrameContext&, std::string&, uint32_t viewIndex)> prepare;

    /// 기본(instance->Shutdown)을 대신할 해제.
    std::function<void()> shutdown;

    /// 블랙보드에서 꺼내 타입 있는 Inputs로 옮기고, 패스의 Declare를 부르고,
    /// 결과를 다시 블랙보드에 넣는다. 지금 RenderOnce의 블록 하나와 일대일이다.
    std::function<void(LiveBlackboard&, EnhancedRenderGraph&,
        const EnhancedFrameContext&, const LiveFrameBinding&)> declare;

    bool IsActive() const { return !active || active(); }
};

struct LivePassNodeSnapshot
{
    std::string name;
    bool conditional{}, active{}, perView{};
    std::vector<std::string> reads, writes, modifies;
};

class LivePipelineDesc
{
public:
    void AddNode(LivePassNode node) { m_nodes.push_back(std::move(node)); }
    void Clear() { m_nodes.clear(); }
    bool IsEmpty() const { return m_nodes.empty(); }
    size_t NodeCount() const { return m_nodes.size(); }

    /// 파이프라인을 세울 때 한 번. 노드 순서대로 걸으며 발행 집합을 유지한다.
    ///
    /// 잡는 것 셋:
    ///   · 아직 발행 안 된 슬롯을 reads/modifies가 짚는다 → 순서 실수다.
    ///     지금은 무효 핸들이 조용히 흘러 패스의 "입력이 모자라면 입력을
    ///     흘려보낸다" 폴백에 삼켜지고, 그림만 이상해진다.
    ///   · 꺼질 수 있는 노드(active 보유)가 writes를 가진다 → 꺼진 프레임에
    ///     소비자가 무효 핸들을 받는다.
    ///   · 같은 슬롯을 둘이 writes한다 → 둘째는 수정이므로 modifies로 적어야
    ///     하고, 그래야 '꺼지면 값 유지' 규약이 성립한다.
    bool Validate(std::string& outError) const;

    /// 노드 순서대로 Initialize. perView 노드는 뷰 수만큼 돈다.
    ///
    /// ★ 순서를 여기서 유도하는 것이 슬라이스 2의 요점이다. 예전에는
    ///   Initialize · PrepareFrame · Shutdown 역순 · Declare 네 곳에 같은
    ///   순서가 따로 적혀 있었고, 셋은 틀려도 컴파일러가 침묵했다.
    bool InitializeAll(const EnhancedFrameContext& context, uint32_t viewCount,
        std::string& outError) const;

    /// 노드 순서대로 PrepareFrame. 비활성 노드도 준비는 한다 —
    /// 포그처럼 '이번 프레임에 켜질지'를 준비 단계가 정하는 경우가 있다.
    bool PrepareAll(const EnhancedFrameContext& context, uint32_t viewIndex,
        std::string& outError) const;

    /// 역순 Shutdown. 순서를 사람이 뒤집어 적던 자리가 사라진다 —
    /// 그 순서가 틀리면 '가끔 죽는' 종류의 실패였다.
    /// viewCount는 InitializeAll에 준 것과 같아야 한다(대칭).
    void ShutdownAll(uint32_t viewCount) const;

    /// 노드 순서대로 declare를 부른다. 비활성 노드는 건너뛴다 —
    /// 그 노드의 modifies 슬롯은 값이 그대로 남아 뒤 노드가 이어받는다.
    void DeclareAll(LiveBlackboard& blackboard, EnhancedRenderGraph& graph,
        const EnhancedFrameContext& context, const LiveFrameBinding& binding) const;

    /// 렌더 디버그 창용. 노드 이름 · 활성 상태 · 슬롯 연결을 사람이 읽는 형태로.
    ///
    /// 이 덤프가 없으면 "이 패스가 어디 끼는지"를 알려면 RenderOnce를 정독해야
    /// 한다. 기본 폰트가 라틴 전용인 디버그 창에서 읽히도록 출력 어휘는 영문이다.
    std::string Dump() const;
    std::vector<LivePassNodeSnapshot> SnapshotNodes() const
    {
        std::vector<LivePassNodeSnapshot> result;
        result.reserve(m_nodes.size());
        for (const auto& node : m_nodes)
            result.push_back({node.name, bool(node.active), node.IsActive(), node.perView, node.reads, node.writes, node.modifies});
        return result;
    }

    /// GPU·활성 scene 없이 Validate와 Dump의 계약을 독립 검증한다.
    /// 정상 기술, 이름 없음, 비활성 가능 노드의 새 슬롯 발행, 미발행
    /// read/modify, 중복 발행, declare 누락과 덤프 순서·상태·슬롯을 검사한다.
    static bool RunSelfTest(std::string& outLog);

private:
    std::vector<LivePassNode> m_nodes;
};

