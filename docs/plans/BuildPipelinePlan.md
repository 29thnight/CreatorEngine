# 빌드 파이프라인 신설과 엔진 계층 전면 개편 (BuildPipelinePlan)

작성: 2026-08-09 · 계기: "언리얼·유니티는 게임을 어떻게 빌드하나"라는 질문에
답하다가 드러난 구멍들. 답을 적고 보니 우리에게 없는 것이 단계 하나(쿡)가
아니라 심판 전체였다.
역할 분리: 빌드·쿡·패키징은 이 문서가 맡고, Editor/Core 소유권·실행 순서와
완료 판정은 `EngineLayerSeparationPlan.md`의 E0~E7이 정본이다.
실측: 2026-08-09, 5방향 병렬 조사(빌드 구성 · 경계 매크로 · 에셋 흐름 ·
기존 계획 정합 · 플레이어와 C#). 아래 수치는 전부 그 조사에서 직접 센 값이다.
검증: 같은 날 적대 검증 3방향(사실 대조 · 문서 정합 · 홍팀)을 통과시키며
지적 12건을 반영했다 — 가장 큰 정정은 B0의 규모(§3)다.
개정(2026-08-10, 사용자 결정): 게임 빌드를 언리얼식(엔진 재컴파일)에서
**유니티식(선빌드 플레이어 + 복사)**으로 전환 — TrainAsis·GameBuild.sln 제거,
Player 프로젝트 신설. 근거와 대가는 §2.0, 개정된 곳은 §1.6·§2.2·§2.3·§3.

> §0~§1의 현재형 문장은 2026-08-09 당시 결함을 보존한 baseline 기록이다.
> 현재 상태와 다음 실행 순서는 §2.3, §3의 B2~B5 및
> `EngineLayerSeparationPlan.md` E0~E7에서만 판정한다.

---

## 0. 논지 — 심판이 없으면 개편도 없다

계층 분리의 판정을 지금은 "컴파일이 되는가"로 한다. 그 판정은 약하다 —
BUILD_FLAG가 창 하나를 꺼도, DYNAMICCPP_EXPORTS 가드가 전부 죽어 있어도,
컴파일은 된다. 강한 판정은 **"에디터 없이 패키지한 게임이 도는가"** 다.

그런데 실측 결과, 그 판정을 내릴 심판 자체가 없다:

- **플레이어는 지금 컴파일조차 되지 않는다.** GameMain이 여섯 곳에서 쓰는
  `SceneRenderer` 타입이 DX11 구 렌더러 은퇴(ccca6964, 2026-08-07)에서
  삭제됐다 — 그 커밋은 "이미 dead code"라 적었지만 유일한 잔존 소비자가
  GameMain이었다. 링크 흔적도 없다: 이 머신의 로컬 산출물 기준
  `x64/GameBuild/`에 라이브러리 6종은 있는데 TrainAsis.exe가 없다.
- **플레이어는 셰이더를 로드하지 않는다.** `ShaderSystem->Initialize()` 호출이
  저장소 전체에서 에디터 메인(`EngineEntry/Dx11Main.cpp:63`) 한 곳뿐이고,
  그 파일은 에디터 exe에서만 컴파일된다.
- **플레이어는 C#을 켜지 않는다.** `GameMain.cpp`에 ClrHost 참조 0건 —
  스크립트 계층이 통째로 기동하지 않는다.
- **플레이어는 DX12를 켜지 않는다.** `EnhancedSceneRenderer::InitializeRuntime`이
  에디터 메인에만 있다. 플레이어는 DX12 이관 이전의 부팅 순서로 얼어 있다.
- **C# 어셈블리는 게임 빌드에서 생성되지 않는다.** `dotnet build` 호출은
  저장소에 있지만 전부 에디터 프로젝트(Academy_4Q.vcxproj)의 PreBuildEvent
  4곳이다 — 에디터를 빌드할 때만 ScriptCore·GameScripts가 생성된다. 게임
  경로(TrainAsis · GameBuild.sln · CI)에는 0건이고, `x64/GameBuild/Managed`가
  없다.

"게임 빌드" 버튼은 있고 MSBuild도 돌고 pak도 만들어지지만, 그 산출물은 실행
가능한 게임이 아니다. `GameBuilderSystem.cpp:26`의 주석("게임 스크립트는 C#
어셈블리로 배포된다")과 실제 파이프라인이 어긋난 채로 있다.

그래서 순서가 정해진다. **심판(파이프라인)을 먼저 세우고, 계층 개편의 모든
슬라이스를 그 심판 아래서 진행한다.** 파이프라인의 각 단계가 자기 층만 보게
만들면, 빌드가 통과한다는 사실이 곧 경계가 서 있다는 증명이 된다.

---

## 1. 실측 — 다섯 방향

### 1.1 빌드 기술이 아홉 벌

빌드 구성이 vcxproj 9개(라이브러리 7 + Academy_4Q + TrainAsis)에 각각 복제돼
있다. 태그 출현 수로 세면: PlatformToolset 52회 · CharacterSet 52회 ·
UseDebugLibraries 52회 · WholeProgramOptimization 34회 · EnableUnitySupport
23회. GameBuild|x64 블록을 7개 라이브러리에서 나란히 diff하면
**ConfigurationType 한 줄(Static/Dynamic)을 빼고 나머지가 바이트 단위로
동일하다.** OutDir 재정의도 7개 파일에 같은 2줄씩 반복되는데, 그중
GameBuild 쪽 줄은 MSBuild 기본 공식과 값이 같은 no-op이다.

공용화 시도가 없었던 것이 아니다 — `EngineOutput.props`가 그 목적으로
존재한다. 그런데 **자동 임포트가 아니라 opt-in이고, 임포트한 프로젝트가
9개 중 2개뿐이며(Academy_4Q · ScriptBinder), 핵심 프로퍼티 `EngineOutDir`을
참조하는 vcxproj가 0개다**(csproj 두 개만 참조한다 — ScriptCore.csproj:19 ·
GameScripts.csproj:13). 배선되지 않은 채 남은 절반의 시도다. 반면
`Directory.Build.props/targets`는 자동 임포트라 진짜 전역인데, 지금 담긴
것은 ASan 스위치와 버전 헤더 생성뿐이다 — **그릇은 이미 있고, 내용이 아홉
벌로 흩어져 있을 뿐이다.**

부수: TrainAsis만 PlatformToolset이 v143이다(링크하는 라이브러리 전부는
v145). CreatorEngine.sln에는 GameBuild 구성이 아예 없고, 두 sln의
SolutionGuid가 동일하다(복제 생성의 흔적).

### 1.2 매크로 경계는 창을 끄지, 의존을 끊지 않는다

**BUILD_FLAG** — 여는 지시문 48곳(17개 파일): `#ifndef` 43(에디터 전용 분기)
+ `#ifdef` 5(게임 전용 분기). 최대 집중지는 `RenderEngine/DataSystem.cpp`
19곳(콘텐츠 브라우저 구현 본체가 엔진 라이브러리 안에 있다). 정의 방식이
특이하다: 에디터 exe(Academy_4Q)는 이 매크로를 어느 구성에서도 정의하지
않고, 라이브러리 7종 + TrainAsis(8개 프로젝트)의 GameBuild|x64 구성만
정의한다 — 즉 "라이브러리를 게임용으로 다시 컴파일"하는 절단이다.

그런데 이 절단이 막지 못하는 경로가 셋 있다:

1. **전이 include.** `Utility_Framework/ReflectionFunction.h:12`가 가드 없이
   `<imgui.h>`를 include하고(파일 안에서 ImGui 심볼 실사용 0건 — 순수 전이
   통로), 이 헤더가 ReflectionMecro.h → Reflection.hpp → **Core.Minimal.h**
   경로로 엔진의 최소 공통 헤더에 묶여 있다. Core.Minimal.h를 직접 include하는
   파일만 90곳 이상(계수 기준에 따라 89~102) — 사실상 엔진 전체가 imgui.h를
   본다. BUILD_FLAG는 이 경로에 없다.
2. **플레이어 자신.** `GameMain.cpp`는 BUILD_FLAG 가드 0건으로 imgui 3종
   헤더를 include하고 매 프레임 `ImGuiRenderer::BeginRender/Render/EndRender`를
   실행한다. BUILD_FLAG가 끄는 것은 그 안의 특정 창(도킹스페이스·에디터
   트리거)뿐이다.
3. **게임플레이 컴포넌트의 에디터 자료 하드 의존.** BehaviorTreeComponent.h ·
   AnimationController.h · BTBuildNode.h · BTEnum.h 네 헤더가
   `imgui-node-editor`를 무가드로 include한다 — AI·애니메이션 런타임 자료가
   노드 그래프 UI 라이브러리와 컴파일 타임에 직결. `Camera.cpp`의
   HandleMovement도 이동 키 6개(W·S·A·D·Q·E) 전부에서 `ImGui::IsKeyDown`을
   무가드 폴백으로 조회한다.

**DYNAMICCPP_EXPORTS** — 267건/184개 파일, 살아 있는 `#ifndef` 187곳.
원래 목적은 C++ 게임 스크립트 DLL이 엔진 헤더를 슬림하게 보게 하는 가드였다
(Material.h:4~6 주석이 직접 설명). C++ 핫리로드 은퇴(PHASE 9-4) 후 이
매크로를 정의하는 빌드가 하나도 없다(vcxproj·props 전수 grep 0건, Dynamic_CPP
폴더에 vcxproj 부재). **187개 블록 전부가 항상 참으로 평가되는 통과
래퍼다** — 기능 무해, 그러나 모든 헤더 첫 줄에서 "이 가드는 왜 있지"를
묻게 만드는 순수 부채.

### 1.3 에셋 흐름 — B2 model Cook/pak은 닫혔고 셰이더 쿡은 아직 비어 있다

2026-08-24 현재 셰이더 컴파일 정본은 옛 `ShaderSystem/HLSLCompiler/.cso`나
직접 DXC 경로가 아니라 `RHIShaderCompiler`와 고정 Slang 2026.14다. 요청의
정체성은 source, entry point, target profile, 정규화 permutation key/entries,
DXIL/SPIR-V, strict-math
옵션과 Slang/DXC/DXIL 세 DLL의 콘텐츠 hash로 구성된다. Runtime Host는 Stage의
`slang-compiler.dll`·`dxcompiler.dll`·`dxil.dll`, Editor authoring Host는 저장소의
`ThirdParty/Slang`만 사용한다. 둘 다 Vulkan SDK·PATH·registry compiler fallback을
허용하지 않는다. DXC는 Slang의 DXIL downstream 구현으로만 번들 안에 남으며
엔진이 직접 적재하거나 선택하지 않는다.

B2의 Cook 단계는 더 이상 전체 no-op이 아니다. Serialization D5-b2b2가 package-base
snapshot의 model 전수를 별도 `AssetCooker`로 CEMC/CEMF에 cook하고 `Assets/Derived`를
`AssetPacker` 입력에 overlay한다. 다만 **shader precompile**은 의도적으로 no-op이다. 현재 패키지는 HLSL source와
`slang-compiler.dll`·`dxcompiler.dll`·`dxil.dll`을 싣고 Player가 Slang으로
런타임 컴파일한다. 따라서
"cso를 복사하면 된다"는 과거 계획은 폐기한다. 현 캐시는 compiler 세 DLL의
콘텐츠 identity와 요청·실제 include closure를 키로 쓰지만 authoring용 가변
디스크 캐시이지 배포 artifact manifest가 아니다. Editor cache 파일을 Stage에
복사하는 것도 B3 해법이 아니다. M4의 `.shadermeta` schema v1 위에 Material M2B가
`meta GUID + pass + 다중값 keyword 선택`의 canonical key/ordinal define,
pass-major 전수 열거와 기본 4,096-request fail-closed 상한을 구현했다. Editor load는
 조합 수를 기록하되 이 Build 상한 때문에 실패하지 않는다. B3는 별도 열거기를 만들지
 않고 이 결과를 source/entry/target/include graph와 결합해 실제 compile request와
 artifact manifest identity로 고정해야 한다. Material M7의 중립 reflection은 같은
 request에서 DXIL/SPIR-V의 논리 register/space와 HLSL cbuffer offset이 같은지 검증하고,
 `.shadermeta` property를 소유 binding layout으로 해석한다. B3 manifest는 이 layout
 identity도 기록해 cook 산출물과 런타임 material upload 계약이 갈라지지 않게 해야 한다.
 **열거·reflection 계약은 완료됐지만 실제 compile,
 DXIL/SPIR-V artifact 게시, source-free Stage 소비는 아직 없다.** 따라서 M2B 완료를
전수 shader cook 완료로 판정하지 않는다.

pak 입력은 live `EngineSettings.asset`을 사용하지 않는다. `Project`, `Workspace`,
`Tracked(HEAD git archive)` 모드의 asset/settings 입력 위에 별도 runtime settings
template를 materialize한다. 시작 씬/backend 인자가 있으면 그 값을 쓰고, backend 인자를
생략하면 template의 build 선택을 runtime key로 투영한다. live YAML의
`build.render.backend`는 Editor `BuildSettings`의 저작 값일 뿐 Player 소비 key가 아니다.
`GameBuilderSystem`이 이 값을 인자로 넘기면 패키징이 pak용 `render.backend`에 투영하고,
Player의 Core `RuntimeSettings`는 그 effective runtime key만 읽는다. 현재 template가
`build.render.backend`도 함께 맞추는 것은 preflight 호환을 위한 과도기 중복이며 preflight는
둘의 일치를 강제한다. manifest도 실제 runtime backend를 기록한다. Player는 pak을
`%TEMP%\CreatorEngine\Player\<PID>\RuntimeContent`에 풀고 로그·dump는 sibling
`RuntimeData`에 쓴다. 현재 package boot/smoke가 지나는 authoring write hotspot은
capability로 차단된다. 자동 게이트는 AssetPacker의 overlap/reparse/extended·device·8.3
alias, Player runtime reparse와 exact current-PID cleanup 범위를 판정한다. Pak extractor의
lexical traversal·대소문자 중복·reparse 거부는 구현돼 있지만 forged-pak suite, DOS device
component, 전체 entry 선검증과 부분 추출 rollback은 아직 별도 완료 조건이다.

### 1.4 플레이어는 3세대 전 에디터의 사본이다

`GameMain.cpp`(319줄)와 `Dx11Main.cpp`(609줄)는 복붙 클론 관계다 — 정렬
비교로 GameMain의 243/319줄(약 76%)이 Dx11Main에 그대로 존재하고, 파일
스코프 전역 원자 플래그 3종(`isGameToRender` 등)까지 같은 이름으로 각자
선언한다. 갈라진 뒤 에디터 메인만 진화했다: DX12 InitializeRuntime ·
ClrHost 기동 · 씬 생성 델리게이트가 에디터 쪽에만 있다. **클론은 동기화를
사람이 해야 하는 구조이고, 사람은 안 했다** — 그 결과가 §0의 목록이다.

그리고 클론은 이미 죽었다 — 위 §0의 `SceneRenderer` 삭제로, TrainAsis는
전 구성에서 GameMain.cpp를 컴파일하므로 **B0는 "빠진 호출 이식"이 아니라
렌더 경로 재작성을 포함한다**(§3에서 상술).

vcxproj도 같은 상태다. TrainAsis의 Debug|x64 · Release|x64 구성은 정의는
있으나 성립하지 않는다: SubSystem=Console인데 엔트리는 wWinMain(WINAPI)이고,
D3D11·FMOD AdditionalDependencies가 GameBuild|x64에만 있다. **디버거를 붙일
수 있는 플레이어 빌드가 지금 하나도 없다.**

### 1.5 CI는 게임을 모른다

build.yml은 CreatorEngine.sln에서 라이브러리 7종만 Debug/Release로 빌드한다.
`GameBuild.sln` 문자열은 CI 전체에서 0회. 실행 파일 2종(Academy_4Q ·
TrainAsis)은 FMOD .lib이 저장소에 없어(gitignore `*.lib`) 링크 불가로 명시
제외돼 있고, 이 사유는 실측으로 사실과 일치함을 확인했다. 즉 **"빌드가
그린"이라는 CI 신호는 라이브러리 컴파일까지만 말하고, 링크와 패키지에 대해
아무것도 말하지 않는다.**

### 1.6 참고 — 두 상용 엔진의 답과 우리가 취할 것

| 축 | 언리얼 | 유니티 | 우리가 취할 것 |
|---|---|---|---|
| 빌드 진실의 위치 | `.Target.cs`/`.Build.cs` (코드 한 벌) | 에디터 설정 (데이터) | **한 벌**이라는 성질만 — 도구는 props + 스크립트로 충분 |
| 에디터/게임 분기 | Target 축 (`WITH_EDITOR`) | 별개 바이너리 | Target × Config 2축 (§2.2) |
| 에셋 변환 | Cook (명시 단계) | 임포트 캐시 → 직렬화 | **쿡을 명시 단계로** — 지금은 에디터 기동의 부산물 |
| 엔진 재컴파일 | 한다 | 안 한다 | **선빌드 Player가 목표** — provenance 전까지 오케스트레이터가 native build 수행 (§2.0) |
| 오케스트레이션 | UAT BuildCookRun | BuildPipeline API | 스크립트 하나를 에디터·CI가 공유 |

초안은 여기서 "유니티 모델은 엔진 바이너리 배포가 전제라 우리와 맞지 않는다"
고 적었다. 하루 만에 뒤집혔다(§2.0) — 그 전제는 "엔진 소스를 우리가 갖고
있는가"의 문제가 아니라 **"게임 코드에 네이티브가 있는가"**의 문제였고,
후자는 9-4에서 이미 부정됐다. UBT식 자체 빌드 도구 비채택은 유지. 취하는
것: 빌드 진실 한 벌 · 쿡의 단계화 · **유니티식 선빌드 플레이어**.

---

## 2. 목표

### 2.0 결정 — 게임 빌드는 유니티식이다 (2026-08-10 개정)

초안은 언리얼 모델(게임 빌드가 엔진을 GameBuild 구성으로 재컴파일)을 택했다.
사용자 제안으로 뒤집는다: **플레이어를 미리 빌드해 두고, 게임 빌드는
복사다.**

성립 근거 — 유니티 모델의 전제는 "게임 코드에 네이티브가 없다"인데, 그것이
PHASE 9-4(C++ 핫리로드 은퇴)에서 이미 참이 됐다. 게임 코드는
C#(GameScripts.dll)뿐이고 ClrHost가 실행 시 로드한다 — 게임마다 네이티브를
다시 컴파일할 이유가 없다. 초안이 언리얼 모델을 고른 근거("엔진 소스가 곧
제품")는 엔진 *개발* 빌드에는 여전히 참이지만, 게임 *패키징*과는 무관했다.

무엇이 좋아지는가:

- **목표 상태의 게임 빌드는 복사·쿡·pak이다.** 다만 Core ABI/version provenance가
  아직 없으므로 현재 제품 경로는 stale Player 배포를 막기 위해 오케스트레이터에
  `-BuildNative`를 맡긴다. 선빌드 Player의 정체성을 검증할 수 있게 된 뒤 이 옵션을
  제거해야 비로소 분 단위 native build가 사라진다.
- **에디터의 MSBuild 명령 조립은 은퇴한다.** `GameBuilderSystem`은 vswhere나
  MSBuild 경로를 알지 않고 `build.ps1`만 호출한다. 과도기 native build의 도구 선택과
  target 구성은 오케스트레이터 한 곳이 소유하며, 최종 상태에서는 그 단계도 생략한다.
- **TrainAsis·GameBuild.sln이 소멸한다.** 컴파일 불능 클론(§1.4)을
  소생시키는 대신 Player 프로젝트를 CreatorEngine.sln 안에 새로 세운다 —
  에디터와 함께 항상 빌드되고, 항상 그린이고, CI가 본다. sln 두 벌
  유지비(§1.1의 SolutionGuid 복제 같은 부류)도 사라진다.
- **구성 매트릭스가 준다.** GameBuild 재해석도 GameDebug 신설도 필요
  없다(§2.2 개정).

무엇을 치르는가(정직하게):

- **BUILD_FLAG의 행동 스위치를 런타임 모드로 바꿔야 한다.** 48곳 중
  ~17곳은 코드 절단이 아니라 행동 분기다 — PathFinder의 %TEMP% 경로 ·
  옛 EngineSetting의 pak 언팩(현재는 Player Host로 이동) · SceneManager의 자동 시작 · CoreWindow의
  보더리스 전체화면 · TagManager의 읽기 전용 · GameApp/EngineBootstrap 로그
  태그. 단일 라이브러리 바이너리 세계에서 이것들은 **진입점이 정하는 런타임
  모드**(`EngineMode: Editor | Player`)가 돼야 한다. 컴파일 타임 보증이
  런타임 보증으로 약해지는 지점이므로, 모드는 부팅 첫 줄에서 한 번 정해지고
  이후 불변 + 초기화 assert.
- **과도기 플레이어는 뚱뚱하다.** 나머지 ~31곳(#ifndef의 에디터 전용 코드 —
  프로파일러 · 콘텐츠 브라우저 · 셰이더 타임스탬프 등)은 Release 라이브러리에
  컴파일돼 들어간 채 플레이어에 실린다. 호출자가 에디터 창뿐이라 동작은
  무해하고(스모크로 확인), 무게는 E2·E4·E6의 물리 분리가 걷는다. 즉 BUILD_FLAG는
  개명도 재배치도 아니라 **두 갈래로 소멸한다: 행동 → 런타임 모드(B0),
  코드 → 물리 분리(E2·E4·E6).**
- **게임별 네이티브 최적화의 자유를 잃는다** — 게임 코드가 C#뿐이라 지금
  실익이 없다. 미래에 게임별 네이티브 플러그인 개념이 생기면 이 결정을
  재검토한다(명시적인 native plugin 계약이 생기기 전까지는 성립하지 않는다).

### 2.1 계층 — 다섯 층

| 층 | 소속 (목표) | 지금과의 차이 |
|---|---|---|
| **Runtime** | Utility_Framework · RenderEngine · Physics · ScriptBinder · ScriptCore(관리) | 에디터 코드가 물리적으로 나간다 — BUILD_FLAG로 가리는 게 아니라 |
| **Editor** | EngineGUIWindow · ImGuiHelper · EngineEntry 대부분 · Runtime에서 적출되는 것들(DataSystem 에디터 UI · 노드에디터 자료 등) | 지금은 절반이 Runtime 라이브러리 안에 산다 |
| **Player** | CreatorEngine.sln 소속 `Player` 프로젝트 — Editor와 같은 Runtime Core를 링크, 공용 frame/bootstrap 경계는 E3·E6에서 완성 | TrainAsis(컴파일 불능 76% 클론)는 제거 |
| **Project** | Host가 주입하는 project root + Assets + ProjectSetting + Scripts | Dynamic_CPP 폴백과 경로 하드코딩을 E1·E6에서 제거 |
| **Tools** | HeaderTool · AutoRegisterCreateReflection · AssetPacker · `build.ps1` | 실행 파일과 분리된 빌드/패키지 도구 |

### 2.2 빌드 축 — 구성 2개와 런타임 모드 (2026-08-10 개정)

초안은 Target × Config 4구성(GameBuild 재해석 + GameDebug 신설)이었다.
유니티식 전환(§2.0)으로 축 하나가 통째로 사라진다:

- **구성은 Debug · Release 둘뿐이다.** GameBuild 구성은 제거한다 —
  라이브러리를 게임용으로 다시 컴파일하지 않으므로 존재 이유가 없다.
  초안이 GameDebug 신설로 풀려던 결핍("디버깅 가능한 플레이어가 하나도
  없다", §1.4)은 구성 신설이 아니라 **Player 프로젝트가 Debug 라이브러리를
  링크하는 것**으로 자연히 얻는다.
- **에디터/플레이어 분기는 구성이 아니라 실행 파일이다.** 같은 라이브러리
  바이너리를 두 exe(Academy_4Q · Player)가 링크하고, 행동 차이는 진입점이
  정하는 런타임 모드(§2.0)가 낸다.
- 남는 공통 속성(PlatformToolset · LanguageStandard · WarningLevel 등)의
  `Directory.Build.props` 승격은 그대로 간다 — 복제가 아홉 벌인 사실은
  모델과 무관한 부채다(§1.1).
- x86(Win32) 구성 열도 함께 제거한다 — 어느 실행 파일도 Win32로 성립하지
  않는 죽은 열이다. 제거 전 소비자 부재만 재확인한다.
- BUILD_FLAG 정의는 props로 옮기는 것이 아니라 **소멸한다** — §2.0의 두
  갈래(행동 → 런타임 모드, 코드 → 물리 분리).

### 2.3 파이프라인 — 여섯 단계와 한 오케스트레이터

```
Tools/build.ps1 -Config Release -InputMode <Project|Workspace|Tracked>
                [-Project <경로>] [-BuildNative]
                [-StartupScene <name.creator>] [-RenderBackend <dx12|vulkan>]
  1 BuildNative    선택적으로 Player·AssetPacker·AssetCooker를 현재 solution에서 빌드
  2 BuildManaged   dotnet build ScriptCore → GameScripts
  3 Cook           package-base model→CEMC/CEMF; shader는 B3 전까지 source를 유지
  4 Stage          미게시 candidate에 Player.exe·명시 DLL·Managed 배치
  5 Pak            정본 입력 + runtime settings overlay → GameAssets.pak/manifest
  6 Verify         격리 TEMP에서 Player --smoke N, unpack hash와 runtime hash 검증

성공              candidate → immutable release, current JSON pointer 원자 교체
promotion 전 실패 candidate를 남기고 기존 release/current를 보존
pointer 교체 실패 기존 current를 보존하나 unpublished immutable release가 남을 수 있음
SkipVerify         검증·publish 없이 candidate만 진단용으로 남김
```

- Verify는 종료 코드뿐 아니라 startup scene load, ScriptCore 초기화/타입 등록,
  GT frame, display frame/promotion, failure marker, unpack entry 수·SHA-256을 본다.
  `FT_Primitives.creator`일 때는 scene-resident `PackageSmokeProbe`의
  `OnInitialized → OnBeginSimulation`을 각각 정확히 한 번 요구한다.
- `GameBuilderSystem::BuildGame()`과 `game.pak`은 같은 script를 `Release/Project`
  모드로 호출하며 Build Settings의 시작 씬/backend를 전달한다. 현재는 stale native
  배포를 막기 위해 제품 경로도 `-BuildNative`를 쓴다. GameBuilder가 MSBuild 경로나
  명령을 직접 조립하지 않는 것이 현재의 단일화 계약이다. build 직전에 live 설정 전체를
  다시 저장하지도 않는다. UI 변경 시 `EditorSettingsStore`가 live YAML을 원자 저장하고,
  build 경로는 메모리의 `BuildSettings` 값만 명시적 CLI 인자로 넘긴다.
- Editor 호출은 아직 UI thread에서 동기 대기한다. timeout/cancel/progress 중계는
  B2 운영성 잔여이며, 기능적 산출물 정본과 분리해 후속 처리한다.
- Release 산출물은 현재 .NET 10 x64 runtime과 Microsoft Visual C++ Redistributable
  x64를 외부 prerequisite로 둔다. Debug package는 debug CRT를 요구하므로 개발용이다.
- `contentDigest`는 pak logical entry/hash, `runtimeDigest`는 실행 payload,
  `distributionDigest`는 둘을 합친 논리 배포물 정체성이다. Pak 파일 자체는 포맷의
  random salt 때문에 빌드마다 SHA가 달라도 세 논리 digest가 같으면 재현된 입력이다.

### 2.4 산성 테스트 — 모든 슬라이스의 심판

> **에디터를 한 번도 띄우지 않은 체크아웃에서 `build.ps1 -Target Game`이
> 패키지를 내고, 그 패키지가 첫 씬을 그리고 C#
> `OnInitialized → OnBeginSimulation` marker를 남기고 정상
> 종료한다.**

- v1(트랙 B의 기존 판정): 개발 머신 기준 — FMOD 바이너리가 이미 있는 환경.
  "에디터 미기동"이 핵심이었다(쿡이 에디터 부산물이 아님의 증명).
- v2(B5의 판정): CI 기준 — 클린 머신. `AudioBackendModernizationPlan.md` PHASE 22 AU8/AU9의
  FMOD 은퇴·miniaudio source 통합·package smoke가 선행한다.

E0~E7의 모든 소유권 이동 슬라이스는 커밋 전에 이 테스트를 다시 통과해야 한다.
**계층 이동의 판정이 "컴파일된다"에서 "패키지가 돈다"로 올라간다** — 그것이
이 계획 전체의 요점이다.

2026-08-21 최신 snapshot에서 Workspace Release/BuildNative 170 entries와 Editor 제품 진입
`--exec game.pak --exec quit`의 Project/BuildNative 470 entries가 각각 통과했다. 제품 실행은
exit 0, managed type 25종, lifecycle marker 2개, display/promotion 2회, smoke exit 0,
`verification=passed`를 남겼다. Host runtime-root injection과 fail-closed Log abort까지
포함해 게시한 제품 release `Dynamic_CPP-2a778eb37a4f4a70a33abcbdf609aa84`도 같은 판정을
유지했다. 정상 Player는
WM_CLOSE 후 실제 PID root 생성/삭제, 숫자형 sibling PID root와 parent snapshot,
Stage/Pak/Player 불변을 통과했다.
다만 canonical scene/template/probe가 아직 HEAD에 없고 FMOD import lib와 runtime DLL
공급도 저장소 밖이므로 **clean-checkout/CI 산성 테스트는 미완료**다. 공급 방식은 더 이상
미결정이 아니며 PHASE 22에서 FMOD를 제거하고 source-integrated miniaudio로 닫는다. `Tracked` 모드는
이 파일을 working tree에서 보충하지 않고 명확히 실패한다.

---

## 3. 실행 — 슬라이스

### 트랙 B — 심판 세우기 (선행)

**B0 — 플레이어 재건 (TrainAsis · GameBuild.sln 제거).** 가장 먼저이고
트랙 B 최대 슬라이스. ★ 개정(2026-08-10): 소생이 아니라 **교체**다 —
컴파일 불능 클론(§1.4)을 되살리는 대신 새로 세운다. 죽은 코드를 산
패턴으로 고치는 것보다 산 패턴으로 새로 쓰는 것이 싸고, 어차피 초안 B0도
"렌더 경로 재작성"이었다.

1. **런타임 모드 도입** — `EngineMode: Editor | Player`(§2.0). BUILD_FLAG의
   행동 가드 ~17곳을 모드 분기로 전환. 모드는 진입점 첫 줄에서 정해지고
   불변, PathFinder 초기화가 assert로 확인.
2. **Player 프로젝트 신설** — CreatorEngine.sln 소속, Debug/Release
   라이브러리 링크(전용 구성 없음). main은 Dx11Main을 참조해 새로 쓰되
   **Dx11Main 자체는 수정하지 않는다**(당시 R5와의 충돌 회피 — 이후 공통
   frame/bootstrap 경계는 E3·E6이 맡는다). 초안 B0의 이식 목록이 그대로 새 main의 명세다: DX12
   InitializeRuntime + SetRenderScene · 씬 델리게이트 2종(없으면 조용한 빈
   화면) · `ShaderSystem->Initialize()` · ClrHost 기동 + **프레임별
   TickScripts**(없으면 Awake 영영 없음) · ScreenResizeBus 초기값 ·
   WinProcProxy 드레인 · `--smoke N`(명령줄 파싱 신설 + 종료 코드 규약).
   ★ ImGuiRenderer는 얹지 않는 것을 기본으로 한다 — 게임 UI는 엔진 UI
   계통(UIPass)이지 ImGui가 아니다(구 GameMain이 매 프레임 돌리던 것은
   잔재). 라이브러리 내부의 ImGui 참조가 링크를 여전히 요구하므로 심볼 0은
   E4·E6에서 판정한다.
3. **TrainAsis · GameBuild.sln 제거** — 프로젝트·폴더 삭제, GameBuild.sln
   삭제, 참조 정리(build.yml · check_include_boundary.py 2곳 확인됨).
   PakHelper의 `TRAIN_ASIS` 하드코딩 2곳은 잠정 상수 하나로 통일(최종 project
   identity 주입은 E1·E6).
4. **시작 씬 정합** — EngineSettings.asset이 실존 씬을 가리키게(§2.4의 1호).

판정: **수동** 스모크로 산성 테스트 v1 상당 확인(자동 Verify는 B2부터 —
B0 시점에는 build.ps1이 없다). ★ 미답 경로의 첫 가동은 여전하므로 발견
예산 유지 — 여기서 나오는 버그는 발견 목록으로 넘긴다(고치는 것은 각자의
자리에서). 과도기 비만(§2.0)의 무해성도 이 스모크에서 확인한다.

> ✅ **완료(2026-08-10, 4커밋).** `Player.exe --smoke 120` → pak 언팩 →
> Scene loaded → 120프레임 → 종료 코드 0. Player.exe는 첫 시도에 링크됐다.
> 발견 셋: ① 구 GameApp의 종료 절차 둘(BUILD_FLAG 전용 — 한 번도 실행된
> 적 없음)이 전부 결함 — DataSystems->Finalize 이중 해제, 종료 시점 언팩
> 정리가 엔진 해체가 밟는 %TEMP% 트리를 선삭제. 정리를 부팅 직전으로 이전.
> ② 경계 게이트가 CoreWindow→Resource.h 상향 간선 적발(TrainAsis 삭제로
> basename 모호성 해소) — 아이콘 ID를 생성자 인자로 역전. ③
> **EngineSettings.asset이 `*.asset` gitignore 대상** — 시작 씬 정합이
> 로컬에만 있고, 자가 생성 기본값(SampleScene)도 실존하지 않는다. 프로젝트
> 설정의 저장소 진실 부재는 산성 테스트 v2의 구멍이고 E1·E6 project identity의
> 입력이다. 부수: `game.pak` CLI 신설(B2 Pak 단계의 입구), Package
> 출력(x64\GameBuild)과 Unpackage 탐색(exe 옆)의 경로 불일치는 B2 Stage가
> 잇는다.

**B1 — 빌드 기술 단일화.** ★ 개정: 방향이 초안과 반대다 — 구성을
늘리는(GameDebug 신설) 대신 **줄인다.** GameBuild 구성 제거(B0에서 소비자가
사라졌다) · x86(Win32) 열 제거 · 남는 공통 속성 전부(§1.1 후보:
PlatformToolset · CharacterSet · UseDebugLibraries · WPO ·
EnableUnitySupport · WarningLevel · LanguageStandard · AdditionalOptions ·
OutDir 규약)를 Directory.Build.props로 승격 · EngineOutput.props 흡수
은퇴(csproj의 `$(EngineOutDir)` 참조 2곳 이전 포함). 판정: vcxproj 안 구성
공통 속성 0 · 구성 매트릭스 {Debug, Release} × {x64} · Player가 Debug
링크로 디버거 아래서 뜬다.

**B2 — 오케스트레이터 ◐ Workspace·Editor Project gate 통과, Tracked/CI/운영성
잔여 (2026-08-21, model Cook 개정 2026-08-29).** `Tools/build.ps1`이 BuildNative→BuildManaged→Cook→
Stage→Pak→Verify와 원자적 publish를 소유한다. `Project`, `Workspace`, `Tracked`
입력은 live `EngineSettings.asset`을 제외하고 runtime settings overlay를 사용한다.
Verify는 startup scene, 렌더 진행, exact unpack hash, C#
`OnInitialized → OnBeginSimulation`, Stage 불변을 함께 판정한다.

`GameBuilderSystem::BuildGame()`과 `game.pak`은 시작 씬/backend를 전달해 같은 script를
호출한다. Editor가 vswhere/MSBuild 경로나 명령을 직접 구성하는 제품 경로는 0이며,
legacy Test Pack/Unpack UI도 제거했다. 현재 `-BuildNative`의 구성은 오케스트레이터만
소유한다. `MSBuildHelper`와 소비자 없는 VS/MSVC 탐색 상태도 제거했으며 별도
`EditorToolchainSettings`를 만들지 않는다. live 설정은 Editor의 단일
`EditorSettingsStore`만 unknown key를 보존해 candidate→atomic replace로 기록한다.
패키징은 `BuildSettings`의 선택을 runtime overlay `render.backend`로 투영하고 Player는
`build.render.backend`를 직접 읽지 않는다. E1 설정 재배선 뒤 Core/Editor 비유니티 build,
Editor 즉시 종료 6회, packaging boundary와 Workspace/Product Player smoke를 다시 통과했다.
Serialization D5-b2b2에서 Cook은 선택된 `InputMode`의 base snapshot을 입력으로 쓰고,
AssetCooker가 만든 model `Derived` tree만 merge한 후 pak에 넣는다. Debug/Project/
SkipVerify 실행은 model 14, CEMC 14 + CEMF 1, 529 pak entries를 생성했고
AssetPacker reopen/index 검증을 통과했다. 이 실행은 publish/runtime smoke는 의도적으로
생략했다. `DirectXTK12.dll`은 Player PE import가 아닌 낡은 runtime copy list 항목이었으며
그 목록에서 제거했다. `verify-mathematics-contract.ps1`은 manifest/헤더와 더불어
패키징 copy list의 `DirectXTK12.dll` 재유입도 거부한다.
`Release|x64`의 host compiler/linker도 `PreferredToolArchitecture=x64`로 고정한다. 이전에는
x86·x64 MSBuild 진입점이 같은 `IntDir`의 LTCG `.ipdb/.iobj`를 교대로 갱신해, x86 host로
전환할 때 7만여 함수 중 90% 이상을 재생성했다. `build.ps1`은 PATH의 x86 MSBuild를 같은
설치의 amd64 실행 파일로 승격하고 CI도 x64 MSBuild를 선택하며,
`verify-msbuild-tool-architecture.ps1`이 effective property를 회귀 검사한다.
다음 명령이 overlap/reparse/path alias, StageRoot/runtime junction, pak 누락의
exit 2·부분 PID root 정리, Player 정상 실행의 exact-root 정리와 sibling 보존을 한 번에
회귀시킨다.

```powershell
pwsh Tools/regression/verify-packaging-boundaries.ps1
```

2026-08-22 Release Editor의 `--exec game.pak --exec quit`도 exit 0으로 끝났고, 게시 release
`Dynamic_CPP-09179ad6e1474aef8cddef261c153f66`의 manifest는 `Project`,
`nativeBuildRequested=true`, `WORKTREE`, 470 entries, `FT_Primitives.creator`, `dx12`,
`verification=passed`, `smoke.exitCode=0`, managed lifecycle와 promotion 2회를 기록했다.
backend 인자를 생략한 Workspace release `Dynamic_CPP-796b2b22acfd4d00a6729c565710cb28`도
170 entries와 같은 runtime backend/smoke 판정을 통과해 기본 build→runtime 투영을 확인했다.

잔여는 (1) canonical scene/template/probe/도구를 HEAD에 편입한 뒤 `Tracked`
clean-checkout gate 통과, (2) PHASE 22 AU8/AU9의 FMOD 은퇴·miniaudio source 통합과 Release의 `fmodL` 제거,
(3) Editor UI의 동기 무한 대기를 비동기 progress/cancel/timeout/stdout 중계로 전환,
(4) forged pak·DOS device name·전체 entry 선검증/rollback, (5) 같은 leaf 이름의 외부
프로젝트 stage namespace 충돌, pointer 실패 orphan release 복구/GC다. 소비자 0인
`PakHelper::PackageGameAssets()`도 E2에서 제거한다.

**B3 — 셰이더 쿡 + 스테이징 ◐ 열거 계약 완료, artifact 경로 미착수.** 옛
`HLSLCompiler/.cso` 계획은 폐기한다. Material M2B의 단일
`EnumerateForBuild` 결과(GUID/pass/selection identity + ordinal defines)와
fail-closed compile-request 상한을 그대로 소비한다. 별도 Build 전용 keyword
열거/정렬 규칙은 만들지 않는다.
`RHIShaderCompiler`가 실제로 받는 source, entry point, target profile, defines/
specialization identity, DXIL/SPIR-V, strict-math 옵션, compiler identity·version과 include graph를
정본 request manifest로 만든다. 여기에 M7의 정규화된 resource kind/register/space와
cbuffer field type/offset/size layout identity를 함께 넣는다. Cook은
그 정체성으로 artifact를 생성하고 Stage는 source/SDK fallback 없이 artifact만 읽는다.
DX12와 Vulkan 각각 cold-cache 패키지 첫 실행에서 shader compile 통계가 0이고 failure가
0이어야 완료다. B2는 Slang compiler bundle과 HLSL source를 싣는 런타임 컴파일
방식이므로 이번 M1B의 DX12/Vulkan package smoke도 B3 완료 증거로 사용하지 않는다.

**B4 — C# 편입 ◐ 패키지 경로 완료, 빌드 정의 통합 잔여.** `build.ps1`이 ScriptCore와
GameScripts를 순서대로 빌드하고, 실행에 필요한 managed 파일의 exact allowlist를
`Managed\`에 배치한다. `FT_Primitives`의 scene-resident `PackageSmokeProbe`가 패키지에서
`OnInitialized → OnBeginSimulation`을 각각 정확히 한 번 남겨 실제 로드와 tick을
검증한다. 남은 일은 Academy_4Q의 구성별 PreBuildEvent 복제를 공용 targets로 옮겨
Editor native build와 오케스트레이터가 같은 managed build 정의를 소비하게 하는 것이다.

**B5 — CI 게임 레그.** 선행 결정은 2026-08-24에 닫혔다. FMOD 바이너리를 CI에 공급하거나
오디오를 compile-out하지 않는다. `AudioBackendModernizationPlan.md` AU8/AU9에서 FMOD를 제거하고
pinned miniaudio source를 Player에 정적으로 포함한 뒤 build.yml에 `build.ps1 -BuildNative` 전체
파이프라인 레그를 추가한다(Player.exe 링크 포함). 패키지를 아티팩트로 올리고 WAV/MP3/FLAC smoke,
FMOD PE import 0, `miniaudio.dll` 0을 함께 판정한다. 판정: 산성 테스트 v2.

### Editor/Core 개편과의 접점

Editor/Core 소유권 이동의 순서와 완료 판정은 `EngineLayerSeparationPlan.md`의
E0~E7만 따른다. 이 문서는 각 E 슬라이스가 재사용할 package/smoke gate와 B0~B5의
빌드·쿡·배포 순서만 정의한다. 삭제된 진입점과 구 L/D 단계에 기반한 과거 계획은
Git history로 보존한다.

```text
B0 → B1 → B2 → { B3, B4 } → B5
                 └─ E0~E7 각 슬라이스의 package/smoke 판정으로 재사용
```

---

## 4. 하지 않을 것

- **UBT식 자체 빌드 도구.** 얻으려는 것은 "빌드 진실 한 벌"이고, 그것은
  props + 스크립트로 이미 얻어진다. 네이티브 9프로젝트 규모에서 도구는
  유지비가 이득을 넘는다.
- **에셋 포맷 쿡(텍스처 재압축 · 플랫폼 변환).** 지금 pak은 원본 운반이고
  그것의 문제는 용량·로드 시간이지 정확성이 아니다. DX12 안정 후
  AssetResidencyPlan과 연계해 별도로.
- **pak 마운트 전환.** 언팩 방식의 문제도 성능이지 정확성이 아니다.
  지금은 정확성 구멍(§0)부터.
- **구성 이름 개편 · BUILD_FLAG 개명.** WITH_EDITOR 식 이름이 더 곱지만
  개명은 코스메틱 churn이다. BUILD_FLAG는 개명이 아니라 두 갈래로
  소멸한다(§2.0) — 행동 가드는 E1의 Host capability로, 코드 가드는 E2·E4·E6의
  물리 소유권 분리로 없앤다.
- **vcpkg 매니페스트 전환.** 대시보드 5-6 별도 트랙 유지.
- **DX12 셰이더/PSO 워밍업 쿡.** 패스의 런타임 D3DCompile은 부팅 시간
  주제이고, 그 캐시(`dx12_*.cache`)는 이미 자가 치유로 돈다. 후속.

---

## 5. 위험

- **검증되지 않은 clean checkout.** canonical fixture/template/tool과 PHASE 22 오디오 전환이
  HEAD 밖에 있어 Workspace 성공을 CI 재현성으로 확대할 수 없다. `Tracked`가
  working-tree fallback 없이 실패하는 상태를 유지하고 B5에서 닫는다.
- **Editor 제품 경로의 동기 실행.** `GameBuilderSystem`은 UI thread에서 child process를
  무한 대기한다. 기능 정본은 한 벌이지만 timeout/cancel/progress/stdout 중계가 없어
  긴 빌드를 hang으로 오인할 수 있다.
- **publish 복구.** pointer 교체 전 실패는 current를 보존하지만 candidate 또는
  unpublished immutable release가 남을 수 있다. project leaf namespace 충돌과 함께
  recovery/GC 정책이 필요하다.
- **pak 적대 입력.** 현재 source-generated pak smoke만 통과했다. forged archive의 DOS
  device name, index/payload 경계, file-directory prefix collision, late-entry failure와
  partial extraction rollback은 별도 fixture가 없으므로 완료로 보지 않는다.
- **런타임 TEMP 소유권.** Player Host가 owner/process/content/data root를 주입하고
  Utility의 TEMP/PID/제품명 재구성은 0이 됐다. exact current-PID 삭제, sibling 보존,
  owner-junction fail-closed도 회귀시킨다. 다만 실제 병렬 두 Player 수명 교차와
  validation↔delete 사이 child-junction 교체 경쟁은 아직 후속이다.
- **FMOD.** import/runtime 바이너리 공급이 저장소 밖이고 Release가 아직 `fmodL`을 우선
  링크·스테이징한다. B5 전에 PHASE 22 AU8/AU9에서 source/project/stage/PE import를 0으로 만들고
  pinned miniaudio provenance와 WAV/MP3/FLAC package smoke를 함께 고정한다.

---

## 6. 완료 기준 (수치)

1. 산성 테스트 v1 통과 — 에디터 0회 기동 체크아웃에서 패키지가 첫 씬 렌더 +
   C# `OnInitialized → OnBeginSimulation` marker + 정상 종료 (트랙 B 완료 시)
2. 산성 테스트 v2 통과 — CI가 게임 패키지를 아티팩트로 산출 (B5)
3. vcxproj에서 구성 공통 속성 중복 0 — PlatformToolset 52회 → 프로젝트당
   1회, OutDir 재정의 14줄 → 0줄 · 구성 매트릭스 {Debug, Release} × {x64}
   (GameBuild·Win32 열 소멸) (B1)
4. 디버거 아래서 뜨는 플레이어 — Player가 Debug 라이브러리 링크로 기동
   (B0·B1)
5. BUILD_FLAG 여는 지시문 48 → 0 — E1 Host capability와 E2·E4·E6 물리 분리
6. DYNAMICCPP_EXPORTS 187 → 0 — E6 프로젝트 경계 확정 때 dead guard 제거
7. 플레이어 산출물에서 ImGui 심볼 0 — Player가 ImGuiHelper를 링크하지
   않는다 (E4 + E6)
8. 공통 frame orchestration 한 벌 — Editor/Player 차이는 Host adapter로 제한 (E3 + E6)
9. `TRAIN_ASIS`·`..\..\Dynamic_CPP` 등 Host/project 경로 하드코딩 0 (E1)
10. `GameBuilderSystem`의 vswhere·MSBuild 경로/명령 조립 0 — `MSBuildHelper`와 죽은
    toolchain 설정도 제거하고, native build가 필요한 과도기에는 `build.ps1`만 구성과
    실행을 소유한다 (B2·E1)

---

## 7. 문서 관계

- **EngineLayerSeparationPlan.md** — Runtime Core/Editor/Host의 소스 소유권,
  프로젝트 참조, 실행 순서 E0~E7의 현재 기준. 빌드·쿡·패키징 트랙 B와
  smoke/산성 테스트는 계속 이 문서가 기준이다.
- **EnginePackagingPlan.md** — 이미 L1의 설계 근거로 흡수된 상태. 잔여
  몫의 처리처는 셋으로 갈라져 있다: P5의 RenderEngine 몫 →
  RhiBoundaryPlan R축, §4.1 지형 몫 → 대시보드 PHASE 11(전부 todo),
  P4 → 대시보드 5-2. 이 문서가 새로 가져갈 것 없음.
- **RhiBoundaryPlan.md** — 병행 트랙. §5의 핫 존 목록이 그 문서의 잔여
  슬라이스(R4 그래프 서명 · R5 구 RHI 은퇴)에서 나온다. T축은 T6까지 전부
  완료다(그 문서의 §4.3 본문이 뒤쪽 정정 노트보다 낡아 "남은 곳" 표를 아직
  들고 있으니 주의 — 진실은 :1004 정정 노트 쪽).
- **대시보드** — PHASE 12로 등재(12-0 ~ 12-10).
