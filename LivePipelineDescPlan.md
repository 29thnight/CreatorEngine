# 라이브 파이프라인 기술(記述)화 설계 (LivePipelineDesc)

2026-08-07. SSR·SSS·Decal 배선 직후 작성 — 그 배선이 이 설계의 실측 근거다.

---

## 0. 이름부터 — 그래프는 하나다

처음 이 문서는 새 층을 `LivePipelineGraph`라고 불렀다. **틀린 이름이라 고쳤다.**

이 코드베이스에는 한때 그래프가 둘이었다. 옛 `RenderGraphBuilder`(싱글턴 ·
배리어 유도 없음 · 리소스 타입이 자리표시자)와 `EnhancedRenderGraph`. 전자는
참조 0곳으로 죽어 제거됐고, **지금 살아 있는 그래프는 `EnhancedRenderGraph`
하나다.** 새 층에 "그래프"를 붙이면 그 수가 다시 둘로 보인다.

이름이 틀렸던 더 정확한 이유: **이 층은 순서를 유도하지 않는다.** 노드 목록의
순서가 곧 실행 순서다(3-5 계약 그대로 — §2 비목표 참조). 순서를 유도하지 않는
것은 그래프가 아니라 **기술(記述)** 이다. 그래서 `LivePipelineDesc`다.

관계는 이렇다:

```
LivePipelineDesc      (신규) 영속. 무엇을 · 어떤 순서로 · 꺼지면 어떻게 흘릴지  ← 조립
EnhancedRenderGraph   (기존) 프레임당. 그 선언을 받아 배리어 · 컬링 · 실행      ← 실행
```

### 왜 EnhancedRenderGraph에 흡수하지 않는가

같은 클래스에 넣지 않은 근거 셋. 전부 코드의 사실이다.

**① 어휘가 다르다.** `AddPass(이름, usages, 콜백)`가 그래프의 입구다 — 패스
*객체*가 아니라 이미 해석이 끝난 핸들과 기록 람다를 받는다. 그래프는
`EnhancedRenderPass`라는 타입도, Initialize/PrepareFrame/Shutdown도, "SSR이 꺼져
있다"도, 뷰당 인스턴스도 모른다. 지금 하드코딩된 것이 정확히 그 모르는 것들이다.

**② 수명이 정반대다.** 그래프는 프레임마다 · 슬롯마다 새로 만들어지고
(`EnhancedSceneRendererLive.cpp`의 `slot.graph = std::make_unique<...>()`), 뷰 2 ×
슬롯 3이라 최대 6개가 동시에 산다. 소멸자가 transient를 풀에 반납하는 것이
계약이다. 조립은 반대로 파이프라인이 설 때 한 번 정해지고 리사이즈 때만 다시
선다. 흡수하려면 그래프를 영속화해 transient 풀·stateWriteback·Reset 계약을 다시
쓰거나, 조립을 프레임마다 ×6 재구축해야 한다 — 전자는 검증된 핵심을 다시 쓰는
일이고 후자는 이득을 버리고 비용만 남긴다.

**③ 호출 지점이 39곳이다.** 자가 검증 30여 개가 `EnhancedRenderGraph graph;`를
스택에 만들어 손으로 배선한다. 그 손배선은 결함이 아니라 목적이다(무엇이
연결됐는지 전부 통제해야 판정이 성립한다). 그래프에 패스 수명·활성 조건·슬롯
해석을 얹으면 검증된 39개 호출 지점의 API가 흔들린다. 별도 층이면 건드리는 곳은
라이브 런타임 하나다.

### 왜 "Live" 접두사인가

`Live`는 이 코드베이스의 기존 어휘다. `EnhancedSceneRenderer`에는 표면이 둘이고
접두사가 그 구분이다 — `Run*`은 콘솔 명령마다 스택에 새로 만들어 한 번 돌고 죽는
진단, `*Live*`(`TickLive` · `LiveState` · `LivePipeline` · `EnhancedLiveTuning`)는
상태를 가지고 매 프레임 도는 메인 런타임. 새 층이 기술하는 대상이 문자 그대로
`struct LivePipeline`의 조립이고, 접착 람다가 `LivePipeline&`과 `LiveState`를
캡처하므로 수명도 그 안이다. 이름이 그 범위를 말하게 둔다.

`RenderPipelineDesc` 같은 범용 이름을 피한 이유: 39개 `Run*` 테스트는 이 층을
쓰지 않으며 앞으로도 그렇다. 범용 이름은 "테스트도 이걸로 배선하면 되겠네"라는
거짓 신호를 준다.

★ 단, `Live`는 전환기 어휘이기도 하다. 헤더가 "교체(3-9) 때 이 API가 본체
인스턴스로 흡수된다"고 적어 두었다. 그때 `TickLive`·`LiveState` 등 수십 개
이름이 함께 정리되므로 **이 층도 그 rename 목록에 포함된다** — 기계적 치환이고,
그 시점까지는 Live가 사실을 말하는 이름이다.

---

## 1. 문제 — 배선이 코드에 박혀 있다

지금 구조는 두 층인데, 아래층은 그래프이고 **위층은 아무것도 아니다**:

```
EnhancedRenderGraph   프레임 단위. 배리어 유도 · 컬링 · transient 수명. (완성, 건드리지 않음)
RenderOnce (C++)      파이프라인 조립. 어떤 패스를 어떤 순서로 어떻게 잇는가. (하드코딩)
```

패스 하나를 라이브에 이으려면 `EnhancedSceneRendererLive.cpp` 안에서만 흩어진
자리를 전부 찾아 고쳐야 한다. SSR·SSS·Decal 셋을 이은 이번 작업이 그 수를 직접
알려 준다: **한 파일에 hunk 19개**, 접점 종류로는:

| # | 접점 | 이번에 고친 곳 |
|---|------|----------------|
| 1 | `#include` | 3건 |
| 2 | `LivePipeline` 멤버 선언 | decal·sss·ssr |
| 3 | `Initialize` 호출 + 순서 | 3건 (+환경변수 초기값) |
| 4 | `Shutdown` 호출 + **역순 유지** | 2곳에 나눠서 (ssr·sss와 decal 자리가 다르다) |
| 5 | `CaptureFromCamera` 씬 수집 | decal 프록시 복사 |
| 6 | `PrepareFrame` 호출 + 순서 | 2곳 (decal은 SetDecals가 앞서야 한다) |
| 7 | `Declare` 배선 (SetInputs + 폴백 체인) | 3곳 |
| 8 | `ApplyAndPublishTuning` 적용 + 미러 되읽기 | 2블록 |
| 9 | `EnhancedLiveTuning` 구조 (헤더) | Sss·Ssr 추가 |
| 10 | 디버그 창 · status 출력 | 2파일 |

10종의 접점 중 **순서를 사람이 지켜야 하는 곳이 넷**(3·4·6·7)이고, 셋이 틀리면
컴파일러가 아무 말도 하지 않는다. Shutdown 순서가 틀리면 가끔 죽고, PrepareFrame
순서가 틀리면 한 프레임 늦은 데이터가 보이고, Declare 순서가 틀리면 그림이
'조금 이상하다'로만 드러난다 — 전부 이 코드베이스가 가장 경계해 온 부류의 실패다.

또 하나. 같은 정보가 세 번 적힌다. "SSS는 Forward 뒤"라는 사실이 PrepareFrame
순서에 한 번, Declare 순서에 한 번, Shutdown 역순에 한 번 들어 있고, 셋을 잇는
것은 사람의 기억뿐이다.

## 2. 목표 / 비목표

**목표**

- 파이프라인 조립(노드 목록·연결·켬끔 조건·뷰 정책)을 **한 곳의 데이터**로 모은다.
  패스 하나를 잇는 일 = 노드 하나를 목록에 추가하는 일.
- Initialize / PrepareFrame / Declare / Shutdown 순서를 **그 데이터에서 유도**한다.
  순서를 사람이 네 곳에서 따로 지키는 일을 없앤다.
- 연결을 **세울 때 검증**한다. 아무도 발행하지 않은 슬롯을 읽는 노드는 파이프라인
  구축 시점에 오류로 잡힌다 — 지금은 무효 핸들이 조용히 흘러 "입력이 모자라면
  입력을 흘려보낸다" 폴백에 삼켜진다.
- `dx12.live status`의 패스 이름 목록에 더해, **노드·슬롯·활성 상태**를 콘솔로
  덤프할 수 있게 한다(`dx12.pipeline`).

**비목표 (이번 단계에서 하지 않는 것)**

- **패스 재정렬 없음.** EnhancedRenderGraph의 계약(실행 순서 = 선언 순서)을
  이 층에서도 그대로 따른다. 노드 목록의 순서가 곧 실행 순서다.
  위상 정렬로 순서를 유도하려다 뒤집은 3-5의 교훈을 반복하지 않는다 —
  같은 슬롯을 두 노드가 수정하면 순수 데이터 흐름으로는 순서가 정해지지 않는다.
  **이것이 이 층을 "그래프"라 부르지 않는 이유이기도 하다(§0).**
- **파일 직렬화(JSON/YAML) 없음.** 지금 파이프라인은 하나뿐이고 편집 주체도
  없다. 데이터 파일은 소비자(에디터 노드 UI 또는 프로젝트별 파이프라인)가
  생길 때 붙인다 — 노드 목록이 이미 데이터이므로 그때 직렬화만 얹으면 된다.
- **패스 클래스 개조 없음.** 타입 있는 `Inputs` 구조체와
  Initialize/PrepareFrame/Declare/Shutdown 규약은 그대로 둔다. 자가 검증
  (dx12.sss 등)이 패스를 직접 부르는 경로가 살아 있어야 하고, 문자열 슬롯을
  패스 안으로 밀어 넣으면 그 검증들이 전부 어댑터를 통과해야 하게 된다.
- **Tuning 경로 개편 없음.** ApplyAndPublishTuning은 지금 구조(뮤텍스 + 미러)를
  유지한다. 노드화하면 좋아지는 것이 명확해질 때 다시 본다.

## 3. 핵심 관찰 — 지금 배선은 이미 세 가지 패턴뿐이다

RenderOnce의 Declare 구간을 읽으면 연결이 전부 세 형태로 환원된다:

**① 발행(write)** — 새 리소스를 만들어 이름을 준다.
```
GBuffer  → GBuffer.Diffuse / .MetalRough / .Normal / .Emissive / .Bitmask / .Depth
Shadow   → Shadow.Map (+ 사이드밴드 EnhancedShadowData)
Deferred → Scene.LitColor (최초 발행)
PostChain→ Display.LDR
```

**② 수정(modify)** — 이름 있는 슬롯의 현재값을 읽어 새 핸들로 갈아 끼운다.
지금 코드의 `litColor = X.GetOutput().IsValid() ? X.GetOutput() : litColor` 체인이
정확히 이것이다:
```
SkyBox / SSGI / Forward+ / SSS / SSR / Fog   → Scene.LitColor 수정
Decal                                          → GBuffer.Diffuse·Normal·MetalRough 제자리 수정
Grid / WireFrame / GizmoIcon / GizmoLine       → Display.LDR 수정
```
꺼진 패스의 pass-through(입력을 그대로 흘림)와 폴백 체인은 전부 "수정 노드가
비활성이면 슬롯이 안 바뀐다"의 특수형이다. **기술화하면 폴백 코드 자체가
사라진다.**

**③ 소비(read)** — 슬롯을 읽기만 한다. live_present가 Display.LDR을 읽어 공유
텍스처로 복사(부작용 노드).

여기에 직교하는 속성이 둘:
- **활성 조건**: fog(설정+지연 초기화), wireframe(GizmoRenderer 모드), SSS·SSR(설정).
- **뷰 정책**: SSGI·Fog는 뷰당 인스턴스(시간축 상태), 나머지는 공유.

즉 필요한 것은 범용 노드 그래프가 아니라, **이 세 패턴 + 두 속성을 데이터로
적을 수 있는 최소한의 층**이다.

## 4. 설계

### 4.1 계층

```
LivePipelineDesc       (신규) 노드 목록 = 조립의 단일 출처. 수명·순서·검증·덤프.
  └ LivePassNode       (신규) 패스 하나의 어댑터: 슬롯 선언(데이터) + 타입 접착(람다)
      └ EnhancedRenderPass  (기존, 무개조) 타입 있는 Inputs/Declare
LiveBlackboard         (신규) 프레임마다 새로 채우는 이름→RGHandle 표
EnhancedRenderGraph    (기존, 무개조) 배리어·컬링·transient
```

### 4.2 자료구조 스케치

```cpp
// EnhancedLivePipelineDesc.h (신규 — RenderEngine/RHI/DX12)

/// 프레임마다 비우고 다시 채우는 이름 → 핸들 표. 값 복사가 싼 RGHandle만 담고,
/// 사이드밴드(그림자 행렬·IBL 포인터·데칼 목록)는 담지 않는다 — 4.4 참조.
class LiveBlackboard
{
public:
    void     Reset();
    void     Set(std::string_view slot, RGHandle handle);   // 발행·수정 공용
    RGHandle Get(std::string_view slot) const;              // 없으면 무효 핸들
private:
    std::unordered_map<std::string, RGHandle> m_slots;
};

struct LivePassNode
{
    std::string name;

    // ── 데이터 절반: 검증·덤프·순서의 근거 ──
    std::vector<std::string> reads;      // 발행돼 있어야 읽을 수 있다
    std::vector<std::string> writes;     // 이 노드가 처음 발행한다
    std::vector<std::string> modifies;   // 읽고 → 같은 이름으로 재발행한다
    bool perView{ false };               // 뷰당 인스턴스인가

    // ── 접착 절반: 타입은 여기 안에만 산다 ──
    // viewIndex로 뷰당 인스턴스를 고른다(공유 패스는 무시).
    std::function<EnhancedRenderPass*(uint32_t viewIndex)>  instance;

    // 비활성이면 declare를 건너뛴다. modifies 슬롯은 값이 안 바뀌므로
    // pass-through가 공짜다. 기본은 항상 활성.
    std::function<bool()>                                    active;

    // SetInputs(블랙보드에서 꺼내 타입 구조체로) → pass->Declare →
    // writes/modifies 슬롯 재발행(블랙보드에 Set). 이 람다가 지금 RenderOnce의
    // "{ Inputs i{}; i.x = ...; SetInputs(i); } Declare(); if(valid) lit=..."
    // 블록 하나와 정확히 일대일이다.
    std::function<void(LiveBlackboard&, EnhancedRenderGraph&,
                       const EnhancedFrameContext&, uint32_t viewIndex)>  declare;

    // 기본 구현: pass->PrepareFrame. 데칼(SetDecals 선행)·포그(지연 초기화)처럼
    // 프레임 준비에 접착이 필요한 노드만 채운다.
    std::function<bool(const EnhancedFrameContext&, std::string&, uint32_t)> prepare;
};

class LivePipelineDesc
{
public:
    void AddNode(LivePassNode node);

    /// 세울 때 한 번. 노드 순서대로 걸으며 발행 집합을 유지한다:
    ///   · reads/modifies가 아직 발행 안 된 슬롯을 짚으면 → 오류(노드·슬롯 이름 명시)
    ///   · active를 가진(꺼질 수 있는) 노드가 writes를 가지면 → 오류
    ///     (꺼지는 노드는 modifies만 가질 수 있다 — 꺼졌을 때 소비자가 끊기지 않는
    ///      것은 슬롯 값이 유지될 때뿐이다. 지금 배선의 폴백 규약을 규칙으로 승격)
    ///   · 같은 슬롯을 둘이 writes하면 → 오류(수정은 modifies로 적어야 한다)
    bool Validate(std::string& outError) const;

    // 수명·순서를 목록에서 유도한다. Shutdown은 자동 역순 — 접점 4가 사라진다.
    bool InitializeAll(const EnhancedFrameContext&, uint32_t viewCount, std::string& outError);
    bool PrepareAll   (const EnhancedFrameContext&, uint32_t viewIndex, std::string& outError);
    void DeclareAll   (LiveBlackboard&, EnhancedRenderGraph&,
                       const EnhancedFrameContext&, uint32_t viewIndex);
    void ShutdownAll  ();

    /// dx12.pipeline: 노드 이름 · 활성 상태 · 슬롯 연결을 사람이 읽는 형태로.
    std::string Dump() const;
};
```

### 4.3 노드 정의는 어떤 모습인가 — 지금 코드와 일대일 대조

SSR을 예로. 지금 RenderOnce의 이 블록이:

```cpp
{
    EnhancedSSRPass::Inputs ssrInputs{};
    ssrInputs.color = litColor;
    ssrInputs.depth = outputs.depth;
    ...
    p.ssr.SetInputs(ssrInputs);
}
p.ssr.SetTime(totalSeconds);
p.ssr.Declare(graph, p.frameContext);
if (p.ssr.GetOutput().IsValid()) litColor = p.ssr.GetOutput();
```

이 노드 정의가 된다:

```cpp
LivePassNode ssr;
ssr.name     = "SSR";
ssr.reads    = { "GBuffer.Depth", "GBuffer.MetalRough", "GBuffer.Normal", "GBuffer.Bitmask" };
ssr.modifies = { "Scene.LitColor" };
ssr.instance = [&p](uint32_t) { return &p.ssr; };
ssr.active   = [&p]() { return p.ssr.IsEnabled(); };
ssr.declare  = [&p, &state](LiveBlackboard& bb, EnhancedRenderGraph& graph,
                            const EnhancedFrameContext& ctx, uint32_t)
{
    EnhancedSSRPass::Inputs in{};
    in.color      = bb.Get("Scene.LitColor");
    in.depth      = bb.Get("GBuffer.Depth");
    in.metalRough = bb.Get("GBuffer.MetalRough");
    in.normal     = bb.Get("GBuffer.Normal");
    in.bitmask    = bb.Get("GBuffer.Bitmask");
    p.ssr.SetInputs(in);
    p.ssr.SetTime(state.totalSeconds);
    p.ssr.Declare(graph, ctx);
    if (p.ssr.GetOutput().IsValid()) bb.Set("Scene.LitColor", p.ssr.GetOutput());
};
desc.AddNode(std::move(ssr));
```

접착 람다는 지금 코드와 길이가 같다 — **줄어드는 것은 람다가 아니라 람다
바깥이다**: PrepareFrame 순서 자리, Shutdown 역순 두 자리, 폴백 체인, 그리고
"이 패스가 어디 끼는지"를 찾는 일. `reads`/`modifies` 메타데이터와 람다 내용의
중복은 EnhancedRenderGraph가 usages와 콜백을 나눠 받는 것과 같은 형태의 비용이고,
같은 이유로 치른다 — 선언이 있어야 검증과 덤프가 성립한다.

### 4.4 사이드밴드 데이터 — 블랙보드에 넣지 않는다

그림자 행렬(EnhancedShadowData), IBL 리소스 포인터, 데칼 Item 목록, 기즈모
아이콘처럼 핸들이 아닌 것들은 블랙보드에 담지 않고 **접착 람다가 패스 포인터를
직접 잡아 지금처럼 Set한다** (`p.forward.SetShadow(p.shadow.GetShadowMap(), ...)`).

이유: 이것들을 일반화하려면 `std::any`나 타입 소거가 필요한데, 얻는 것은
"덤프에 한 줄 더"뿐이고 잃는 것은 컴파일 타임 타입 검사다. 핸들 흐름과 달리
사이드밴드는 순서 오류가 컴파일러(포인터 타입)로 잡히므로 기술화의 동기인
'조용한 순서 실패'가 애초에 없다. YAGNI.

### 4.5 뷰 정책

`perView` 노드는 InitializeAll이 뷰 수만큼 Initialize를 돌리고(지금 SSGI 루프와
동일), DeclareAll/PrepareAll이 viewIndex를 람다에 넘긴다. 인스턴스 저장소는
지금처럼 `CameraView` 안에 두고 `instance` 람다가 `&p.views[i].ssgi`를 돌려준다 —
저장 위치는 바꾸지 않고 선택만 노드로 옮긴다.

포그의 지연 초기화(fogReady, 뷰당 42MB)는 그 노드의 `prepare` 람다에 그대로
들어간다. 기술화가 이 정책을 바꾸지 않는다.

### 4.6 검증 규칙이 잡는 실수들

| 실수 | 지금 | 기술화 후 |
|------|------|-------------|
| 발행 전 슬롯을 읽는 순서 실수 | 무효 핸들이 폴백에 삼켜져 그림만 이상 | Validate가 노드·슬롯 이름으로 즉시 오류 |
| 꺼질 수 있는 패스가 새 슬롯을 발행 | 꺼진 프레임에 소비자가 무효 핸들 | Validate 오류 (modifies로 강제) |
| Shutdown 순서 어긋남 | 가끔 죽음 | 역순 자동 |
| PrepareFrame 순서 어긋남 | 한 프레임 늦은 데이터 | 목록 순서 자동 |
| "이 패스 어디 끼지" | RenderOnce 정독 | dx12.pipeline 덤프 |

## 4.7 진행 상황 (2026-08-07)

| 슬라이스 | 상태 | 결과 |
|---|---|---|
| 1. Declare 구간 치환 | **완료** | RenderOnce의 배선 약 200줄 → 노드 17개 + `DeclareAll` 한 줄 |
| 2. 수명·순서 유도 | **완료** | 접점 3·4·6 제거. 노드 밖 수명 호출 4건만 남음 |
| 3. 뷰 정책 노드화 | 부분 | `perView` + `instance(viewIndex)`는 슬라이스 2에서 들어감. 잔여 정리 필요 |
| 4. `dx12.pipeline` 덤프 | 미착수 | `Dump()`는 구현됨, 콘솔 노출만 남음 |
| 5. 데이터 파일·에디터 UI | 보류 | 소비자가 생길 때 |

**슬라이스 1에서 실제로 사라진 것** — 설계가 예상한 그대로였다:

- `litColor = X.GetOutput().IsValid() ? X : 이전값` 폴백이 여섯 번 반복되던
  자리가 `Scene.LitColor` 슬롯 하나로 대체됐다.
- 와이어프레임의 삼항 분기(`wireFrameEnabled ? wireframe : grid`)도 사라졌다 —
  꺼지면 슬롯 값이 남아 아이콘이 그리드 결과를 이어받는다.

**설계에서 다듬은 것**: `declare` 콜백의 마지막 인자를 `viewIndex`(uint32)가
아니라 `LiveFrameBinding` 구조로 바꿨다. `live_present`의 대상(표시 슬롯의
공유 텍스처)이 프레임마다 회전하는데, 노드가 그것을 캡처하면 노드 정의가
프레임에 묶여 데이터화의 의미가 없어진다.

**슬라이스 2에서 옮긴 특수 처리**: 데칼의 `SetDecals` 선행은 그 노드의
`prepare`로, 포그의 지연 초기화(뷰당 42MB)는 빈 `initialize` + `prepare`로.
예전에는 그 둘이 RenderOnce의 순서 규칙으로만 존재했고, "노드를 옮기면 이
규칙도 같이 옮겨야 한다"가 코드 어디에도 적혀 있지 않았다.

`BuildPipelineDesc`를 패스 초기화 **앞**으로 옮겼다 — 노드가 패스를 참조로만
잡으므로 성립하고, 그래야 초기화 순서까지 목록이 정한다.

노드 밖에 남은 수명 호출 넷은 전부 정당하다: `ibl`(패스가 아니라 생성기),
`profiler` · `resources`(패스 아님).

## 5. 이행 계획 — 슬라이스와 검증

원칙은 기존 이식과 같다: 슬라이스마다 A/B 가능해야 하고, 판정은 그림이 아니라
수(패스 목록·픽셀)다.

**슬라이스 1 — 층 도입 + Declare 구간 치환.**
`EnhancedLivePipelineDesc.h/.cpp` 신설. RenderOnce의 Declare 구간(shadow →
live_present)을 노드 목록 구축(파이프라인 생성 시 1회) + `DeclareAll`(프레임마다)
로 치환. Initialize/Prepare/Shutdown은 아직 손대지 않는다.
- 검증 ①: `dx12.live status`의 패스 이름 목록이 치환 전과 **문자 그대로 동일**
  (기본 · SSS·SSR 켬 · 포그 켬 세 조건).
- 검증 ②: 같은 씬 픽셀 대조 — 치환 전 리비전과 후 리비전을 같은 씬·같은
  카메라로 돌려 공유 텍스처 리드백을 비교한다. 기술화는 순수 리팩토링이므로
  한 픽셀도 달라질 이유가 없다.
- 검증 ③: Validate에 일부러 순서를 뒤집은 노드를 넣어 오류 문구가 노드·슬롯을
  짚는지 확인(자가 검증 `dx12.pipelinetest` — 검증기의 검증).

**슬라이스 2 — 수명·순서 유도.**
Initialize/PrepareFrame/Shutdown 호출을 노드 목록 유도로 교체. 접점 3·4·6이
사라진다. 검증: 슬라이스 1과 같은 A/B + off→on 재활성화(재초기화 경로 —
LivePipeline 재구축이 노드 재구축과 함께 도는지).

**슬라이스 3 — 뷰 정책 노드화.**
SSGI·Fog의 perView 처리를 노드로. 검증: 씬뷰+게임뷰 동시 렌더에서 잔상 회귀
없음(멀티카메라 잔상 조사의 세 조건 대조 재사용).

**슬라이스 4 — `dx12.pipeline` 명령.**
Dump를 콘솔에 노출하고 status와 연결. 검증: 덤프의 활성 상태가 환경변수·창
조작을 따라 바뀌는지.

**슬라이스 5(후속, 별도 결정) — 데이터 파일·에디터 UI.**
노드 목록이 데이터가 된 뒤에만 의미 있는 단계. 소비자가 생길 때 진행.

## 6. 위험과 그 처리

- **std::function 간접 비용**: 노드 ~20개 × 프레임당 람다 호출 몇 번. 기록
  비용(ms 단위)에 견줘 ns 단위 — 무시. 근거가 필요하면 슬라이스 1 검증에서
  치환 전후 CPU ms를 함께 적는다.
- **문자열 슬롯 오타**: Validate가 세울 때 잡는다(발행 안 된 슬롯 읽기 = 오류).
  자주 쓰는 이름은 `namespace LiveSlots { constexpr const char* kLitColor = ...; }`
  상수로 두어 오타 자체를 줄인다.
- **람다가 잡는 참조 수명**: 노드는 LivePipeline이 소유하고 람다는 `p`(자기
  소유자)와 LiveState를 잡는다 — LivePipeline 재구축 시 노드도 함께 부수고
  다시 세우므로 대롱거리는 참조가 없다. 파이프라인 생성 순서에 노드 구축을
  포함시키는 것이 계약.
- **자가 검증과의 이중화**: dx12.sss 같은 검증은 패스를 직접 부르므로 이 층과
  무관하게 계속 돈다. 층 자체의 검증(dx12.pipelinetest)을 따로 둔다.

## 7. 끝 그림 (슬라이스 4 완료 시)

패스 하나를 라이브에 잇는 일:

1. `LivePipeline`에 패스 멤버 추가 (기존과 동일)
2. 노드 정의 하나를 목록의 원하는 자리에 추가 (이것이 배선의 전부)
3. (조정 파라미터가 있으면) Tuning 미러·창 — 기존과 동일

접점 10종 → 3종. 순서를 사람이 지키는 곳 4 → 1(노드 목록의 자리 하나).
틀리면 조용히 이상해지던 실수 셋이 구축 시점 오류가 된다.
