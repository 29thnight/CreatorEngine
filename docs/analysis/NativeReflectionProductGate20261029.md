# F# 전용 스크립팅 및 API 49 제품 게이트 — 2026-10-29

## F# 전용 전환 완료 — Windows Release

게임 스크립트와 `Tools/regression`의 C# 소스·프로젝트를 제거했다. 남은 구현은 F#으로 이식했고, 사용되지 않던 비교 도구와 미호출 레거시 fixture는 삭제했다. `ScriptCore`, `ScriptCore.Generators`, 기존 `ScriptRuntime`, 구 `GameScripts.csproj`·`GameScripts.fsproj` 디렉터리/프로젝트도 제거했다. 솔루션, CLRHost, Editor의 자동 컴파일, 배포·패키징·headless 도구는 F# 전용 경로를 사용한다. 구 관리 어셈블리 출력은 `Build/Verification/RetiredManagedArtifacts-*`로 옮겨 현재 Bin에서 제외했다.

현재 런타임은 `Engine/Scripting/Bindings`, 값 형식은 `Engine/Scripting/AudioValues`, F# 작성 보조는 `Engine/Scripting/Authoring`, 게임 스크립트 빌드는 `Engine/Scripting/GameScripts`가 담당한다. 바인딩 소스는 `Components`, `Services`, `Interop`, `Runtime`, `Values`, `GeneratedNative`로 구분한다. [소스 배치 안내](../../Engine/Scripting/README.md)를 따른다. 배포 입력은 `scriptBackend=fsharp-native`만 받는다. 빌드 도구와 값 생성자 보정 도구는 게임 스크립트 런타임에 포함되지 않는다.

회귀 이식 중 확인한 두 오류도 수정했다. `PhysicsBodyComponent.SetShapeRole`은 Guid 메모리가 아니라 네이티브 계약에 맞는 NUL 종료 UTF-8 UUID 문자열을 넘긴다. 센서 접촉 키는 같은 식별 필드를 유지하면서 두 개의 5항목 구조체 튜플로 나눠 긴 튜플의 해시 할당을 없앴다.

| 검증 | 결과 | 원본 |
|---|---|---|
| 엔진 Release 빌드 | reflgen 1.3.0, Editor·Player·AssetCooker·AssetPacker·ProfilerViewer 빌드 및 배포 생성 성공 | `Build/Verification/fsharp-only-publish.log` |
| 최종 배포본 | `scriptBackend=fsharp-native`, API 49 | `Build/Verification/FSharpOnlyVerifiedDistribution/local-0.0.0.0-win-x64-Release-888e3945-88e9-4ec8-ade1-562b6392954f` |
| 최종 Editor·cooked Player | 각각 컴포넌트 27개·타입 682개·동작 검사 21개, 고정 틱 30회, 이동 0.65, 이전 컴포넌트 핸들 거부, 오디오·렌더링·정상 종료 통과 | `Build/Verification/NativeReflectionProduct-84a774d557f04d2f93e55bf9b6226261/result.json` |
| 전체 F# 스크립트 | 78개 컴파일, ScriptCore 의존성 없음 | `Build/Verification/NativeCorpus-9d57b03e60b14676b6647429100cc38e` |
| 런타임 단위 회귀 | 고정 틱 16개·바디 식별/역할 문자열 16개·접촉 76개, API 슬롯 220개, hostfxr 호출 통과; 정상상태 접촉 할당 0 | `Build/Verification/fsharp-runtime-regressions.log` |
| 물리 ABI | 레이아웃·호출 계약 70개 및 슬롯 순서 220개 통과 | `Build/Obj/Phase19M0Script/abi-Release.log` |
| 접촉 제품 회귀 | 기본·그룹화·늦은 관찰자·솔리드·객체/컴포넌트 제거·바디 교체/연속 교체·센서 전환·구독자 활성화·역할 변경/명시적 바인딩·용량 초과·고의 예외 14가지 모두 Editor·Player 통과 | `Build/Verification/FSharpContactMigration-db61e56eb81a48a28eee9be80b4cfe7d/results.json`, `Build/Verification/FSharpContactMigration-e2578249312d4ae582e82ecf430a3be7/results.json` |
| 자동 F# 작성·핫리로드 | 109개 검사 통과, 추가·삭제·이름 변경·실패 시 이전 코드 유지·재생 중 교체·필드 보존 | `Build/Verification/fsharp-only-authoring-product.log` |
| 오디오 Player | 재생/정지/완료 100회, GC 이후 핸들 유지, 패키지 불변 통과 | `Build/Verification/fsharp-audio-player-product.log` |
| 오디오 Editor | Play/Stop 100회·관리 시작 100회·정리 100회 통과 | `Build/Verification/fsharp-audio-editor-product.log` |
| CurveIndicator Editor | 실제 재질 그래프와 사본 생성·알파·없는 상수 검사 4개 통과 | `Build/Verification/NativeUi-1e16dcb998214b2e90e685698b60bea6` |
| 배포 도구 | 189개 검사, 퇴역 백엔드 배포 입력 거부 포함 | `Build/Verification/fsharp-only-buildtool-tests-isolated.log` |
| 소스/도구 정리 | `FSHARP_ONLY_LAYOUT_OK`, PowerShell 구문·Python 구문·NetworkHandoff 등록 검사 통과 | `Tools/regression/verify-fsharp-only-layout.ps1` |

B2·Dagger F# 이식 코드는 위 78개 컴파일에 포함된다. 해당 제품 fixture의 원본 GLB가 없어 실행 검증은 사용자의 지시에 따라 제외했다. 이하 내용은 이 최종 정리 이전의 API 49 전환 기록이다.

## 이전 API 49 전환 검증

현재 네이티브 계약은 API 49다. API 48에서 도입한 재귀 리플렉션 전송에 API 49의 게임 스레드 콜백 등록·세대 폐기 계약이 추가됐다. 실제 Editor·cooked Player용 게이트는 소스와 바인딩에서 버전을 읽어 현재 API를 검사한다. 기존 [API 46/47 검증 기록](FSharpNativeBindingAcceptance20261029.md)은 이전 실행의 증거다.

2026-10-29 Windows Release 후속 실행: reflgen 1.3.0으로 엔진 배포물 빌드와 검증을 완료했고 실제 Editor 및 cooked Player에서 제품 게이트가 통과했다. 원본은 `Build/Verification/NativeReflectionProduct-d665e4c1f67c4d4897e9b562b372679c/result.json`이다. 각 실행은 컴포넌트 27개와 타입 682개를 확인하고 고정 틱 30회, 캐릭터 이동 0.65, 라이트·데칼 값과 오디오 재생·완료를 검사했다. Editor에서 제거한 컴포넌트의 이전 핸들도 거부됐다. Editor·Player 오디오 콜백은 각각 586회·384회이고 백엔드 실패는 0이다. 원본 결과에는 각 검사와 종료 기록이 포함되며 모든 네이티브 멤버를 개별 실행한 결과는 아니다. 아래 표는 이 후속 실행 이전의 기록이다.

현재 로컬에서 확인한 범위는 다음과 같다.

| 검사 | 확인한 결과 | 제품 실행 여부 |
|---|---|---|
| 네이티브 서비스/F# 계약 fixture | `SCRIPT_REFLECTION_OK checks=712 types=63 api=49 bridge=C#-free`; 콜백의 libclang/MSVC bool·중첩 const 참조 표기 대응 및 다른 bool 플래그·constness 거절 포함 | 실제 서비스 코드, FSM 자식 저장소·Animator 출력 어댑터와 fixture 타입을 실행; Editor/Player 실행 아님 |
| 네이티브 콜백 fixture | `NATIVE_REFLECTION_CALLBACKS_OK checks=129 signatures=5` | 콜백 계약 실행; GPU·오디오 제품 실행 아님 |
| `NativeReflectionProduct.Compile.fsproj` | API 49 Windows codegen 실행 `40806497136`에서 실제 엔진 헤더로 생성한 F# 바인딩과 제품 프로브 컴파일 성공; 오류 0개, 기존 `ScriptNative` 경고 63개 | 컴파일만 수행; Editor/Player 실행 아님 |
| `verify-native-reflection-product-results.ps1` | PowerShell 7.4.7에서 결과 소비자·네이티브 파일 검증 35개 검사와 제품 실행 스크립트 구문 검사 성공 | 합성 결과·파일의 거부 검사 |
| 계약·제품 workflow | YAML 및 PowerShell 블록 구문 검사 성공 | 제품 workflow 미실행 |
| 새 codegen·release workflow | YAML 2개, PowerShell 블록 3개, bash 블록 2개, Python 블록 3개 구문 검사 성공 | 게시·호스트 동작을 구문 검사에서 실행하지 않음 |
| `verify-fsharp-native-product.ps1` | 후속 Windows Release 실행에서 `NATIVE_REFLECTION_PRODUCT_OK` | 실제 Editor·cooked Player 실행 |

계약 실행의 원본 기록은 로컬 작업공간 `.build/probe-debug/Testing/Temporary/LastTest.log`, `.build/probe-debug/managed/Debug/managed-run.log`에 있다. 63개 타입은 서비스 fixture의 타입 수이며 실제 엔진 전체 카탈로그의 크기로 해석하지 않는다. 추가 workflow 구문 검사 기록은 `.build/workflow-review-20261029/syntax-results.log`다.

초기 제품 프로브의 로컬 컴파일은 Linux에서 `-std=c++20 -D__stdcall= -D__cdecl=`로 생성한 API 선언을 사용했다. Windows 호출 규약 검증은 별도의 Windows 계약 게이트가 맡는다. F# 프로브 자체에는 경고를 오류로 처리하며, 이 초기 빌드에서 참조하는 기존 `ScriptNative` 프로젝트의 경고 63개는 그대로 보고됐다.

작업 브랜치의 커밋 `d4dfa637f16a9c1dfd5df5485ac88092f6e23d70`에 대한 [Windows codegen 실행 40806497136](https://github.com/29thnight/CreatorEngine/actions/runs/40806497136)이 성공했다. 실제 Windows 엔진 헤더로 생성한 RenderEngine·SceneRuntime 등록 코드, ScriptReflectionBindings·ScriptReflectionCallbacks, StateMachineComponent.cpp와 BehaviorTreeComponent.cpp의 여섯 object를 컴파일했다. 생성 파일 369개에 대해 일치 검사를 통과했으며, 해당 F# 바인딩과 제품 프로브도 컴파일했다. BlackBoard native-only 인자 정책, Animator 출력 저장소, Terrain의 bounded getter를 포함한 최종 소스의 Windows 생성·컴파일 증거다. Editor/Player 실행과 구분한다.

## 최종 Windows 생성물의 API 범위

위 실행의 생성물을 그대로 반영한 [`coverage.json`](../../ScriptNative/GeneratedNative/coverage.json)은 컴포넌트 27개, 네이티브 선언 328개와 수동 런타임 스키마 35개, 레코드 뷰 363개, enum 88개를 기록한다. F# 소스 365개가 컴파일 목록과 중복·누락 없이 일치한다. `native-surface.json`의 SHA-256은 `6a6cc0d153db14fc4476dddb04b4b76b7057ccf0043ddfdf02262b49f9824813`이며 coverage의 원본 해시와 같다.

| 멤버 | 타입 지정 접근 | 명시적 unavailable | 전체 |
|---|---:|---:|---:|
| 필드 | 1,794 | 3 | 1,797 |
| 메서드 | 1,048 | 154 | 1,202 |
| 합계 | 2,842 | 157 | 2,999 |

기존 생성물의 멤버 2,712개와 원시 선언은 모두 보존됐고 멤버 287개가 추가됐다. 기존 unavailable 202개가 typed로 전환됐다. 소유권 이동 대입 연산자인 `AssetEntry::operator=(AssetEntry&&)`와 `TextureImage::operator=(TextureImage&&)` 두 개는 명시적 unavailable로 바뀌었다. 컴포넌트 자체 필드 162개는 모두 typed다. 컴포넌트 자체 메서드는 486개 중 374개가 typed이며, 나머지는 엔진 내부용 110개, 원시 `Terrain.GetHeightMap()`과 authoring용 `UIComponent.LoadLegacyNavigation()`이다. Terrain의 읽기 접근에는 실제 저장소 크기로 제한된 `GetHeightSamples()`가 제공된다.

전체 unavailable 157개는 엔진 내부 API 138개, 포인터·원시 버퍼 7개, 소유권 이동 3개, type descriptor API 3개, authoring 노드 API 3개, 계층 변경 RAII scope 1개, 자산 내부 IO 콜백 2개다. 각 멤버는 사유가 있는 `NativeUnavailableMember`로 생성된다. typed 집계는 생성된 타입 지정 인터페이스의 수이며, 모든 멤버를 실제 Editor/Player에서 실행했다는 뜻이 아니다. 런타임 descriptor의 읽기·쓰기·호출 가능 여부와 세대·소유권 검사는 호출 시에도 적용된다.

## 실제 제품 게이트가 요구하는 증거

[`verify-fsharp-native-product.ps1`](../../Tools/regression/verify-fsharp-native-product.ps1)은 검증한 엔진 배포물을 입력으로 받는다. 배포물의 구성·소스 커밋·native API가 현재 체크아웃과 일치하는지 먼저 확인한다. 이어서 검증 배포물의 `Runtime/Manifests/CreatorEditor.json`을 기준으로 launcher, 실제 `CreatorEditor.runtime.dll`, 공유 네이티브 의존성 전체의 크기와 SHA-256을 저장소 실행 경로와 대조한다. 동일한 launcher에 오래된 runtime DLL만 섞인 경우도 거부하며, 비교한 digest·runtime hash·파일 수를 `editor.native-closure.json`과 최종 결과에 기록한다. 별도 작업 디렉터리에 기존 캐릭터 물리 장면, F# 프로브, 직접 생성한 PCM WAV와 셰이더를 구성한다.

버전은 성공 문자열에 고정하지 않는다. F# 프로브가 바인딩 검증 이후의 `NativeReflection.ApiVersion`을 보고하고, runner는 `ScriptApiVersion.h`와 Editor·Player 배포 매니페스트의 버전에 같은 값을 요구한다. 현재 소스가 49이므로 API 48 결과는 현재 제품 게이트를 통과하지 못한다. 결과 reader의 최솟값 48과 API 48 합성 fixture는 전송 프로토콜의 최소 지원 범위를 검사하며 실제 실행 버전 판정은 정확한 일치 검사로 수행한다.

Editor와 Player는 각각 자신이 시작한 프로세스의 PID와 명령 서비스 endpoint만 사용한다. 성공 줄은 정확히 한 개여야 하며 종료 뒤 전체 출력을 다시 읽어 늦게 도착한 실패·중복 결과도 거부한다.

1. 실제 네이티브 카탈로그의 컴포넌트 이름을 `ComponentTypeUUID.h`의 전체 목록과 대조한다. `ScriptComponent`와 `InvalidScriptComponent`, 고유 타입 ID, 컴포넌트보다 큰 도달 가능 타입 집합을 확인한다.
2. F# 생성 타입으로 Light 스칼라·색, 중첩 렌더 프로필, Decal 필드, Unicode 상태 이름, nullable borrowed 결과, Character 정의와 Sound 설정 값 반환을 읽고 쓴다.
3. 실제 고정 틱에서 실행 순서와 60 Hz 간격을 확인하고 캐릭터 상태 조회·목표 속도 설정 뒤 월드 X 위치가 0.1보다 크게 변했는지 확인한다. 30~180 틱 이내에 끝나야 한다.
4. Sound의 타입 지정 재생·일시정지·재개·정지, GC 이후 값 핸들, 비동기 종료 알림을 확인한다. Editor와 Player 종료 시의 `audio.host` 이벤트는 Device 모드, 실제 출력 콜백, 백엔드 실패 0을 요구한다. Null·DegradedDevice·Offline은 통과할 수 없다.
5. Editor에서 Decal을 제거하고 다시 추가한 뒤 이전 핸들이 `stale_component`로 거부되는지 확인한다. Play/Stop 커밋과 정상 종료도 요구한다.
6. 같은 장면을 쿠킹·패키징하고 native presenter에 실제 게임 프레임을 제출하는 패키지 smoke를 실행한다. 이어서 별도 Player를 실행해 F# 프로브의 완료를 기다린다. 짧은 렌더 smoke가 고정 틱 검증을 대신하지 않는다.
7. 각 호스트의 순서가 있는 runtime journal, 렌더 백엔드 큐 정리와 정상 종료를 확인한다. Player의 authoring 텍스트 파서 호출은 0이어야 한다. Player 실행 전후의 패키지 파일 목록·해시도 같아야 한다.

음성 장치 콜백과 논리 재생 상태를 확인하는 검사다. 사람에게 들리는 파형의 품질, 모든 렌더 효과의 픽셀 정확성, 모든 카탈로그 멤버의 실제 사용까지 보장하지 않는다. 카탈로그 전체성 검사와 대표 멤버의 실행 검사는 다른 범위를 측정한다.

Editor 관리 스크립트 네 파일은 먼저 백업한다. 성공·실패 모두 소유한 Editor를 종료한 뒤 원본을 복원하고 해시를 확인한다. 엔진 배포물 자체는 변경하지 않는다.

## Windows 실행 조건

[`native-reflection-product.yml`](../../.github/workflows/native-reflection-product.yml)은 수동 실행 전용이다. 자동 PR 실행으로 신뢰된 워크스테이션을 사용하지 않는다. 기본 runner label은 `["self-hosted","Windows","X64"]`이며 실행 시 변경할 수 있다.

2026-10-29 동일 작업 세션에서 [저장소의 self-hosted runner 화면](https://github.com/29thnight/CreatorEngine/actions/runners?tab=self-hosted)을 직접 확인했으며 **이 저장소에 등록된 runner가 없었다.** 따라서 현재 등록 상태에서는 필요한 Windows GPU·오디오 제품 검증을 실행할 수 없다. 공개 hosted runner의 codegen·계약 작업을 제품 실행 증거로 대체하지 않는다. 아래 조건을 갖춘 runner를 준비한 뒤 제품 workflow를 실제로 완료해야 한다.

- 실제 DX12 장치와 출력 가능한 오디오 장치를 가진 Windows 세션.
- Visual Studio 2026 C++ v145, Windows SDK, .NET 10 SDK, PowerShell 7.
- 사용자 MSBuild에 통합된 vcpkg. 체크아웃은 엔진 `vcpkg.json`의 `builtin-baseline` 커밋과 같아야 한다. 게이트는 프로젝트 빌드 전에 매니페스트 의존성을 설치한다.
- `GITHUB_WORKSPACE/FSharp_Tool_Pakage_for_CreatorEngine`의 기존 **비공개** F# 도구 체크아웃. 기본 고정 커밋은 `dcfa051e1354a6caa3ab4389b168429f73a2342e`이며 workflow 입력으로 정확한 다른 커밋을 지정할 수 있다. 작업 트리가 깨끗해야 한다.

엔진 checkout은 `GITHUB_WORKSPACE/CreatorEngine`에 배치되므로 사전 설치한 형제 도구 저장소와 분리된다. 공개 엔진 저장소의 `GITHUB_TOKEN`으로 비공개 도구를 가져오지 않으며 새 비밀 키도 가정하지 않는다. 필수 도구가 없거나 오디오가 대체 모드로 내려가면 게이트가 실패한다. 조건부 건너뛰기로 성공을 만들지 않는다.

workflow는 BuildTool로 실제 Editor·Player·쿠킹 도구를 빌드하고 불변 배포물을 만든 뒤 제품 게이트를 실행한다. 업로드 목록은 최상위 진단 로그·JSON 결과로 한정한다. 비공개 도구 소스, 배포물, 백업 관리 어셈블리, 임시 command-service token은 업로드하지 않는다.

준비된 개발 환경에서 직접 실행할 수도 있다.

```powershell
./Tools/regression/verify-fsharp-native-product.ps1 `
    -Configuration Release `
    -EngineDistribution C:/CreatorEngine/Build/Distributions/<verified-distribution>
```

`-Work`를 지정하면 존재하지 않는 새 디렉터리여야 한다. 성공 시 해당 디렉터리의 `result.json`은 `NATIVE_REFLECTION_PRODUCT_OK`, 실제 PID별 프로브 결과, 사용한 소스 커밋·API, 오디오 장치 진단을 기록한다. 실패 시 남아 있는 출력은 원인 분석용이며 통과 증거가 아니다.

## 공개 Windows 계약 CI와의 구분

기존 [`native-reflection-contracts.yml`](../../.github/workflows/native-reflection-contracts.yml)의 Debug·Release 작업은 네이티브 서비스 fixture와 F# 계약, 실제 엔진 헤더 생성·컴파일을 실행한다. 여기에 **제품에서 사용할 동일 F# 소스의 컴파일**과 결과 소비자 검사를 추가했다. 이 작업은 비공개 도구 없이 공개 `ScriptNative`를 참조하며 실제 Editor/Player를 링크하거나 실행했다고 주장하지 않는다.

[`native-reflection-codegen.yml`](../../.github/workflows/native-reflection-codegen.yml)은 별도의 Windows 작업 checkout에서 production 헤더를 읽어 생성물을 갱신하고, 생성 결과 일치 검사와 F# 제품 프로브 컴파일을 수행한다. 생성물과 소스 커밋·reflgen 커밋·workflow URL을 아티팩트로 보존한다. 저장소 ref를 직접 수정하지 않으며 Editor/Player 실행은 포함하지 않는다.

Windows 로컬 제품 게이트는 통과했으며 별도의 제품 workflow 실행은 이 결과에 포함되지 않는다. 이번 제품 프로브의 FSM 검사는 이름 왕복과 없는 이름 조회까지다. 상태·전이의 ID, 이름 변경·삭제·재생성, 콜백 등록·호출, Animator 출력 인자와 Terrain의 bounded view 검사는 생산 서비스·어댑터 fixture에서 실행했으며, 실제 Editor/Player에서 이 시나리오들이 완료됐다고 확대해 기록하지 않는다.
