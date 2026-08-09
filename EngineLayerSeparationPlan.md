# 3계층 분리 — 코어 엔진 · 에디터 · 게임 프로젝트

작성: 2026-08-08 · 계기: "코어 엔진, 에디터, 게임 개발 프로젝트의 3개 레이어로 분리"
선행 문서: `EnginePackagingPlan.md`(코어 계층화 P1~P5) — 이 문서는 그것을 포함하는 상위 계획이다.
후속 문서: **`BuildPipelinePlan.md`(2026-08-09)가 L2~L4를 L2'~L5'로 승계한다** —
이후의 진행·정정은 그쪽에 기록한다. 이 문서의 L2~L4 원문은 승계 근거로 유지.
승계에서 달라진 것 둘: ① 빌드 파이프라인(구 L3-4의 한 항목)이 선행 트랙 B로
승격됐다(심판 먼저 — 실측 결과 게임 빌드가 실행 가능한 게임을 만든 적이 없었다).
② L2에 imgui 전이 절단(ReflectionFunction.h → Core.Minimal.h 경로)이 최우선
소슬라이스로 추가됐다 — 이 문서는 그 경로를 모르고 있었다.

---

## 0. 전제 — 지금 워킹트리 상태

- EffectSystem 제거가 **미커밋 진행 중**(솔루션 제외 + 파일 삭제 119건). 이 계획은
  EffectSystem이 없는 상태를 전제한다. 커밋 전이라면 그 작업을 먼저 마무리한다.
- PHASE 4-2(RenderEngine→ScriptBinder 절단)가 진행 중이고 CI 래칫 게이트
  (`scripts/check_include_boundary.py`)가 이미 가동 중이다. 이 계획은 그 게이트를
  확장해서 쓴다 — 새 인프라를 만들지 않는다.

## 1. 실측 — 세 층은 지금 어디에 있나

| 목표 층 | 현재 실체 | 상태 |
|---|---|---|
| **코어 엔진** | Utility_Framework · SingletonManager · ManagedHeap · RenderEngine · Physics · ScriptBinder · ScriptCore(C#) | 프로젝트 경계는 있으나 **역방향 간선 56**으로 층이 아님 |
| **에디터** | Academy_4Q(exe) = EngineEntry + EngineGUIWindow, ImGuiHelper | exe는 분리돼 있으나 **에디터 코드가 엔진 라이브러리 안에 16파일** 섞임 |
| **게임 프로젝트** | Dynamic_CPP(에셋·설정) + GameScripts(C#) + TrainAsis(런타임 exe) | 콘텐츠는 모였으나 **경로 하드코딩·프로젝트 개념 부재** |

### 1.1 에디터가 엔진에 섞인 지점 (조사 실측)

- **EDITOR 매크로는 죽어 있다.** `ImGuiHelper/ImGuiRegister.h`가 파일 안에서
  `#define EDITOR`를 무조건 선언 → `Dx11Main.cpp`의 `#ifdef EDITOR` 4곳은 빌드
  구성과 무관하게 **항상 참**. Release(GAME) 빌드도 에디터 분기가 켜진다.
- **ImGui가 엔진 라이브러리에 직접 들어간 파일 15**(EffectSystem 제외 후):
  - RenderEngine 12: `Camera.cpp`, `DataSystem.{h,cpp}`, `EditorImGuiTexture.h`,
    `GizmoRenderer.cpp`, `ImGuiRenderer.cpp`, `RenderDebugManager.cpp`,
    `RenderScene.cpp`, `ShaderSystem.cpp`, `RHI/DX12/DX12DeviceResources.h`,
    `RHI/DX12/EnhancedSceneRenderer.{h,cpp}`, `RHI/DX12/ImGuiDx12Shell.cpp`
  - ScriptBinder 1: `PrefabEditor.{h,cpp}` — 통째로 에디터 전용 클래스
- **EngineGUIWindow는 엔진 내부 헤더 60여 종을 직접 include** — 에디터 API 경계가
  없다. (이건 "고칠 대상"이 아니라 "방향만 단속할 대상"이다 — §3 정책 참고.)
- 게임 빌드(TrainAsis)는 에디터 UI를 **호출하지 않을 뿐**, 위 에디터 코드가
  전부 링크 대상에 포함되어 컴파일된다.

### 1.2 게임 프로젝트가 프로젝트가 아닌 지점 (조사 실측)

- `Utility_Framework/PathFinder.h:104~144` — 콘텐츠 루트가 실행 파일 기준
  `..\..\Dynamic_CPP\...` **상대경로 하드코딩**. "프로젝트를 연다"는 개념 자체가 없다.
- `EngineEntry/ProjectSetting.{h,cpp}` — `Initialize/Load/Save` 전부 빈 스텁.
- `GameScripts.csproj`·`ScriptCore.csproj`는 **어느 솔루션에도 없다** — dotnet
  빌드를 따로 돌려야 하고, GameBuilderSystem(패키징)도 C# dll을 다루지 않는다.
- `Dynamic_CPP/Assets/Script/`에 레거시 C++ 스크립트 흔적 잔존(9-4 은퇴의 잔재).

## 2. 목표 구조

```
[게임 프로젝트]  Dynamic_CPP/  ─ 프로젝트 파일(.cproj) + Assets + ProjectSetting + GameScripts(C#)
                      ▲ 데이터로만 소비 (엔진·에디터는 프로젝트를 "연다")
[에디터]        Academy_4Q(exe) = EngineEntry(셸) + EngineGUIWindow + ImGuiHelper
                      + 엔진에서 적출한 에디터 코드(PrefabEditor·DataSystem UI·디버그 UI)
                      ▲ 에디터 → 엔진 단방향
[코어 엔진]     ScriptBinder ─ RenderEngine ─ Physics ─ Utility_Framework
                      ─ SingletonManager · ManagedHeap · ScriptCore(C# API)
                      + Player(exe) ← TrainAsis의 GameMain을 엔진 소속 런타임으로 승격
```

규칙 셋:
1. **화살표는 아래로만.** 엔진은 에디터를 모르고, 에디터·엔진은 특정 게임
   프로젝트를 모른다(경로 하드코딩 금지).
2. **에디터는 특권층.** 에디터가 엔진 내부 헤더를 보는 것은 허용한다(60종
   include를 API로 감싸는 것은 비용 대비 무익). 단속하는 것은 역방향뿐이다.
3. **게임 프로젝트는 코드가 아니라 데이터.** 네이티브 프로젝트를 갖지 않는다 —
   C# 어셈블리(GameScripts.dll)와 에셋·설정 파일이 전부다.

## 3. 실행 계획 — 5단계

각 슬라이스는 독립 커밋. 판정은 항상 ① CreatorEngine.sln + GameBuild.sln 전체
빌드 그린 ② `check_include_boundary.py` 통과 ③ `Tools/regression/run-all.ps1`.

### L0 — 게이트 확장 ✅ 2026-08-08 완료

`check_include_boundary.py`를 층 행렬 전체로 확장했다. 각 프로젝트에 층
번호(§2), 상향 include = 위반, 래칫 allowlist. 확장 시점 실측 **40간선**:

| 쌍 | 간선 |
|---|---:|
| RenderEngine → ScriptBinder | 23 |
| Utility_Framework → ScriptBinder | 5 |
| RenderEngine → EngineEntry | 4 |
| ScriptBinder → EngineEntry | 3 |
| Utility_Framework → RenderEngine · EngineEntry | 2+2 |
| Utility_Framework → ImGuiHelper | 1 |

패키징 계획의 추정 56에서 줄어든 것은 EffectSystem 은퇴 + EngineBootstrap이
이미 EngineEntry에 있음 + 모호 basename의 보수적 판정(놓침은 안전 측) 때문.
EngineGUIWindow·EngineEntry·TrainAsis는 최상층이라 자동으로 검사 비대상
(에디터 특권층 정책).

### L1 — 코어 정화 (기존 계획 승계) — P4 제외 완료 (2026-08-08)

`EnginePackagingPlan.md` P1~P4 중:
- P1 부트스트랩 상향: ✅ 이미 EngineEntry에 있었다(문서 작성 시점과 실측의 차).
- P2 설정 하향: ✅ RenderPassSettings→데이터 경계, EngineSetting.{h,cpp}→코어,
  EngineVersion.h 생성도 코어로. ProgressWindow는 코어 게시 지점
  (`Utility_Framework/ProgressSink.h`)으로 역전, MSBuildHelper는 EngineEntry로
  상향. **실행 파일 층(EngineEntry)으로 올라가는 간선 0.**
- P3 에디터 전용 상향: ✅ ReflectionImGuiHelper→EngineGUIWindow,
  GlobalImGuiContext→ImGuiHelper, IObject→코어(하향), DeviceResources의
  DeviceState 대입→게시 콜백 역전. (DataSystem 에디터 UI 분리는 L2-3 몫으로 잔존)
- P4 렌더→게임플레이 절단: ⬜ 잔여 23간선 — PHASE 4-2 트랙에서 계속
  (C5 Model·ModelLoader 8은 콜백 역전 설계 필요).

부수 발견: Interfaces 의사층 렌즈가 드러낸 **데이터 경계 누수 12간선**
(패스 설정 헤더 7종이 RenderEngine 본체에 잔존, FoliageType→Model) — 래칫에
동결, 후속 슬라이스로.

### L2 — 에디터 적출 (엔진 라이브러리에서 에디터 코드 빼기)

> ★ 승계(2026-08-09): `BuildPipelinePlan.md` §3 L2'가 이 절을 승계한다 —
> 항목 1(EDITOR 매크로)→L2'-7, 항목 2(PrefabEditor)→L2'-5, 항목 3(12파일
> 3분류)→L2'-3·4·6(RenderDebugManager.cpp는 D4에서 별도 소멸), 항목 4(판정)→
> L2' 판정으로 강화 승계(플레이어 ImGui 심볼 0은 L4'와의 합산 판정으로 정정).

L1의 P3와 이어지는 작업. 슬라이스 순서:

1. **EDITOR 매크로 소생** — `ImGuiRegister.h`의 자가 `#define EDITOR` 제거,
   Academy_4Q(Debug·Release 모두)의 PreprocessorDefinitions에 `EDITOR` 정의,
   GameBuild 구성에는 미정의. 이것만으로 `Dx11Main.cpp`의 기존 분기가 처음으로
   실제 작동한다. ★ 이 슬라이스는 **동작 변화가 생길 수 있는 유일한 지점**
  (Release 빌드에서 에디터 창이 사라짐) — 의도된 변화인지 눈으로 확인.
2. **PrefabEditor 이주** — `ScriptBinder/PrefabEditor.{h,cpp}` → EngineGUIWindow.
   소비자는 에디터 창들뿐이므로 이동 + include 경로 수정으로 끝난다.
3. **RenderEngine의 ImGui 12파일 분류** — 세 부류로 나눠 처리:
   - *에디터 전용 클래스*(GizmoRenderer, EditorImGuiTexture, ImGuiRenderer,
     RenderDebugManager, ImGuiDx12Shell): EngineGUIWindow 또는 신설
     `EditorRuntime` 정적 라이브러리로 이동.
   - *엔진 클래스에 섞인 UI 조각*(Camera.cpp, ShaderSystem.cpp, RenderScene.cpp,
     DataSystem.cpp의 ImGui 호출): 그리기 코드를 에디터 측 헬퍼로 옮기고 엔진에는
     데이터 접근만 남긴다(기존 `ImGuiDrawHelper*` 패턴 그대로).
   - *디버그 오버레이*(EnhancedSceneRenderer, DX12DeviceResources): 렌더러
     내부 통계를 구조체로 노출하고 그리는 쪽을 에디터로. 당장 어려우면
     `#ifdef EDITOR`로 격리만 해두고 후속 슬라이스로.
4. **판정** — GameBuild 구성 빌드 산출물에서 ImGui 심볼이 (ImGuiHelper 링크
   제외 후) 사라지는지 확인. GameBuild.sln에서 ImGuiHelper 참조 제거가 최종 목표.

### L3 — 게임 프로젝트화 (경로 하드코딩 → 프로젝트 개념)

> ★ 승계(2026-08-09): `BuildPipelinePlan.md` §3 L3'가 1:1 승계. 단 항목 4(빌드
> 파이프라인 봉합)는 트랙 B(B2~B4)로 승격 분리됐다.

1. **프로젝트 파일 도입** — `Dynamic_CPP/Project.cproj`(yaml/json) 신설:
   프로젝트 이름, 에셋 루트, 시작 씬, 스크립트 어셈블리 목록. `ProjectSetting`
   스텁을 이 파일의 Load/Save로 구현한다.
2. **PathFinder 역전** — `InternalPath::Initialize()`의 `..\..\Dynamic_CPP`
   하드코딩을 "프로젝트 파일 위치 기준"으로 바꾼다. 진입은 ① 커맨드라인 인자
   ② 없으면 마지막 프로젝트(사용자 설정 파일) ③ 없으면 현재 Dynamic_CPP 폴백.
   기존 동작이 폴백으로 보존되므로 무해한 단계다.
3. **GameScripts를 프로젝트 소속으로** — `GameScripts/` → `Dynamic_CPP/Scripts/`
   이동, csproj의 출력 경로는 유지(`Managed\Scripts\`). ClrHost 로드 경로는
   이미 baseDir 기준이라 변경 없음. 레거시 `Dynamic_CPP/Assets/Script/`(C++)는
   이 슬라이스에서 삭제.
4. **빌드 파이프라인 봉합** — GameBuilderSystem이 ① GameBuild.sln(네이티브)
   ② `dotnet build`(GameScripts) ③ pak 패키징 + **Managed dll 복사**까지
   한 버튼으로 수행하게. (현재 C# 빌드·dll 배치가 파이프라인 밖에 있다.)
5. **판정** — Dynamic_CPP 폴더를 통째로 다른 경로에 복사한 뒤 인자로 열어서
   에디터·플레이어가 동일하게 동작하면 "프로젝트"가 된 것이다.

### L4 — 런타임 플레이어 정식화 + 저장소 재편 (마지막)

> ★ 승계(2026-08-09): Player 승격은 `BuildPipelinePlan.md` L4'가, 폴더 재편은
> L5'로 분리 승계. 주의 — 아래 "에디터 무관 경량 진입점"이라는 서술은 낡았다:
> 실측 결과 GameMain은 에디터 메인의 76% 클론이며 DX11 은퇴로 지금은 컴파일도
> 되지 않는다(BuildPipelinePlan §1.4). 소생(B0)이 L4'보다 먼저다.

1. **Player 승격** — TrainAsis의 `GameMain.cpp`(에디터 무관 경량 진입점)를
   `Player`(가칭) 프로젝트로 엔진 소속화. TrainAsis는 "TRAIN_ASIS라는 게임의
   빌드 산출"이었으므로, 게임 이름이 프로젝트 파일에서 오도록 일반화한다
   (pak 이름 `TRAIN_ASIS.pak` 하드코딩 포함).
2. **폴더 재편**(선택, 전부 그린 뒤에만) — `git mv`로:
   ```
   Engine/   ← Utility_Framework, RenderEngine, Physics, ScriptBinder,
               SingletonManager, ManagedHeap, ScriptCore, Player
   Editor/   ← EngineEntry, EngineGUIWindow, ImGuiHelper
   Projects/ ← Dynamic_CPP (→ 이름도 프로젝트답게 개명 고려)
   ```
   vcxproj 상대경로·`$(SolutionDir)` 참조·PathFinder·CI 스크립트가 전부
   걸리는 **고위험 일괄 변경**이므로 독립 커밋 + 하루를 통째로 배정한다.
   L0~L3의 가치는 폴더 이동 없이도 전부 성립하므로, 이 단계는 미뤄도 된다.

## 4. 순서와 규모

| 단계 | 내용 | 규모(슬라이스) | 선행 |
|---|---|---|---|
| L0 | 경계 게이트 층 행렬 확장 | 1 | — |
| L1 | 코어 정화 (P1~P4) | 6~10 | L0 |
| L2 | 에디터 적출 | 4~6 | L1의 P3 |
| L3 | 게임 프로젝트화 | 5 | 없음(병행 가능) |
| L4 | 플레이어 정식화 + 폴더 재편 | 2~3 | L1~L3 |

L3은 L1·L2와 독립이라 **병행 가능**하다. L4의 폴더 재편만 전부의 뒤에 선다.

## 5. 완료 기준

1. 층 행렬 역방향 간선 **0** (L0 게이트가 CI에서 상시 검증).
2. GameBuild 구성이 ImGuiHelper·EngineGUIWindow 없이 링크된다.
3. `EDITOR` 미정의 빌드에 에디터 코드가 컴파일되지 않는다.
4. 게임 프로젝트 폴더를 임의 경로로 옮겨도 에디터가 열고 플레이어가 돈다.
5. 에디터의 "게임 빌드" 버튼 하나로 네이티브 + C# + 에셋 패키징이 끝난다.

## 6. 이 코드베이스 특유의 함정 — 2026-08-08 제거 완료

착수 전에 함정 자체를 제거했다. 남은 것은 DX12 플레이크 하나뿐이다.

- ~~유니티 빌드 전이 include~~ **제거**: 비유니티 빌드(`/p:EnableUnitySupport=false`)를
  그린으로 만들어 모든 TU가 include 자급자족. CI Debug 레그가 비유니티로 돌아
  재발을 막는다(유니티 모드는 Release 레그·로컬 빌드가 검증).
- ~~Scene.h 순환~~ **제거**: GameObject.inl의 Scene 접근을 비템플릿
  `SceneObjectAt()`로 우회해 inl→Scene.h 간선을 끊고, Scene.h가 GameObject.h를
  정식 include. `ScriptBinder/HeaderSelfSufficiency.cpp` 프로브(유니티 블롭
  제외)가 재발을 상시 감시한다. "GameObject.h를 먼저 include" 규칙 소멸.
- ~~단독 vcxproj 빌드 불가~~ **제거**: Directory.Build.props가 `$(SolutionDir)`
  미정의 시 리포 루트로 폴백. 단독 프로젝트 빌드 가능.
- DX12TextureCache::UploadFromDX11 회귀 간헐 플레이크는 기지(旣知) — 이 계획의
  실패 판정에 쓰지 않는다. (별도 추적)
