# Editor Automation CLI 서브시스템 정식화 계획 (PHASE 6 · CLI 트랙)

작성: 2026-08-28.

상태: **현재 소스 감사 기반 실행 계획. 구현·빌드·런타임 검증 전이다.**

선행: PHASE 0의 `0-8`(`--exec`/`--script`/`--console`, command queue, frame-boundary
`Pump`) 완료. 이 계획은 그 구현을 폐기하지 않고 정식 command subsystem으로 승격한다.

관련: `BuildPipelinePlan.md`(build/package 결과와 process exit) ·
`EngineLayerSeparationPlan.md`(Editor operation·Play mode 경계) ·
`ProfilingCapturePlan.md`/`RhiBoundaryPlan.md`(CLI를 소비하는 검증 하네스) ·
`EngineDistributionAndLauncherPlan.md`(`CreatorEditor.exe --project` Host 계약).

> **MCP는 보류한다.** 이 계획에는 MCP server, `tools/list`, tool schema 변환,
> `structuredContent`, 원격 호출·권한 모델을 구현하는 슬라이스가 없다. CLI의 결과·schema·
> 실행 수명 계약이 모두 닫힌 뒤에도 필요성이 남을 때 별도 계획으로 다시 판정한다(§15).

---

## 0. 한 줄 결론

현재 `ConsoleCommandSystem`은 기능이 부족한 초기 CLI가 아니다. 별칭 포함 176개 명령 이름과
40개 자동화 소비 파일을 가진 **실사용 Editor automation/regression harness**다. 부족한 것은
명령 수가 아니라 다음 네 계약이다.

1. 실패가 반드시 `CommandResult`와 process exit code로 귀결되는가.
2. 핸들러가 raw 문자열을 재해석하지 않고 검증된 argument만 받는가.
3. 이름·help·argument·실행 조건의 정본이 하나인가.
4. 사람이 읽는 로그와 자동화가 읽는 결과가 분리되어 있는가.

따라서 새 명령을 계속 보태기 전에 **Result → Arguments → Descriptor → Module → Machine
Output** 순으로 세운다. dotted command ID와 frame-boundary `Pump()`는 유지한다.

완료 모습은 다음과 같다.

```text
--exec / --script / --console
              │
              ▼
        CLI frontend/parser
              │ CommandInvocation (owned values)
              ▼
        CommandRegistry
        descriptor + validation
              │
              ▼
        CommandExecutor
        session + affinity + queue
              │
       ┌──────┴────────┐
       ▼               ▼
 Editor operations   Test/diagnostic/build adapters
       │               │
       └──────┬────────┘
              ▼
        CommandResult
       ┌──────┴────────┐
       ▼               ▼
 human formatter    JSONL formatter
 console/log        automation harness
```

MCP나 Editor command palette는 이 그림의 frontend로 추가하지 않는다.

---

## 1. 범위와 비범위

### 1.1 이번 계획의 범위

- `--exec`, `--script`, `--console` 입력 호환 유지
- dotted canonical command ID와 alias 유지
- `CommandInvocation`, `CommandDescriptor`, `CommandResult`, session summary 도입
- 인자 lexical parse·scalar validation·command precondition 분리
- help/list/argument 문서를 descriptor에서 생성
- 실패의 process exit code 집계와 `--fail-fast` 정책
- 기본 human 출력과 opt-in JSONL 결과 모드
- 6,756줄 `ConsoleCommandSystem.cpp`의 도메인별 buildable 분리
- GUI와 CLI가 공유해야 할 Editor operation의 의미 동등성 감사
- 기존 PowerShell/command-file 소비자 40개의 점진적 machine-result 전환
- unattended Editor 실행의 모드·stdout/stderr·timeout·종료 수명 강화

### 1.2 이번 계획에서 제외

- **MCP server/tool 노출 전부**
- Editor command palette나 새 GUI 명령 창
- 동적 command plugin/DLL discovery
- `creator object parent ...` 같은 새 외부 문법과 dotted ID 전면 개명
- 별도 `creator.exe`/UAT형 host 신설
- 렌더 창을 만들지 않는 진짜 headless Editor
- 모든 명령을 worker thread에서 실행하는 전환
- 기존 domain service·selftest 알고리즘의 기능 재작성
- Player에 Editor CLI 링크

`CoreWindow::SetUnattended(true)`는 대화상자를 억제할 뿐 headless가 아니다. 현재 App은 CLI
초기화 전에 창을 표시한다. 이번 계획은 그 사실을 숨기지 않고 실행 프로필 이름도
`UnattendedEditor`로 고정한다. true headless가 필요해지면 Editor 초기화 의존을 별도 감사한다.

---

## 2. 현재 소스 기준선

### 2.1 입력·실행 수명

| 항목 | 현재 소스 | 판정 |
|---|---|---|
| 실행 인자 | `Editor/EngineEntry/ConsoleCommandSystem.cpp:536~611` | `--exec` 반복, `--script`, `--console`, `--heapcheck` |
| stdin | `ConsoleCommandSystem.cpp:614~638` | reader thread는 raw 한 줄을 queue에 넣기만 함 |
| script | `ConsoleCommandSystem.cpp:640~663` | 빈 줄/`#` 주석을 제외하고 raw 한 줄 queue |
| queue | `ConsoleCommandSystem.h:103~120` | mutex + deque, `wait`와 quit 상태 포함 |
| 실행 | `ConsoleCommandSystem.cpp:671~700` | GT에서 프레임당 한 명령, wait/scene loading 동안 보류 |
| parse | `ConsoleCommandSystem.cpp:6540~6558` | `Pump()`가 부른 `Execute()` 안, 즉 **현재는 GT에서 parse** |
| App 연결 | `Editor/EngineEntry/App.cpp:245~263` | 창 표시·초기화 뒤 CLI init, 매 프레임 `Pump()` |

헤더 주석은 “명령 파싱은 백그라운드 스레드”라고 적지만 실제 reader는 `Enqueue(line)`만
하고 `Split(line)`은 GT `Execute()`에서 호출된다. 구현 착수 시 주석을 실제 계약에 맞춰
고치고, lexical parse를 frontend로 옮길지는 CLI2의 소유값/registry thread-safety gate로
판정한다.

### 2.2 규모와 소비자

2026-08-28 현재 정적 집계:

| 항목 | 실측 |
|---|---:|
| `ConsoleCommandSystem.cpp` | 6,756줄 |
| registration call | 162 |
| 등록 이름(별칭 포함) | 176 |
| `Cmd_*` handler | 153 |
| `std::printf` call | 490 |
| Debug log call | 164 |
| 직접 `EngineBootstrap::SetExitCode` | 2 |
| `PrintHelp()`에 문자열로 나타나지 않는 등록 이름 | 51 |
| CLI를 호출/파싱하는 추적된 script·scenario 파일 | 40 |

명령군은 Editor 저작(`scene/object/component/prefab/script/ui/animator`), asset authoring,
build/package, memory/profile/lifecycle diagnostics, DX12/Vulkan/RHI selftest를 한 registry에
담는다. 명령 **표면**은 한 곳이지만 DX12/Vulkan 등의 실제 검증 알고리즘 상당수는 이미
별도 `Run*Test`에 있다. 분리 대상은 domain 알고리즘 재작성보다 command adapter·metadata·
result formatting의 과밀이다.

### 2.3 현재 하네스가 문자열 형상에 의존한다

대표적으로 `Tools/dx12-validation/Invoke-Dx12Suite.ps1`은 다음을 한다.

- 명령 목록을 registry API가 아니라 C++ 소스의 `"dx12.*"` 문자열 리터럴에서 추출
- 판정을 `^[CLI] <name> (통과|실패|완료)` 한국어 정규식으로 추출
- stderr byte와 process exit code를 별도 보조 신호로 사용

이 스크립트의 주석에는 help가 낡아 35개 중 26개만 실행한 사례, registry 구현 형식이
바뀌어 35개를 0개로 읽은 사례가 이미 기록돼 있다. CLI3·CLI5의 완료 기준은 이 source
scraping과 verdict regex를 제거하는 것이다.

---

## 3. 먼저 닫아야 할 정확성 결함

### 3.1 실패가 process success로 남을 수 있다

대부분의 selftest handler는 `bool passed`를 받은 뒤 `printf`/Debug log만 남긴다. session
결과 정본이 없고 직접 `SetExitCode`도 두 곳뿐이라, 명령이 “실패”를 출력한 뒤 `quit`하면
프로세스가 0으로 끝날 수 있다.

결정:

- 모든 명령은 정확히 하나의 terminal `CommandResult`를 만든다.
- session은 결과 severity를 누적하며 뒤의 성공/`quit`가 앞의 실패를 지우지 못한다.
- 기본은 뒤 명령을 계속 실행해 진단을 모으고, `--fail-fast`만 조기 중단한다.
- 직접 `EngineBootstrap::SetExitCode`는 최종 session adapter 한 곳만 허용한다.

### 3.2 tokenizer 결과를 핸들러가 다시 버린다

`Split()`은 큰따옴표 구간을 하나의 token으로 만든다. 그러나 `object.parent`,
`object.duplicate`, `prefab.create` 등은 `parts.back()`과 `line.substr()/rfind()`로 raw 원문을
다시 역산한다.

예:

```text
object.parent "Big Boss Character" "Main Characters"
```

`parts`는 올바른 세 token이지만 현 `object.parent`가 복원한 child 이름에는 따옴표가 남는다.

결정:

- handler에는 raw `line`을 주지 않는다.
- 공백 포함 문자열의 canonical 문법은 quote다.
- 기존의 “마지막 token을 둘째 이름, 앞의 raw remainder를 첫째 이름” 입력은 호환 adapter로
  한정하고 deprecation을 기록한다.
- Windows path의 `\`는 임의 escape로 소비하지 않는다. quote 안에서는 `\"`와 `\\`만
  명시적으로 처리하고 나머지 backslash는 보존한다.

### 3.3 같은 primitive가 같은 Editor operation을 뜻하지 않는다

- CLI `object.duplicate`와 GUI duplicate는 `Object::Instantiate`는 공유하지만 GUI 쪽에는
  Undo·선택·부모 보정이 더 있다.
- CLI `play`와 GUI Play 버튼은 `SetGameStart`는 공유하지만 UI 쪽은 `m_isGameMode`도 쓴다.
  `PlayModeController`가 실제 전환 때 Undo를 정리해도 이 별도 상태 차이는 남는다.

결정:

- “같은 low-level 함수”를 동등성 완료로 판정하지 않는다.
- mutating command마다 canonical Editor operation, 의도된 raw primitive, test-only probe 중
  하나로 분류한다.
- 의미를 바꾸기 전에 기존 회귀가 Undo·selection·이름·slot에 기대는지 characterization
  test를 세운다.
- 의도적으로 GUI 의미를 우회하는 명령은 descriptor에 그 사실을 표시하고 일반 Editor
  command처럼 이름 붙이지 않는다.

### 3.4 수동 help와 registry가 이미 갈라졌다

`PrintHelp()`는 하나의 긴 `printf` 문자열이고 registry는 별도 `unordered_map` 초기화다.
단순 문자열 대조만으로도 등록 이름 51개가 help에 없다.

결정:

- 이름·alias·summary·arguments·capability·help는 descriptor 한 곳만 정본이다.
- `help`, `help <command>`, category list, machine discovery는 모두 registry snapshot에서 만든다.
- command enumeration을 C++ source scraping으로 구현하지 않는다.

---

## 4. 고정 결정

| 결정 | 채택 | 이유 |
|---|---|---|
| canonical ID | `scene.load`, `object.parent` 등 dotted ID 유지 | 기존 scenario와 40개 소비자 호환 |
| 실행 queue | frame-boundary `Pump()` 유지 | Scene/Editor state의 GT 규약 보존 |
| 기본 출력 | human text 유지 | 개발자 console과 기존 로그 가독성 |
| machine 출력 | opt-in JSONL | 줄 단위 streaming·부분 결과·PowerShell 파싱 |
| 결과 정본 | `CommandResult` | printf/Debug/process exit 삼중 판정 제거 |
| schema 정본 | `CommandDescriptor` | help/list/validation 문서 drift 제거 |
| 기본 affinity | `GameThread` | 기존 handler의 thread-safety를 추측해 옮기지 않음 |
| 모듈화 | domain registration unit | command 한 개당 class/file 폭증 방지 |
| 실패 처리 | 기본 continue + aggregate, 선택 `--fail-fast` | 회귀 한 번에 여러 진단 확보 + CI 정확성 |
| headless | 비범위 | 현재 Editor 창·renderer/scene 의존과 다른 문제 |
| MCP | **보류** | local command contract가 아직 안정되지 않음 |

---

## 5. 목표 계약

### 5.1 Invocation

```cpp
enum class CommandSource : uint8_t
{
    ExecArgument,
    ScriptFile,
    InteractiveConsole,
};

struct CommandInvocation
{
    uint64_t                    sequence{};
    CommandSource               source{};
    std::string                 commandId;
    std::vector<CommandArgument> arguments;
    std::string                 sourceName;   // script path 또는 "--exec"/"stdin"
    uint32_t                    sourceLine{};
};
```

- queue를 건너는 값은 전부 owned value다.
- `string_view`, `Scene*`, `Entity*`, backend handle을 queue에 보관하지 않는다.
- raw line은 오류 위치를 표시하는 frontend diagnostics에만 남기고 handler에 넘기지 않는다.

### 5.2 Descriptor

```cpp
struct CommandDescriptor
{
    std::string_view                    name;
    std::span<const std::string_view>   aliases;
    std::string_view                    summary;
    CommandCategory                     category;
    std::span<const CommandArgumentDescriptor> arguments;
    CommandCapabilities                 capabilities;
    CommandExecutionAffinity            affinity;
    CommandHandler                      execute;
};
```

최소 capability:

- `RequiresEditor`
- `RequiresScene`
- `MutatesScene`
- `MutatesAssets`
- `UsesUndo`
- `RequiresRenderer`
- `RequiresDX12` / `RequiresVulkan`
- `UnattendedSafe`
- `MayBlock`
- `TestOnly`

capability는 권한 시스템이 아니다. 이번 계획에서는 실행 전 조건 검사, help 표시, 하네스
선별을 위한 정적 사실이다.

### 5.3 Arguments

초기 `ArgumentKind`는 실제 현재 명령 표면만 담는다.

- `String`, `Bool`, `Int64`, `Double`
- `Path`, `AssetPath`, `EntityName`
- `Enum`
- `Remainder`(명령당 마지막 인자 하나만)

Entity/asset의 실제 해석은 parser가 아니라 GT precondition 단계가 한다. parser thread가 live
Scene이나 DataSystem을 읽지 않는다.

descriptor는 `required/optional/default/repeated`와 간단한 범위·enum 목록을 가진다.
두 개의 공백 포함 이름을 quote 없이 나누는 모호한 문법은 schema로 합리화하지 않는다.

### 5.4 Result

```cpp
enum class CommandStatus : uint8_t
{
    Succeeded,
    InvalidArguments,
    PreconditionsFailed,
    Failed,
    Cancelled,
    InternalError,
    LegacyUnreported,   // 이행기 전용
};

struct CommandResult
{
    CommandStatus status{};
    std::string   code;       // 예: "scene.not_found", "test.pixel_mismatch"
    std::string   message;    // human summary
    CommandData   data;       // owned bool/int/double/string/array/object tree
};
```

`CommandData`에는 raw pointer, `Meta::Type*`, RHI native object를 넣지 않는다. 큰 바이너리/PNG/
dump는 inline base64가 아니라 artifact path와 digest를 결과에 넣는다.

### 5.5 Session과 exit code

stable process exit mapping:

| exit | 의미 |
|---:|---|
| 0 | 모든 실행 명령 성공 |
| 2 | CLI 문법·unknown command·argument 오류 |
| 3 | scene/backend/editor 상태 등 precondition 불충족 |
| 4 | 명령 또는 selftest 판정 실패 |
| 5 | build/IO/내부 infrastructure 오류 |

OS exception/crash code는 덮어쓰지 않는다. timeout은 process 밖 harness가 별도로 판정한다.
session은 숫자 최대값이 아니라 명시한 severity 순서로 가장 심한 결과를 보존한다.

---

## 6. Parser·validation 파이프라인

```text
raw input
  │ lexical parse (quote/escape/source location)
  ▼
tokens
  │ registry lookup + descriptor arity/type validation
  ▼
CommandInvocation (owned scalar values)
  │ queue
  ▼
GT precondition/resource resolution
  │
  ▼
handler
```

규칙:

1. 닫히지 않은 quote와 잘못된 escape는 실행 전에 `InvalidArguments`다.
2. 빈 quote `""`는 빈 string argument로 보존한다.
3. command ID/alias 정규화는 registry lookup 한 곳에서 한다.
4. 숫자 parse는 `atoi/atof`의 조용한 0 변환을 사용하지 않는다.
5. path normalization과 workspace 탈출 허용 여부는 argument kind가 아니라 각 command
   precondition이 결정한다.
6. script의 file/line을 결과에 남겨 어느 명령이 실패했는지 알 수 있게 한다.
7. `wait N`은 wall time이 아니라 현재처럼 정확히 frame count를 뜻한다.

호환 기간에는 command별 `LegacyArgumentPolicy`를 둘 수 있지만 registry 밖의 raw substring
복원은 금지한다. 호환 정책의 사용 횟수를 진단에 남겨 제거 시점을 측정한다.

---

## 7. Registry·help·discovery

필수 표면:

```text
help
help <command>
commands.list [category]
commands.describe <command>
```

- human mode는 사람이 읽는 표를 출력한다.
- JSONL mode에서는 같은 descriptor snapshot을 `data`에 담는다.
- alias는 canonical command와 한 descriptor를 공유한다.
- duplicate registration은 초기화 실패다. 로그만 남기고 계속하지 않는다.
- category는 이름 prefix에서 매번 추론하지 않고 descriptor에 명시하되 dotted ID와 일치
  여부를 selftest한다.
- registry snapshot 순서는 canonical name ordinal로 고정한다.

`Invoke-Dx12Suite.ps1`의 test discovery는 `commands.list` 결과 중 category/capability를 읽어
선별한다. 더는 C++ 리터럴이나 help 문자열을 파싱하지 않는다.

---

## 8. 출력 계약

### 8.1 Human mode

- 기본값이며 기존 `[CLI] ...` 출력 형상을 호환 기간 동안 유지한다.
- 상세 engine log는 기존 Debug/HTML sink에 남길 수 있다.
- terminal result는 formatter 한 곳에서 성공/실패 summary를 낸다.
- handler가 같은 terminal verdict를 다시 `printf`하지 않는다.

### 8.2 JSONL mode

실행 인자:

```text
--result-format jsonl
```

terminal record 예:

```json
{"schemaVersion":1,"sequence":7,"command":"component.list","status":"succeeded","code":"ok","message":"37 component types","data":{"components":["Camera","Light","MeshRenderer"]}}
```

session 마지막에는 `type:"session"` summary 한 줄을 낸다. JSONL mode의 stdout은 JSON object
한 줄만 허용하며 일반 로그는 stderr/HTML sink로 보낸다. 검증은 “JSON처럼 보인다”가 아니라
stdout 모든 줄 parse 성공과 terminal result sequence의 중복/누락 0을 단정한다.

schema v1 안에서는 field 삭제·의미 변경을 금지한다. field 추가는 consumer가 무시할 수 있는
형태로만 한다. v2가 필요하면 formatter와 consumer를 병행 지원한 뒤 전환한다.

### 8.3 대용량 출력

hierarchy dump, reflection golden, screenshot, profiler dump는 결과 본문에 통째로 넣지 않는다.

```json
{"artifact":{"path":"...","kind":"png","sha256":"...","bytes":12345}}
```

artifact root와 상대/절대 경로 정책은 각 기존 검증 도구의 output directory 계약을 유지한다.

---

## 9. Editor operation 동등성

CLI4에서 mutating command를 다음 표로 전수 분류한다.

| 분류 | 의미 | 예 |
|---|---|---|
| Shared Editor operation | GUI와 Undo/selection/transaction까지 같아야 함 | play/stop, 일반 duplicate, undo/redo |
| Shared engine service | GUI와 low-level 결과만 같으면 됨 | scene load/save, prefab save, script reload |
| Test/diagnostic probe | GUI 의미가 없고 관측/격리 검사용 | `dx12.*`, `vk.*`, `profile.*` |
| Raw fixture authoring | 회귀 fixture 생성용으로 Undo를 의도적으로 우회 | 일부 `*.authoring.probe` |

각 mutating command descriptor에는 이 분류가 드러나야 한다. 일반 명령과 raw fixture command가
같은 이름으로 우연히 다른 의미를 갖지 않게 한다.

완료 판정 예:

- GUI Play와 CLI `play`가 `IsGameStart`, edit/game Undo depth, selection을 같은 규약으로 전이
- GUI duplicate와 일반 CLI duplicate가 부모·선택·Undo를 동일하게 반영
- raw fixture command는 Undo 우회가 명시되고 일반 command 회귀와 분리
- UI/CLI 어느 쪽도 domain operation을 복사 구현하지 않음

---

## 10. 물리 파일 구조와 의존 방향

첫 착지 후보:

```text
Editor/EngineEntry/CommandSystem/
  CommandTypes.h
  CommandParser.h/.cpp
  CommandRegistry.h/.cpp
  CommandExecutor.h/.cpp
  CommandFormatters.h/.cpp

Editor/EngineEntry/Commands/
  CoreCommands.cpp
  SceneObjectCommands.cpp
  AssetAuthoringCommands.cpp
  ScriptUiAnimatorCommands.cpp
  DiagnosticsCommands.cpp
  RenderTestCommands.cpp
  BuildCommands.cpp

Editor/EngineEntry/ConsoleCommandSystem.h/.cpp
  -- 호환 facade: command-line/stdin/script 수명, Enqueue, Pump, Shutdown
```

규칙:

- 파일 분리는 기능 변경과 한 덩어리로 하지 않는다.
- domain 파일은 registration function과 얇은 adapter만 소유한다.
- 실제 renderer test, asset writer, build orchestrator를 command 폴더로 이동하지 않는다.
- include는 각 TU가 직접 소유한다. 기존 unity build의 전이 include에 기대지 않는다.
- domain 하나를 옮길 때마다 Editor unity/non-unity build를 둘 다 통과한다.
- Player project에는 CommandSystem/Commands 파일이 등록되지 않는다.

`ConsoleCommandSystem.cpp`를 한 번에 7개 파일로 잘라 빌드를 마지막에 보는 방식은 금지한다.

---

## 11. 기존 소비자 이행 정책

현재 최소 40개 추적 파일이 CLI 실행이나 출력 parse에 참여한다. 이들을 한 번에 바꾸지 않는다.

1. engine이 human과 JSONL을 동시에 지원한다.
2. CLI5에서 consumer를 domain별로 하나씩 JSONL로 옮긴다.
3. 같은 scenario를 두 mode로 실행해 logical result가 같음을 대조한다.
4. 옮긴 consumer는 text fallback을 유지하지 않는다. 이중 parser는 새 drift를 만든다.
5. 마지막 consumer가 옮겨진 뒤에만 legacy verdict line을 제거한다.

이행 순서:

1. `Tools/dx12-validation/Invoke-Dx12Suite.ps1` — source scraping과 verdict regex를 둘 다 제거
2. `Tools/profiling-validation`
3. `Tools/featuretest`
4. `Tools/regression`의 독립 verify 스크립트
5. `Tools/regression/run-all.ps1` 집계
6. 루트 `scripts/*.txt`와 문서 예제

command file 형식은 그대로 유지할 수 있다. 바뀌는 것은 결과를 읽는 방식이다.

---

## 12. 실행 슬라이스

총 추정: **18 인일**. 실제 command·consumer 수는 CLI0에서 다시 생성하고 공수를 갱신한다.

### CLI0 — 기준선·characterization·실패 canary (P0 · 1.5일)

- registry name/alias/handler/help coverage inventory를 artifact로 남긴다.
- raw `line`을 재해석하는 handler와 직접 `SetExitCode`/verdict print 지점을 센다.
- CLI 소비 파일·정규식·source scraping 목록을 고정한다.
- `--exec` 반복, script, stdin, wait, scene loading gate, missing script, shutdown을 characterise한다.
- unknown command와 논리 실패가 exit 0으로 남는 false-green canary를 만든다.
- quoted empty/string/path/name과 잘못 닫힌 quote parser golden을 만든다.

완료 기준: 현재 거동을 바꾸지 않고 baseline artifact 생성 · canary가 현 결함에 실제 발화 ·
command/consumer 개수가 0이 아닌 상세 목록으로 고정.

### CLI1 — `CommandResult`·session·exit spine (P0 · 2.5일)

- `CommandStatus`, stable code, owned data, session severity를 도입한다.
- unknown/parse/precondition/handler/internal failure를 exit mapping에 연결한다.
- `help/quit/wait`, `scene.load`, `game.pak`, 대표 selftest 하나를 result-bearing으로 이행한다.
- legacy void handler adapter는 `LegacyUnreported`를 반환하며 새 command에는 사용을 금지한다.
- 기본 continue와 `--fail-fast`를 검증한다.

완료 기준: 실패→`quit`가 비-0 · 앞 실패 뒤 성공도 비-0 · 전부 성공은 0 · process crash code
비가림 · 직접 exit code write가 session adapter 외 0으로 줄어드는 방향의 ratchet 가동.

### CLI2 — 소유 인자 parser·raw 재해석 제거 (P0 · 2일)

- lexical parser와 source location을 분리한다.
- descriptor 전에는 token/arity adapter로 착지하고 CLI3에서 typed validation을 연결한다.
- `object.parent`, `object.duplicate`, `prefab.create`부터 `parts + raw line` 혼합을 제거한다.
- Windows path, 빈 string, quote/escape, UTF-8 이름을 golden으로 고정한다.
- legacy ambiguous form 사용량을 계측한다.

완료 기준: migrated handler의 raw line 접근 0 · quoted 두 이름 실제 scene operation 통과 ·
기존 command file 40개 소비 경로의 입력 호환 · parser mutation canary 전부 발화.

### CLI3 — Descriptor registry·help·discovery (P0 · 2.5일)

- descriptor와 argument/capability metadata를 도입한다.
- `help`, `help <command>`, `commands.list`, `commands.describe`를 registry에서 생성한다.
- alias 중복·canonical 중복·help 누락을 초기화/selftest 실패로 만든다.
- `Invoke-Dx12Suite`가 source를 긁지 않고 명령 목록을 얻을 수 있는 discovery result를 제공한다.

완료 기준: 등록 명령 help/description coverage 100% · 수동 `PrintHelp` 목록 0 · 이름 중복 canary
발화 · registry snapshot 순서 deterministic · descriptor 없이 새 handler 등록 불가.

### CLI4 — 도메인 분리·Editor operation 의미 정렬 (P1 · 4일)

- §10의 domain 단위로 한 파일씩 이동·빌드한다.
- 이동하는 handler를 result-bearing으로 함께 바꾸되 domain 알고리즘은 수정하지 않는다.
- mutating command를 §9 네 분류로 전수 표기한다.
- play/duplicate/undo/selection의 GUI 대 CLI characterization을 닫고 shared operation으로 합류한다.
- test adapter는 기존 `Run*Test` 결과를 구조화할 뿐 테스트 본체를 끌어오지 않는다.

완료 기준: `ConsoleCommandSystem.cpp`가 facade/executor wiring 중심 · domain 하나당 unity/non-unity
build · GUI/CLI 의미 차이 미분류 0 · Player 링크 변화 0.

### CLI5 — JSONL output·자동화 소비자 전환 (P0 · 3일)

- `--result-format jsonl`과 schema v1을 도입한다.
- JSONL stdout에서 engine/log noise를 제거한다.
- domain별 consumer를 §11 순서로 전환한다.
- test discovery는 registry, verdict는 `CommandResult`, artifact는 structured path/digest를 쓴다.
- human/JSONL 동일 scenario logical result 대조를 자동화한다.

완료 기준: JSONL 모든 stdout line parse · sequence terminal 누락/중복 0 · source/help scraping 0 ·
한국어 verdict regex 0 · 기존 40개 소비자의 성공/실패 판정 동등.

### CLI6 — Unattended Editor 실행·affinity hardening (P1 · 1.5일)

- `InteractiveEditor`와 `UnattendedEditor`를 명시적으로 구분한다.
- descriptor affinity/capability를 전수 채우고 기본 GT를 유지한다.
- `MayBlock` command의 시작/완료/소요와 timeout 책임을 결과에 남긴다.
- external build process만 별도 ticket/poll이 이득인지 측정하고, scene/renderer command는 근거 없이
  worker로 옮기지 않는다.
- stdin reader cancel/join과 script-only no-reader 계약을 회귀로 고정한다.

완료 기준: unattended에서 modal 0 · timeout 시 process tree 정리 · wait/scene loading semantics 유지 ·
worker로 이동한 명령이 있다면 live Scene/raw pointer 접근 0을 별도 증명.

### CLI7 — legacy 제거·문서·최종 ratchet (P1 · 1일)

- `LegacyUnreported`, raw reconstruction compatibility, manual verdict print를 사용량 0 확인 후 제거한다.
- 직접 registry/source scraping 재도입 방지 정적 gate를 추가한다.
- CLI help, 이 문서, 대시보드, 대표 실행 예제를 최종 계약에 맞춘다.
- command 추가 checklist를 코드 가까이에 둔다.

완료 기준: void command handler 0 · terminal result 없는 command 0 · help drift 0 · direct exit write 1곳 ·
machine consumer의 text verdict parse 0 · §16 전부 통과.

### 의존 순서

```text
CLI0 → CLI1 → CLI2 → CLI3 ─┬→ CLI4 ─┐
                            └→ CLI5 ─┼→ CLI6 → CLI7
                                    ┘
```

CLI4와 CLI5는 파일 충돌을 피하도록 domain 단위로 병행할 수 있다. 같은 handler를 한쪽이
옮기고 다른 쪽이 formatter까지 바꾸는 동시 작업은 금지한다.

---

## 13. 검증 매트릭스

### 13.1 정적 gate

- registry canonical/alias 중복 0
- descriptor 없는 command 0
- argument/help coverage 100%
- migrated handler raw line access 0, 최종 전체 0
- `EngineBootstrap::SetExitCode` 직접 호출은 session adapter 한 곳
- command enumeration을 위한 `ConsoleCommandSystem.cpp` source scraping 0
- Editor command 파일의 Player project 등록 0

### 13.2 parser·result unit/selftest

| 시나리오 | 판정 |
|---|---|
| `""` | 빈 argument 1개 |
| `"Main Camera"` | quote 제거, 한 token |
| `object.parent "Big Boss" "Main Characters"` | 이름 둘 정확 보존 |
| Windows path | backslash 무손실 |
| 닫히지 않은 quote | `InvalidArguments`, handler 미호출 |
| 숫자 overflow/garbage | 조용한 0 변환 금지 |
| unknown command | exit 2 |
| precondition failure | exit 3 |
| selftest false | exit 4 |
| 실패 뒤 성공·quit | session 비-0 유지 |

### 13.3 build gate

- VS18/v145 Editor Debug/Release unity build
- Editor non-unity build — domain split의 전이 include 누락 검출
- 관련 PowerShell parser/static gate
- Player build/link surface 변화 0 확인

### 13.4 runtime gate

- `--exec` 반복 순서와 process exit
- `--script` 주석/빈 줄/source line
- `--console` stdin reader와 종료 회수
- missing script unattended 즉시 실패
- `wait N` 정확한 frame delay
- scene loading 중 다음 명령 보류
- play/stop/undo/selection GUI 의미 대조
- quoted object duplicate/parent/prefab authoring
- DX12/Vulkan 대표 selftest success/failure와 artifact
- human/JSONL logical result diff 0
- JSONL stdout noise 0
- 전체 `Tools/regression/run-all.ps1`와 DX12 suite

새 gate가 처음부터 초록이면 다음 변이로 이빨을 확인한다.

- result success를 강제로 failure로 바꾸면 process exit가 붉어짐
- descriptor에서 command 하나를 빼면 discovery/help coverage 붉어짐
- parser quote close 처리를 제거하면 golden 붉어짐
- JSONL 앞에 일반 로그 한 줄을 넣으면 stdout purity 붉어짐

---

## 14. 위험과 롤백 경계

| 위험 | 대응/롤백 |
|---|---|
| 176개 명령 signature 일괄 변경 | legacy adapter로 domain별 이행, 한 번에 전환 금지 |
| human 로그를 바꿔 기존 regex 파손 | JSONL consumer 이행 전 human verdict 유지 |
| command 분리 후 unity build 숨은 include 드러남 | domain 하나씩 이동, unity/non-unity 둘 다 build |
| exit code 강화로 기존 false-green script가 갑자기 실패 | CLI0 canary·consumer inventory로 원인을 노출, 성공으로 되돌리지 않음 |
| GUI 동등성을 맞추며 fixture semantics 변화 | characterization 후 shared/raw 분류, 필요 시 test-only ID로 분리 |
| parser가 Windows path `\`를 escape로 삼음 | 명시 escape 외 backslash 보존 golden |
| JSON data가 live pointer/타입을 누출 | owned `CommandData`만 허용, artifact reference 사용 |
| GT 밖으로 옮긴 명령의 data race | affinity 기본 GT, snapshot/worker 안전 증명 전 이동 금지 |
| `ConsoleCommandSystem.cpp` 대규모 move와 동시 작업 충돌 | handler domain별 작은 patch, 기존 사용자 변경 파일 보존 |

각 슬라이스는 이전 human mode와 command file을 유지하므로 rollback은 해당 domain adapter와
registry entry 단위로 가능해야 한다. CLI7 전에는 legacy adapter를 제거하지 않는다.

---

## 15. MCP 보류 경계

이번 계획의 완료가 MCP 착수를 자동 승인하지 않는다. 다음이 전부 충족되고 사용 사례가 별도로
확정된 뒤 새 계획에서 다시 판정한다.

1. 모든 외부 노출 command가 descriptor/result-bearing이다.
2. JSONL schema v1과 process exit가 전체 regression에서 안정됐다.
3. `MutatesScene/Assets`, `TestOnly`, backend 요구 등 capability가 전수 분류됐다.
4. long-running command의 timeout/cancel/partial result 수명이 정의됐다.
5. remote 호출이 허용할 mutation·filesystem 범위와 사용자 승인 모델이 정해졌다.
6. MCP 없이도 필요한 automation이 CLI/JSONL로 해결되지 않는다는 실제 요구가 있다.

그 전까지 금지:

- command 하나를 MCP tool 하나로 기계적 노출
- human stdout parser를 MCP adapter로 포장
- descriptor에서 곧바로 `tools/list`를 생성하는 코드
- MCP를 이유로 command ID/argument를 먼저 변경
- MCP transport 의존을 Editor/Engine command core에 추가

보류는 “나중 슬라이스”가 아니라 **현재 scope 밖**이라는 뜻이다. 대시보드에도 MCP task를
등록하지 않는다.

---

## 16. 최종 완료 조건

- [ ] 기존 `--exec`/`--script`/`--console` 입력과 dotted ID가 호환된다.
- [ ] 등록 command/alias 전부 descriptor와 argument/help를 가진다.
- [ ] 모든 command가 정확히 하나의 terminal `CommandResult`를 만든다.
- [ ] 논리 실패가 뒤의 `quit`와 무관하게 process 비-0으로 끝난다.
- [ ] handler raw line 재해석, `atoi/atof` 조용한 실패, manual help 목록이 0이다.
- [ ] human mode와 JSONL mode의 logical result가 같다.
- [ ] JSONL stdout 모든 줄이 schema v1으로 parse되고 일반 로그가 섞이지 않는다.
- [ ] 자동화 consumer가 source/help/Korean verdict 문자열을 파싱하지 않는다.
- [ ] GUI와 CLI mutating operation의 공유/의도적 차이가 전수 분류되고 gate로 고정된다.
- [ ] `ConsoleCommandSystem.cpp`는 facade/Pump wiring 중심이며 domain handler가 분리된다.
- [ ] Editor unity/non-unity Debug/Release와 대표 runtime/regression이 통과한다.
- [ ] Player binary/project 표면에는 Editor CLI가 들어가지 않는다.
- [ ] MCP 구현·transport·tool schema가 0이다.

여기까지 닫혀야 “명령이 많다”가 아니라 **자동화 계약이 안정된 CLI subsystem**이라고 판정한다.

---

## 17. 문서 관계와 상태 규약

- 대시보드 PHASE 0 `0-8`은 최초 자동화 레이어의 완료 기록으로 유지한다.
- 이 문서의 CLI0~CLI7은 PHASE 6의 지속적 품질/정식화 트랙으로 관리한다.
- 계획 작성만으로 CLI0을 완료 처리하지 않는다.
- 문서 정적 검사와 실제 build/runtime 검증을 구분한다.
- 구현 슬라이스가 끝날 때만 이 문서와 대시보드의 상태·실측 수치·gate 결과를 함께 갱신한다.
- MCP 보류 상태는 별도 task 진행률에 포함하지 않는다.
