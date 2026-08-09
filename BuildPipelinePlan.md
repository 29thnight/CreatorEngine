# 빌드 파이프라인 신설과 엔진 계층 전면 개편 (BuildPipelinePlan)

작성: 2026-08-09 · 계기: "언리얼·유니티는 게임을 어떻게 빌드하나"라는 질문에
답하다가 드러난 구멍들. 답을 적고 보니 우리에게 없는 것이 단계 하나(쿡)가
아니라 심판 전체였다.
승계: `EngineLayerSeparationPlan.md`의 L2~L4를 이 문서가 흡수한다(§7).
실측: 2026-08-09, 5방향 병렬 조사(빌드 구성 · 경계 매크로 · 에셋 흐름 ·
기존 계획 정합 · 플레이어와 C#). 아래 수치는 전부 그 조사에서 직접 센 값이다.
검증: 같은 날 적대 검증 3방향(사실 대조 · 문서 정합 · 홍팀)을 통과시키며
지적 12건을 반영했다 — 가장 큰 정정은 B0의 규모(§3)다.
개정(2026-08-10, 사용자 결정): 게임 빌드를 언리얼식(엔진 재컴파일)에서
**유니티식(선빌드 플레이어 + 복사)**으로 전환 — TrainAsis·GameBuild.sln 제거,
Player 프로젝트 신설. 근거와 대가는 §2.0, 개정된 곳은 §1.6·§2.2·§2.3·§3.

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

### 1.3 에셋 흐름 — 쿡이 없어서가 아니라, 길 자체가 끊겨 있다

셰이더의 길을 끝까지 따라가면:

```
발견     LoadShaders()가 ShaderSourcePath에서 *.hlsl을 재귀 순회
         — 발견 트리거가 cso가 아니라 hlsl의 물리적 존재다 (ShaderSystem.cpp:53-55)
판정     cso 있음 + 에디터  → 타임스탬프 비교, 최신 쪽 로드 (:64-85)
         cso 있음 + 게임    → 검사 없이 cso 로드 (:86-92)
         cso 없음           → 게임이든 에디터든 hlsl 런타임 컴파일 (:95-102)
캐시     컴파일 결과를 PrecompiledShaderPath에 .cso로 기록 (자가 치유 캐시)
```

그리고 배포 쪽 사실 둘:

- **pak에는 hlsl 소스가 담기고 cso는 담기지 않는다.** PackageGameAssets의
  소스 루트는 Assets(hlsl 포함)와 ProjectSetting 둘뿐이고,
  PrecompiledShaderPath(`<exe>\..\Assets\Shaders\`, BUILD_FLAG 무관 고정)는
  패킹 대상에 없다. 그 폴더를 게임 산출물 옆에 채우는 post-build 복사도
  저장소 어디에도 없다.
- 따라서 게임이 (셰이더 로드를 하게 된다면) 첫 실행에서 **전 셰이더를
  런타임 컴파일**하고, 이후 실행은 그때 캐시된 cso를 검사 없이 신는다 —
  "낡은 cso 출하"가 아니라 "cso를 아예 안 싣고 매 배포 첫 실행이 컴파일
  타임"인 그림이다. 다만 §0에서 봤듯 지금은 로드 호출 자체가 없어서 이
  경로조차 돈 적이 없는 것으로 보인다.

pak 소비는 마운트가 아니라 **기동 시 `%TEMP%\UnpackedAssets\`에 전체 언팩**
이다(EngineSetting::Initialize의 BUILD_FLAG 분기 → UnpackageGameAssets,
종료 시 Cleanup). PathFinder에는 하드코딩 경로가 17곳 — 머신 절대 경로
4(vswhere 1 + **MSBuild.exe 3벌**), 실행 파일 상대 10(`..\..\Dynamic_CPP`
계열 5 포함), %TEMP% 상대 3. pak 이름 `TRAIN_ASIS`는 PakHelper.h 안에
두 번(:113, :410) 따로 하드코딩돼 있다.

MSBuild 경로 취득도 겉과 속이 다르다: vswhere.exe를 실제로 실행하지만 그
출력(설치 경로)은 **버려지고**, MSVCVersion enum 판별에만 쓰인 뒤 실제
경로는 PathFinder.h:75-77의 하드코딩 리터럴 3종 중 하나를 돌려준다 — VS가
관례적 위치가 아니면 vswhere가 성공해도 없는 경로가 나온다.

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
| 엔진 재컴파일 | 한다 | 안 한다 | **게임 빌드에서는 안 한다** (초안은 언리얼 모델 — 정정 경위는 §2.0) |
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

- **게임 빌드가 분에서 초로.** MSBuild `/t:Rebuild`가 사라지고
  복사·쿡·pak만 남는다. 산성 테스트(§2.4)도 그만큼 빨라진다.
- **에디터의 MSBuild 기계가 통째로 은퇴한다.** vswhere 호출 · MSBuild 경로
  하드코딩 3벌 · MSBuildHelper(315줄) · GameBuilderSystem의 커맨드 조립 —
  전부 게임 빌드가 엔진을 재컴파일하기 때문에 있던 것이다. §1.3의 발견
  ("vswhere 출력이 버려진다")은 수리 대상이 아니라 원인째 소멸한다.
- **TrainAsis·GameBuild.sln이 소멸한다.** 컴파일 불능 클론(§1.4)을
  소생시키는 대신 Player 프로젝트를 CreatorEngine.sln 안에 새로 세운다 —
  에디터와 함께 항상 빌드되고, 항상 그린이고, CI가 본다. sln 두 벌
  유지비(§1.1의 SolutionGuid 복제 같은 부류)도 사라진다.
- **구성 매트릭스가 준다.** GameBuild 재해석도 GameDebug 신설도 필요
  없다(§2.2 개정).

무엇을 치르는가(정직하게):

- **BUILD_FLAG의 행동 스위치를 런타임 모드로 바꿔야 한다.** 48곳 중
  ~17곳은 코드 절단이 아니라 행동 분기다 — PathFinder의 %TEMP% 경로 ·
  EngineSetting의 pak 언팩 · SceneManager의 자동 시작 · CoreWindow의
  보더리스 전체화면 · TagManager의 읽기 전용 · GameApp/EngineBootstrap 로그
  태그. 단일 라이브러리 바이너리 세계에서 이것들은 **진입점이 정하는 런타임
  모드**(`EngineMode: Editor | Player`)가 돼야 한다. 컴파일 타임 보증이
  런타임 보증으로 약해지는 지점이므로, 모드는 부팅 첫 줄에서 한 번 정해지고
  이후 불변 + 초기화 assert.
- **과도기 플레이어는 뚱뚱하다.** 나머지 ~31곳(#ifndef의 에디터 전용 코드 —
  프로파일러 · 콘텐츠 브라우저 · 셰이더 타임스탬프 등)은 Release 라이브러리에
  컴파일돼 들어간 채 플레이어에 실린다. 호출자가 에디터 창뿐이라 동작은
  무해하고(스모크로 확인), 무게는 L2'의 물리 분리가 걷는다. 즉 BUILD_FLAG는
  개명도 재배치도 아니라 **두 갈래로 소멸한다: 행동 → 런타임 모드(B0),
  코드 → 물리 분리(L2').**
- **게임별 네이티브 최적화의 자유를 잃는다** — 게임 코드가 C#뿐이라 지금
  실익이 없다. 미래에 게임별 네이티브 플러그인 개념이 생기면 이 결정을
  재검토한다(Project.cproj에 그 개념이 생기기 전까지는 성립).

### 2.1 계층 — 다섯 층

| 층 | 소속 (목표) | 지금과의 차이 |
|---|---|---|
| **Runtime** | Utility_Framework · RenderEngine · Physics · ScriptBinder · SingletonManager · ManagedHeap · ScriptCore(관리) | 에디터 코드가 물리적으로 나간다 — BUILD_FLAG로 가리는 게 아니라 |
| **Editor** | EngineGUIWindow · ImGuiHelper · EngineEntry 대부분 · Runtime에서 적출되는 것들(DataSystem 에디터 UI · 노드에디터 자료 등) | 지금은 절반이 Runtime 라이브러리 안에 산다 |
| **Player** | 신설 프로젝트(CreatorEngine.sln 소속) — 에디터와 함께 항상 빌드, 공용 런타임 부트는 L4'에서 | TrainAsis(컴파일 불능 76% 클론)는 제거 |
| **Project** | Dynamic_CPP (`Project.cproj` + Assets + ProjectSetting + Scripts) | 지금은 `..\..\Dynamic_CPP` 하드코딩이 가리키는 폴더 |
| **Tools** | HeaderTool · AutoRegisterCreateReflection · 빌드 스크립트(신설) | 지금도 대체로 분리돼 있다 |

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
Tools/build.ps1 -Config Release [-Project <경로>] [-BuildNative]
  1 (BuildNative)  MSBuild CreatorEngine.sln — CI·클린 체크아웃 전용 스위치.
                   에디터의 게임 빌드 버튼은 이 단계를 부르지 않는다:
                   Player.exe는 에디터와 함께 이미 빌드돼 있다(없으면
                   안내하고 실패 — 조용히 낡은 것을 싣지 않는다)
  2 BuildManaged   dotnet build ScriptCore → GameScripts
  3 Cook           hlsl 전량 → cso (하나라도 실패하면 빌드 실패)
  4 Stage          Build/Staging/<프로젝트>/ ← Player.exe · dll · Managed/ · cso
  5 Pak            Assets + ProjectSetting → <프로젝트>.pak (스테이징에 산출)
  6 Verify         패키지 스모크 — 플레이어를 --smoke N으로 기동, N프레임 후
                   종료 코드 + 로그 마커(씬 로드 성공 · C# Awake)로 판정
```

- **Verify의 판정은 종료 코드만으로 안 된다.** 지금 부트스트랩은 성패와
  무관하게 0을 반환하고(EngineBootstrap::Run), 씬 로드 실패는 함수 전체
  try/catch에 삼켜져 로그 한 줄로 끝난다 — "아무것도 안 그려짐"과 "정상"이
  종료 코드로 구분되지 않는다. 그래서 스모크는 ① 신설하는 종료 코드
  규약(치명 실패 시 비-0)과 ② 로그 마커 파싱(씬 로드 성공 · Awake 발생)
  둘 다를 본다. `--smoke` 인자 자체도 신설이다 — 지금 플레이어는 명령줄을
  파싱하지 않는다(wWinMain의 인자가 이름 없이 버려진다).

- **에디터의 BuildGame() 버튼과 CI가 같은 스크립트를 부른다.** 진실이 한
  벌이면 "에디터에서는 되는데 CI에서는 안 된다"가 구조적으로 없어진다.
  GameBuilderSystem은 스크립트 호출 + 진행률 중계로 얇아진다.
- 쿡의 정직한 범위: **ShaderSystem이 소비하는 hlsl→cso까지다.** 컴파일
  코어는 이미 `Utility_Framework/HLSLCompiler.cpp`(D3DCompileFromFile)에
  있으므로 그것을 재사용하는 헤드리스 진입점을 만든다 — 컴파일 옵션의
  진실이 두 벌이 되지 않게. DX12 패스의 런타임 D3DCompile·PSO 캐시는 이번
  범위 밖이다(§4).
- **쿡 도구는 경로를 PathFinder에 기대지 않는다** — 입력·출력 경로를 인자로
  받고 build.ps1이 명시적으로 전달한다. PathFinder의 exe 상대 가정 정리는
  L3' 몫인데 B3가 그보다 앞서므로, 인자화가 그 순서 의존을 끊는다.
- 에디터의 MSBuild 의존은 수리가 아니라 **제거**된다(§2.0) — 게임 빌드가
  재컴파일을 안 하므로 vswhere · MSBuildHelper · 경로 하드코딩 3벌이
  소비자를 잃는다. 유니티가 게임 빌드에 VS를 요구하지 않는 것과 같은
  이유로, 우리 에디터도 요구하지 않게 된다.

### 2.4 산성 테스트 — 모든 슬라이스의 심판

> **에디터를 한 번도 띄우지 않은 체크아웃에서 `build.ps1 -Target Game`이
> 패키지를 내고, 그 패키지가 첫 씬을 그리고 C# Awake 로그를 남기고 정상
> 종료한다.**

- v1(트랙 B의 판정): 개발 머신 기준 — FMOD 바이너리가 이미 있는 환경.
  "에디터 미기동"이 핵심이다(쿡이 에디터 부산물이 아님의 증명).
- v2(B5의 판정): CI 기준 — 클린 머신. FMOD 공급 결정(§3 B5)이 선행.

트랙 L의 모든 슬라이스는 커밋 전에 이 테스트를 다시 통과해야 한다.
**계층 이동의 판정이 "컴파일된다"에서 "패키지가 돈다"로 올라간다** — 그것이
이 계획 전체의 요점이다.

★ 산성 테스트의 첫 장애물은 이미 발견돼 있다: 저장소의 시작 씬 설정
(`Dynamic_CPP/ProjectSetting/EngineSettings.asset`의 startupSceneName:
MainScreenScene.creator)이 가리키는 씬 파일이 Assets/Scenes/에 없다 — 지금
그대로 스모크를 돌리면 씬 로드가 조용히 실패하고도 종료 코드 0으로 끝난다.
B0 발견 목록의 1호다.

---

## 3. 실행 — 슬라이스

### 트랙 B — 심판 세우기 (선행)

**B0 — 플레이어 소생.** 가장 먼저이고, 처음 초안에 적었던 "초기화 3종
이식"보다 크다 — 홍팀 검증이 GameMain과 에디터 부트를 전문 대조해 이식
목록을 확정했다:

1. **렌더 경로 재작성** — GameMain이 여섯 곳에서 쓰는 `SceneRenderer`는
   삭제된 타입이다(§0). CommandBuild/Execute 스레드·리사이즈·Finalize를
   Dx11Main이 이미 세워 둔 EnhancedSceneRenderer 패턴으로 다시 쓴다.
2. DX12 InitializeRuntime + `SetRenderScene` 배선.
3. **씬 델리게이트 2종** (newSceneCreated · activeSceneChanged →
   SetActiveScene) — 없으면 렌더러가 활성 씬을 통지받지 못해 크래시도
   로그도 없이 빈 화면만 그린다.
4. `ShaderSystem->Initialize()`.
5. ClrHost 기동 + **프레임별 TickScripts 구동** — Initialize만 이식하면
   CLR은 뜨지만 Awake는 영영 발생하지 않는다(Awake는 매 프레임 불리는
   TickAwake가 낸다).
6. ScreenResizeBus 초기값 설정 — D4가 화면 크기 배선을 이 버스로 옮겼으므로
   생략하면 렌더 타깃이 크기 0으로 설 위험(추정 — 첫 가동에서 확인).
7. WinProcProxy 메시지 드레인 — 에디터·플레이어 공용 큐를 플레이어가 비우지
   않아 누적되는 완만한 누수.
8. TrainAsis.vcxproj 수복 — SubSystem=Windows 통일 · AdditionalDependencies
   구성 무조건화 · PlatformToolset v145 정렬.
9. `--smoke N` 신설 — 명령줄 파싱 인프라(지금은 아예 없다) + 프레임 종료 +
   종료 코드 규약(§2.3).
10. 시작 씬 정합 — EngineSettings.asset이 실존 씬을 가리키게(§2.4의 1호).

**클론인 채로 고친다** — 사본을 한 번 더 갱신하는 셈이지만, 클론 해소(L4')는
심판이 선 다음에 해야 안전하다. 판정: **수동** 스모크로 산성 테스트 v1 상당
확인(자동 Verify는 B2부터 — B0 시점에는 build.ps1이 없다). ★ 미답 경로의
첫 가동 + 렌더 경로 재작성이라 트랙 B에서 가장 큰 슬라이스다. 여기서 나오는
버그는 발견 목록으로 넘긴다(고치는 것은 각자의 자리에서).

**B1 — 빌드 기술 단일화.** §1.1의 공용화 후보 전부를 Directory.Build.props로
승격(PlatformToolset · CharacterSet · UseDebugLibraries · WPO ·
EnableUnitySupport · WarningLevel · LanguageStandard · AdditionalOptions ·
OutDir 규약), GameDebug 구성 신설(props 정의 + GameBuild.sln 구성 매핑 추가),
EngineOutput.props는 Directory.Build.props에 흡수하고 은퇴(csproj의
`$(EngineOutDir)` 참조 2곳 이전 포함). 판정: vcxproj 안 구성 공통 속성 0 +
전 구성 빌드 그린 + GameDebug로 플레이어가 디버거 아래서 뜬다.

**B2 — 오케스트레이터.** `Tools/build.ps1` 신설(§2.3의 6단계 — 이 시점에는
3 Cook이 아직 비어 있어도 좋다), GameBuilderSystem::BuildGame을 스크립트
호출로 축소, `/t:Rebuild` → 증분, vswhere 수리. 판정: 에디터 버튼과 CLI가
같은 산출물을 낸다.

**B3 — 셰이더 쿡 + 스테이징.** HLSLCompiler를 재사용하는 쿡 진입점(콘솔
도구 — 에디터 CLI가 아닌 이유는 에디터가 FMOD 없이 링크되지 않아 CI에서
못 돌기 때문), cso를 Stage에 배치하고 플레이어의 PrecompiledShaderPath를
스테이징 규약에 맞춘다. 판정: 패키지 첫 실행에서 셰이더 런타임 컴파일 0건
(로그로 계수). ★ 실현성은 검증됐다 — HLSLCompiler에 DX11 디바이스 의존이
0건이고, 엔트리 포인트("main")·타깃 프로파일(파일명 하위 확장자) 결정까지
그 파일 안에 있어 진실 두 벌이 생기지 않는다. 두 전제만 지킨다: 경로
인자화(§2.3), 그리고 이 "콘솔 도구"가 HLSLCompiler.h → Core.Minimal.h
전이로 리플렉션·로그 기계장치 전부를(L2'-1 이전에는 imgui.h까지 — 링크는
강제되지 않아 무해) 컴파일 타임에 끌고 온다는 것 — 이름보다 무겁다.

**B4 — C# 편입.** C# 빌드의 진실은 지금 Academy_4Q.vcxproj의 PreBuildEvent
4곳(Debug·Release × ScriptCore·GameScripts)에 있다 — 에디터를 빌드할 때만
생성되고 게임 경로에는 없다. 이 호출을 한 벌로 옮긴다: build.ps1의
BuildManaged 단계와 에디터 빌드가 같은 정의를 쓰도록 공용 targets로
승격하고 PreBuildEvent 4중 복제를 걷는다. Managed/를 Stage로 배치한다 —
ClrHost의 로드 경로(exe 옆 `Managed\`)는 이미 맞으므로 배치만 문제다.
판정: 패키지에서 C# Awake 로그(그 로그를 가능케 하는 틱 구동은 B0-5).

**B5 — CI 게임 레그.** 선행 결정: FMOD 바이너리 공급 방식 —
(a) 사설 아티팩트/캐시로 CI에 공급(권고 — FMOD 라이선스는 저장소 공개
재배포가 문제지 CI 캐시는 통상 관행이다) 또는 (b) 오디오 백엔드 컴파일
아웃 스위치(작업이 더 크고 이득이 CI 하나뿐). 결정 후 build.yml에
`build.ps1 -Target Game` 레그를 추가하고 패키지를 아티팩트로 올린다.
판정: 산성 테스트 v2.

### 트랙 L — 심판 아래의 개편 (B1 이후 착수)

L2'가 기다리는 것은 B1이다 — BUILD_FLAG 정의가 props 한 곳으로 옮겨진 상태.
판정에 필요한 CI Debug 레그(유니티 끔)는 이미 있으므로 B2의 스크립트를
기다릴 이유가 없다(홍팀 지적으로 게이트를 B2 → B1로 낮췄다).

**L2' — 에디터 적출.** 순서 있는 소슬라이스로:

1. **ReflectionFunction.h의 imgui include 절단.** §1.2의 전이 경로 —
   Core.Minimal.h 계보를 타고 90여 개 파일에 퍼지는 뿌리다. 이 한 줄을
   빼면 imgui를 정말 쓰는 파일들이 일제히 드러난다(첫 빌드가 크게 아플
   것이고, 그것이 목적이다 — 숨은 의존의 가시화). 유니티 빌드 전이
   include 함정 주의: CI Debug 레그(EnableUnitySupport=false)로 판정.
2. **노드에디터 자료 분리.** BT·애니메이터 4종 헤더에서 imgui-node-editor
   의존을 에디터 측 자료(빌드/편집 헬퍼)로 갈라낸다 — 런타임 자료는
   순수 데이터로.
3. **Camera ImGui 폴백 제거.** ImGui::IsKeyDown 폴백 6건(W·S·A·D·Q·E).
   ★ 대시보드 4-2 C6과 같은 함수에 있지만 **다른 간선이다** — 이쪽은
   RenderEngine→ImGuiHelper(경계 게이트가 추적하지 않는 축), C6은
   RenderEngine→ScriptBinder(InputManager·MeshRenderer, 허용 목록의 2간선).
   하나를 끝내도 다른 하나는 남는다. 같은 슬라이스에서 함께 처리하되
   완료는 따로 센다(초안이 "한 몸"이라 적었던 것을 정합 검증이 바로잡았다).
4. **DataSystem 분리.** BUILD_FLAG 최대 집중지(19곳) — 대시보드 **4-3과
   동일 작업**이므로 그 항목의 실행이 곧 이 소슬라이스다(이 동일성은
   Phase4CouplingPlan C4가 이미 명시).
5. **PrefabEditor 이주.** 구 L2-2 승계 — `ScriptBinder/PrefabEditor.{h,cpp}`
   → EngineGUIWindow. 소비자가 에디터 창들뿐이라 이동 + include 수정으로
   끝난다.
6. **RenderEngine ImGui 잔여 정리.** 구 L2-3의 12파일 3분류 승계 —
   ① 에디터 전용 클래스 이주: GizmoRenderer · EditorImGuiTexture ·
   ImGuiRenderer · ImGuiDx12Shell (RenderDebugManager는 D4에서 이미 소멸)
   ② 엔진 클래스에 섞인 UI 조각의 DrawHelper화: ShaderSystem.cpp ·
   RenderScene.cpp (Camera는 3에서, DataSystem은 4에서 처리)
   ③ 디버그 오버레이의 구조체 노출: EnhancedSceneRenderer ·
   DX12DeviceResources — ★ DX12 핫 존이라 R4·R5가 닫힌 뒤로 미룬다.
7. **EDITOR 매크로 정리.** 구 L2-1 승계(ImGuiRegister.h 자가 define 제거,
   에디터 exe 구성에서 정의). ★ 구 계획이 적어 둔 대로 "Release 에디터에서
   창이 사라질 수 있는" 유일한 동작 변화 지점.

판정(구 L2-4 승계·정정): 엔진 라이브러리 4종(RenderEngine · ScriptBinder ·
Utility_Framework · Physics)의 ImGui include 0 · BUILD_FLAG 가드 수가
소슬라이스마다 단조 감소(48 → 0이 완료 기준). ★ "플레이어 산출물에서 ImGui
심볼 0"은 L2'만으로 달성되지 않는다 — GameMain 자신이 ImGuiRenderer를
소유하고 매 프레임 돌리므로(§1.2), 그 제거와 GameBuild.sln의 ImGuiHelper
참조 제거는 L4'(공용 부트에서 GUI를 어댑터로 분리)와의 합산 판정이다.

**L3' — 게임 프로젝트화.** 구 L3 승계: `Project.cproj` 도입 · PathFinder
역전(§1.3의 하드코딩 17곳 목록이 작업 목록이다 — 진입은 인자 → 최근 프로젝트
→ Dynamic_CPP 폴백) · pak 이름을 프로젝트 파일에서(`TRAIN_ASIS` 하드코딩
2곳 제거) · GameScripts를 프로젝트 소속으로. 판정: Dynamic_CPP를 다른
경로에 복사해 인자로 열어도 에디터·패키지가 동일 동작(구 L3-5).

**L4' — 플레이어 정식화.** GameMain/Dx11Main의 공통 부트를 한 벌로 추출
(243줄 중복 + B0가 보탠 사본까지 여기서 청산) — 부팅 순서·스레드 구조·
델리게이트 배선이 공용 런타임 부트로, 에디터/플레이어는 각자의 창·GUI
어댑터만 얹는다. TrainAsis → Player 개명은 L5'와 묶는다(경로 변경 최소화).
판정: 부트 시퀀스 코드가 저장소에 정확히 한 벌 + 산성 테스트.

**L5' — 저장소 재편 (선택 · 마지막).** 구 L4-2 승계(`Engine/ · Editor/ ·
Projects/` git mv). ★ 대시보드 4-5가 실측해 둔 함정을 흡수한다: include가
경로 없는 이름 방식이라 물리 이동은 include 경로 관성과 정면충돌한다
(약 1,000곳). 착수 조건 셋 — L2'~L4' 완료 · 동시 세션 없음 · 하루 통짜
배정. L0~L4'의 가치는 이것 없이 전부 성립하므로 무기한 미뤄도 된다.

### D — 죽은 가드 일소 (독립 슬라이스)

DYNAMICCPP_EXPORTS 187곳/184파일 제거. 기계적이지만 접촉면이 저장소
전체라(핫 존 포함) **동시 작업과 정면 충돌하는 종류다.** ★ "조용한 창을
기다린다"는 계획이 못 된다 — 이 계획의 검증이 도는 동안에도 전역 구조 변경
커밋이 실제로 들어왔다(하루 다건이 이 저장소의 상시 상태다). 창은 기다리는
것이 아니라 만드는 것이다: 착수 직전 HEAD 재대조 → 한 커밋 단행 → 즉시
공유. 전 구성 빌드 + 산성 테스트로 판정. 트랙 B·L 어느 것도 이것을
기다리지 않는다.

### 순서도

```
B0 → B1 → B2 → { B3, B4 } → B5
       └→ L2'-1 → L2'-2~7 → L3' → L4' → (L5')
D는 독립 — 착수 직전 HEAD 재대조 후 한 커밋
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
  개명은 코스메틱 churn이다. L2'가 끝나면 BUILD_FLAG는 개명이 아니라
  소멸한다.
- **vcpkg 매니페스트 전환.** 대시보드 4-6 별도 트랙 유지.
- **DX12 셰이더/PSO 워밍업 쿡.** 패스의 런타임 D3DCompile은 부팅 시간
  주제이고, 그 캐시(`dx12_*.cache`)는 이미 자가 치유로 돈다. 후속.

---

## 5. 위험

- **동시 세션 충돌.** DX12 트랙의 잔여(R4 그래프 서명 · R5 구 RHI 은퇴 —
  T축은 T6까지 전부 완료라 핫 존에서 빠진다)가 만질 파일들:
  `EnhancedRenderGraph.{h,cpp}`와 그것을 참조하는 RHI/DX12 폴더의 62개
  파일(패스 17종 + 자가 검증), 구 RHI 4종(`RHI.*` · `RHIDevice.h` ·
  `RHICommandContext.h` · `DX11RHI.*`), 그 죽은 include의 소비자
  `EngineEntry/Dx11Main.cpp` · `RenderPassData.cpp`. 이 계획의 접촉면과
  겹치는 곳: B0가 Dx11Main을 **읽고**(수정은 GameMain 쪽만 — 낮음),
  **B1이 vcxproj 9개를 동시 편집**(다른 세션도 같은 파일을 만질 수 있는
  전역 변경 — 착수 직전 HEAD 재대조), L4'의 공통 부트 추출이 Dx11Main을
  **수정**(높음 — R5가 Dx11Main의 죽은 RHI include를 걷은 뒤로 미룬다),
  D 슬라이스 전면 접촉(§3 D의 절차). 커밋 전 HEAD 재대조 원칙 유지.
- **B0는 미답 경로의 첫 가동.** 플레이어 렌더·언팩·스크립트 경로 어디서
  무엇이 터질지 모른다. B0의 완료 정의를 "발견 목록 완성 + 스모크 통과"로
  잡고, 수리를 슬라이스 밖으로 흘려보내지 않게 목록화한다.
- **L2'-1의 파급.** imgui 전이 절단은 98+개 파일의 재컴파일과 미지의
  컴파일 오류를 낳는다 — 유니티 빌드가 가리던 자급자족 결손이 함께 드러날
  것이다(과거 같은 유형의 함정 이력 있음). CI Debug 레그(유니티 끔)를
  판정 기준으로.
- **MetaGenerator 스캔 경로.** 헤더가 이동하는 슬라이스(L2'-2·4, L5')마다
  리플렉션 코드젠의 스캔 전제를 먼저 확인(구 계획의 확인 결과: 리포 전체
  재귀 스캔이라 이동에 안전 — 단 L5'의 최상위 재편은 재검증).
- **FMOD.** B5의 공급 결정이 늦으면 CI 레그만 늦어진다 — 트랙의 다른
  슬라이스는 영향 없음(산성 테스트 v1은 개발 머신 기준).
- **pak 언팩의 %TEMP% 의존.** 스모크가 CI 러너에서 돌 때 temp 권한·잔존물
  간섭 가능 — Verify 단계에 전용 temp 루트 지정을 포함.

---

## 6. 완료 기준 (수치)

1. 산성 테스트 v1 통과 — 에디터 0회 기동 체크아웃에서 패키지가 첫 씬 렌더 +
   C# Awake 로그 + 정상 종료 (트랙 B 완료 시)
2. 산성 테스트 v2 통과 — CI가 게임 패키지를 아티팩트로 산출 (B5)
3. vcxproj 9종에서 구성 공통 속성 중복 0 — PlatformToolset 52회 → 9회(프로젝트
   존재 선언만), OutDir 재정의 14줄 → 0줄 (B1)
4. 디버거 아래서 뜨는 플레이어 — GameDebug 구성 실링크 (B1)
5. BUILD_FLAG 여는 지시문 48 → 0 (L2' — 가드가 필요하던 코드가 물리적으로
   에디터 층에 있으므로)
6. DYNAMICCPP_EXPORTS 187 → 0 (D)
7. 플레이어 산출물에서 ImGui 심볼 0 · GameBuild.sln에서 ImGuiHelper 참조
   제거 (L2' + L4' 합산 — GameMain 자신의 ImGuiRenderer 소유가 L4'에서
   풀린다)
8. 부트 시퀀스 코드 한 벌 — GameMain/Dx11Main 중복 243줄 → 공용 부트 (L4')
9. `TRAIN_ASIS`·`..\..\Dynamic_CPP` 하드코딩 0 — 프로젝트 파일이 진실 (L3')

---

## 7. 문서 관계

- **EngineLayerSeparationPlan.md** — L0·L1은 그 문서의 기록대로 완료.
  **L2~L4는 이 문서의 L2'~L5'가 승계**하며, 원문과의 차이는 둘이다:
  ① 빌드 파이프라인(구 L3-4의 한 항목이던 것)을 선행 트랙 B로 승격 —
  심판 먼저. ② L2에 imgui 전이 절단(L2'-1)을 최우선 소슬라이스로 추가 —
  구 계획은 이 경로(ReflectionFunction.h)를 모르고 있었다.
- **EnginePackagingPlan.md** — 이미 L1의 설계 근거로 흡수된 상태. 잔여
  몫의 처리처는 셋으로 갈라져 있다: P5의 RenderEngine 몫 →
  RhiBoundaryPlan R축, §4.1 지형 몫 → 대시보드 PHASE 11(전부 todo),
  P4 → 대시보드 4-2. 이 문서가 새로 가져갈 것 없음.
- **RhiBoundaryPlan.md** — 병행 트랙. §5의 핫 존 목록이 그 문서의 잔여
  슬라이스(R4 그래프 서명 · R5 구 RHI 은퇴)에서 나온다. T축은 T6까지 전부
  완료다(그 문서의 §4.3 본문이 뒤쪽 정정 노트보다 낡아 "남은 곳" 표를 아직
  들고 있으니 주의 — 진실은 :1004 정정 노트 쪽).
- **대시보드** — PHASE 12로 등재(12-0 ~ 12-10).
