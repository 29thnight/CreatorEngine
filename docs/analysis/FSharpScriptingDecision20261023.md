> 후속 구현: [F# 단독 저작·OnTickSimulate 계약](../design/FSharpScriptingContract.md)으로 갱신됐다. 아래는 구현 전 판단 기록이다.

# F# 스크립트 전환 판단 — 2026-10-23

이 문서는 판단 기록이다. 구현은 하지 않았고, fsc 빌드·로드·리로드도 실측하지 않았다. 계획 문서([스크립트 계획](../plans/ScriptSurfacePlan.md)·[PrSM 계약 계획](../plans/SimulationEffectContractPlan.md))는 이 문서로 바꾸지 않았다. 반영은 구현 착수 때 따로 한다.

## 전제

- 게임 스크립트의 저작 언어를 C#에서 F#으로 **교체**한다. 엔진 측 관리 API인 ScriptCore는 C#으로 유지한다.
- 갱신 계약은 2026-10-22에 수용한 C++ 소유 `OnUpdate(float dt)` 고정 틱이다([수용 기록](CSharpFixedUpdateAcceptance20261022.md)).
- PrSM 도입은 미래 계획에서 한참 뒤로 미룬다. 그동안 효과·중단 의미를 대신할 저작 계층을 만들지 않는다.

## 1. OnUpdate 기준 판단 결과

1. OnSimulate·Scope·Delay를 되살리지 않는다. F# 스크립트도 `override _.OnUpdate(dt)`로 갱신한다.
2. C++ 계약, 관리 진입점(`Bootstrap.OnUpdate`), N3 실행 순서(`명령 적용 → OnUpdate → 물리 Step → 접촉 → snapshot`), 생명주기 6단계 enum, 현재 script API 버전은 바뀌지 않는다. 새 native 진입점도 필요 없다.
3. 여러 틱에 걸친 흐름은 OnUpdate 안의 명시적 상태로 쓴다. 판별 공용체 + 패턴 매칭, 또는 시간 누산 필드가 그 형태다. 이것이 F#으로 바꿔서 얻는 실질 이점이다.
4. 엔진 시간으로 흐르는 F# 전용 대기 CE(`simulate {}` 등)를 만들지 않는다. 10-22 결정("새 이름의 동등한 프레임워크로 대체하지 않는다")을 언어만 바꿔 우회하지 않기 위해서다.
5. 일반 `task {}`는 지금 C# 규칙과 같게 허용한다. 게임 스레드 SynchronizationContext로 재개되지만 엔진 시간·pause·결정성은 보장하지 않는다.
6. 진행 상태가 필드·판별 공용체 값으로 남는다. 그래서 직렬화, snapshot, 앞으로의 재시뮬레이션 경로에서 제외되지 않는다. 코루틴 중간 상태를 직렬화할 수 없는 OnSimulate의 구조적 한계가 생기지 않는다.
7. 실패 격리(인스턴스 비활성화), 재진입 차단, 게임 스레드 검사, 리로드 세대 중단은 ScriptCore 배치에 있으므로 언어와 무관하게 그대로 적용된다.

## 2. PrSM 장기 보류의 의미

- [PrSM 계약 계획](../plans/SimulationEffectContractPlan.md)의 adapter·언어 계약은 장기 보류로 둔다. 효과·중단·트랜잭션 저작 계약이 없는 기간이 길어진다.
- F#의 기본 불변성·옵션·판별 공용체가 실수 일부를 줄여 주지만, 정적 효과 검사를 대신하지는 않는다. 그 공백을 F# 쪽 프레임워크로 메우지 않는다.
- 문서 정정이 필요한 곳: [ScriptSurfacePlan.md:17](../plans/ScriptSurfacePlan.md)의 "PrSM 계약 도입 전까지 OnUpdate는 고정 틱"은 사실상 장기 계약이 된다. [LifecycleRegistry.h:44](../../Engine/SceneRuntime/LifecycleRegistry.h)와 [ScriptLifecyclePhase.h:40](../../Engine/SceneRuntime/ScriptLifecyclePhase.h)의 주석도 같은 표현을 쓴다.

## 3. C# 대신 F#일 때의 제한사항

### 3.1 컴파일과 코드 생성

| 제한 | 근거 | 영향 |
|---|---|---|
| Roslyn 소스 생성기를 실행할 수 없다 | [ScriptGenerator.cs](../../ScriptCore.Generators/ScriptGenerator.cs)가 등록 표(`CreatorEngine.Generated.ScriptRegistry`), `[SerializeField]` 인덱스 접근자, 이름 디스패치 switch를 만든다 | 세 가지를 모두 대체해야 한다. 후보: F# 코드 생성(Myriad 등), 빌드 후 IL 처리, 손 작성. 리플렉션 대체는 생성기가 전제한 AOT 방향과 충돌한다 |
| partial class가 없다 | 생성 코드가 사용자 클래스에 덧붙는 방식 | 접근자를 override 멤버로 생성하거나, 별도 형식으로 분리해야 한다 |
| 파일 순서가 의미를 가진다 | [GameCompiler.cs:70](../../BuildTool/GameCompiler.cs)이 `*.cs`를 이름순으로 모은다 | 의존 순서 목록(프로젝트 파일 등)과 에디터에서 순서를 편집하는 방법이 필요하다 |
| 생명주기 이름 표를 따로 유지해야 한다 | 생성기 135-142행이 엔진 직접 호출 훅을 이름 디스패치에서 뺀다 | 대체 생성 경로에도 같은 표가 있어야 훅이 두 번 불리지 않는다 |
| 컴파일러 배포가 바뀐다 | 배포 SDK에는 `Scripting/Compiler/csc.dll`만 있다 | fsc와 FSharp.Core를 함께 배포하고 버전을 엔진 빌드에 고정한다. deterministic·pathmap 인자도 옮긴다. 리로드 대기 시간이 늘 수 있다(미측정) |

### 3.2 로딩과 핫리로드

- **FSharp.Core 해석:** `ScriptLoadContext.Load`는 ScriptCore가 아니면 기본 컨텍스트로 넘긴다([ScriptAssemblyLoader.cs:24-35](../../ScriptCore/ScriptAssemblyLoader.cs)). 생성하는 deps.json에는 GameScripts만 있다. 지금 상태로는 FSharp.Core를 찾지 못한다.
- **배치 결정:** FSharp.Core를 ScriptCore처럼 기본 컨텍스트에 상주시키는 편이 타입 중복을 막는다. 대신 FSharp.Core 내부 캐시(printf 포맷, FSharp.Reflection)가 언로드 대상 스크립트 타입을 붙들 위험이 생긴다.
- **회수 방해 요인:** 모듈 수준 `let` 정적 값, 정적 초기화, 클로저, 이벤트 구독, MailboxProcessor는 ALC 회수를 막을 수 있다. `script.reload` 후 이전 ALC 회수 수용을 F# 어셈블리로 다시 해야 한다.

### 3.3 ScriptCore API 사용성

ScriptCore 공개 API는 C# 관용으로 설계되어 있다. 대부분 F#에서 쓸 수 있지만 다음은 제약이 있다.

- **Span 반환·인자:** `ContactStream.Read()`, `Physics.QueryBatch`, `Simulation.Submit/ReadSnapshot`가 `Span`/`ReadOnlySpan`을 쓴다. F#에서 byref-like 값은 클로저·람다·`seq {}`·`list` 식·`task {}` 안에서 쓸 수 없고 캡처도 안 된다. 접촉 처리는 OnUpdate 본문 안의 직접 루프로 써야 한다. 동기 OnUpdate 모델과는 맞지만 쓸 수 있는 문체가 좁아진다.
- **null 반환:** `GetComponent<T>()` 등은 C# nullable 주석으로 `T?`를 돌려준다. F# nullness 검사는 opt-in이라, 켜지 않으면 null이 검사 없이 들어온다. Option으로 감싸는 얇은 F# 보조 함수가 필요하다. F# 컴포넌트 타입에 null을 비교하려면 `[<AllowNullLiteral>]`이 필요하다.
- **out·in·기본값 인자:** `TryGetComponent`, `TryParse`, `ReadState` 등의 out은 튜플 반환으로, `in`과 기본값 인자도 쓸 수 있다. 제약은 아니지만 예제를 다시 써야 한다.
- **할당:** 60Hz OnUpdate에서 F# 클로저, 참조 튜플, `list`, `seq`는 할당을 만든다. ContactStream이 지킨 "반복 구간 할당 0바이트"는 엔진 쪽 계약일 뿐이고, F# 저작 코드는 보장하지 않는다. 구조체 튜플, `ValueOption`, 배열을 쓰라는 저작 지침이 필요하다.
- **비동기:** `task {}`는 C#과 같게 컨텍스트를 캡처한다. `async {}`는 시작·컨텍스트·취소 전파 방식이 달라 게임 스레드 규칙과 어긋나기 쉬우므로 금지하거나 비권장한다.

### 3.4 직렬화와 인스펙터

- `[SerializeField]`는 `val mutable` + `[<DefaultValue>]` 필드에만 안정적으로 붙는다. `let mutable`은 컴파일 결과가 필드로 남는다는 보장이 없다.
- 직렬화 형식은 `FieldType`의 Float, Int32, Bool, Float2, Float3, String, Object, AssetLink다. F# 레코드·판별 공용체·Option 필드는 인스펙터에 노출되지도, 저장되지도 않는다. 1절 3항의 상태 표현은 런타임 상태로만 쓰고, 저작 값은 지원 형식으로 둔다. 형식을 넓힐지는 별도 결정이다.
- 인스펙터 이름 다듬기 규칙(`_moveSpeed` → Move Speed)은 C# 명명을 전제로 한다. F# 명명과 맞추는 규칙을 확인해야 한다.

### 3.5 에디터, 도구, 기존 자산

- `.cs`를 전제로 한 곳: 에셋 DB 감시([EditorAssetDatabase.cpp:3995](../../Editor/EngineEntry/EditorAssetDatabase.cpp)), `FileType::CSharpScript`, 스크립트 생성 템플릿([EditorScriptAuthoring.cpp:246](../../Editor/EngineEntry/EditorScriptAuthoring.cpp)), 패키지 입력([PackageInputs.cs:13](../../BuildTool/PackageInputs.cs)), 컴파일러 호출([GameCompiler.cs](../../BuildTool/GameCompiler.cs)), 엔진 배포(`EnginePublisher.cs`).
- **회귀 프로브:** `GameScripts/`에 C# 파일 74개가 있고, 생명주기·접촉·오디오 게이트가 이를 쓴다. 이것을 F#으로 옮기지 않으면 "엔진 검증용 C# + 사용자 F#" 두 컴파일 경로를 계속 유지해야 한다. 한 어셈블리에 두 언어를 섞을 수 없으므로 둘 중 하나를 정한다.
- **사용자 자산 이주:** `Dynamic_CPP/Assets/Script/Phase22ProductAudioProbe.cs`와 오디오 package 생성 template을 옮겨야 한다.
- 문서, API reference 예제, 게이트 이름(`verify-csharp-fixed-update.ps1` 등)이 C#을 전제로 한다.
- 디버깅은 portable pdb로 가능하다. 다만 CE 상태 머신 단계 실행과 에디터의 "스크립트 열기" 연결 대상 IDE는 다시 정해야 한다.

### 3.6 Native AOT (계획 단계)

AOT는 아직 켜져 있지 않다([ScriptAssemblyLoader.cs:123](../../ScriptCore/ScriptAssemblyLoader.cs)). 켜게 되면 FSharp.Core의 리플렉션 기반 기능(`%A` 서식, 일부 구조적 비교·출력)에서 트리밍 경고와 동작 차이가 생긴다. 저작 지침에서 이 기능들을 제한해야 한다.

## 4. 구현 전에 정할 것

1. 소스 생성기 대체 방식
2. FSharp.Core를 둘 컨텍스트와 버전 고정
3. 회귀 프로브의 언어(F#으로 이주할지, C# 검증 경로를 유지할지)
4. 직렬화 형식 확장 여부
5. `task {}` 허용 범위와 `async {}` 금지 여부
6. nullness 검사 opt-in 여부
7. 파일 순서 관리 방식

## 확인하지 않은 것

fsc 빌드, FSharp.Core 로드, 핫리로드 회수, 컴파일 시간, OnUpdate 할당량은 측정하지 않았다. 위 제한은 현재 코드와 F# 언어 규칙을 대조해 판단한 것이다.
