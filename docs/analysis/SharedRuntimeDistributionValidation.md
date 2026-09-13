# 공용 DLL·사전 빌드 배포·버전 정책 적용 검증

검증일: 2026-09-13. 기존 UI/Editor 작업이 함께 있는 작업 트리 기준이며 Git commit 완료 보고가 아니다.

## 구현

- `Runtime/Common`에 구성별 공용 native DLL을 배치하고 `Runtime/Editor/efsw.dll`을 별도로 둔다.
- 네 native 실행 파일은 정적 CRT의 작은 진입점이며, 검색 경로 설정 후 각각의 `*.runtime.dll`을 로드한다.
- 엔진의 정적 라이브러리는 host DLL 하나로 링크한다. EXE 경계를 넘는 engine 객체/할당/예외를 만들지 않는다.
- 버전 정본 `EngineVersion.json`: `CreatorEngine 2`, 기능 릴리스 미지정, `0.0.0.0`, Preview, 로컬 개발용.
- `EngineVersion.h`, `EngineVersion.rc`, `EngineVersion.props`는 명시적 버전 편집 때 동기화한다. Git/컴파일 횟수로 번호를 올리지 않는다.
- About·크래시 보고·native `--engine-info`·EXE/DLL 파일 속성·배포 manifest의 버전 경로를 연결했다.
- UUID 배포 ID, payload digest, source revision/dirty, 구성, Script API 24를 별도 기록한다.
- 게임 C# 컴파일과 cook/pak/Player 검증은 독립 `CreatorBuildTool`을 사용한다. 최종 엔진 묶음에는 C# 컴파일러와 private .NET이 포함되며 PowerShell host는 포함하지 않는다.

## 완료한 검사

| 검사 | 결과 | 기록 |
|---|---|---|
| 최신 솔루션 x64 Debug Build | 통과 | `Build/runtime-full-debug.log` |
| 최신 솔루션 x64 Release Build | 통과 | `Build/runtime-full-release.log` |
| CreatorBuildTool 회귀 | 49 checks 통과 | `BuildTool/Tests` Release, 실제 배포본 `--engine` 포함 |
| 버전 경계값·배포 ID·변조/추가 DLL·native metadata 검증 | 13 checks 통과 | `verify-engine-distribution.ps1 -StaticOnly` |
| 네 native host의 최소 PATH/외부 CWD 로딩, DLL 격리, 파일 버전, 긴 UTF-8 인자 파일 | 최종 배포본 구성별 48 checks 통과 | `verify-engine-distribution.ps1 -Config Debug/Release -EngineDistribution ...` |
| EXE 옆 DLL 정리 | 네 host 모두 자기 runtime DLL만 남음 | native host 배치 및 회귀 |
| 외부 Editor 실행·CLI·DPI·스타일·종료 | Debug·Release 통과 | `Build/Tests/prebuilt-editor-<Config>.jsonl` |
| 외부 게임 compile → cook → PAK → Player → publish | Debug·Release/DX12 통과 | `Build/Tests/prebuilt-project-<Config>-dx12.json` |

기존 경고(일부 C++ 변환/중복 매크로, 무시되는 Vulkan 지연 로드 pragma, C# 분석 경고)는 빌드 로그에 남아 있다.

## 실행 검증과 수정

검증 사본은 `%LOCALAPPDATA%/CreatorEngine/DistributionValidation/외부 게임 Debug 74445d89d1434cbfaeaf24db1cff0914`다.
원본 `Dynamic_CPP`의 Assets·ProjectSetting·Library/ModelAssetGenerations를 복사하고,
`GameScripts/*.cs`를 이 사본의 `Assets/Script/ValidationSamples`에 넣었다. 원본 프로젝트의 씬과 스크립트는 이동하지 않았다.

자식 프로세스의 PATH는 Windows/System32·Windows만 남기고 DOTNET_ROOT는 없는 경로로 지정했다.
배포본의 private .NET·Roslyn으로 게임 DLL을 만들고 포함된 native 도구만 실행했다.
회귀의 host trace도 시스템 .NET 대신 배포본의 app-relative runtime 선택을 확인한다.

검증 과정에서 다음 문제를 수정했다.

- 167개 에셋의 절대 경로 전달이 약 49,000자로 늘어나 프로세스 생성에 실패했다. 쿠커에 UTF-8 인자 파일을 추가했다.
- 일반 파일 처리를 위한 longPathAware 설정을 실행 진입점에 추가하고, 패키지 후보/게시 폴더에서 긴 프로젝트 이름을 반복하지 않게 했다.
- Editor의 기존 PerMonitorV2 manifest를 새 EXE에도 적용했다. 최종 DPI·스타일 검사에서 `clean=true`를 확인했다.
- efsw 등록·이벤트 경로, CLR의 GameScripts 경로, Editor CLI 입출력 파일을 UTF-8/Windows 경계에서 올바르게 변환했다.
- C# 원본을 PAK에서 제외하면서 그 `.cs.meta`가 cook 입력에 남아 존재하지 않는 소스를 CEMF에 등록하던 문제를 수정했다.
  관리 DLL 컴파일 후 C# 원본과 sidecar를 함께 native cook 입력에서 제외한다. shader 원본·sidecar는 유지하는 회귀를 추가했다.
- host의 표준 C++ 예외는 경계를 넘기지 않고 구체적인 실패 원인을 출력한다.

마지막 솔루션 전체 빌드가 두 구성 모두 오류 0으로 끝난 뒤, Editor EXE의 DPI manifest 배치도 두 구성에서 재빌드·실행 검증했다.

## 최종 배포본과 게임

`Build/Distributions`에는 아래 두 로컬 개발 묶음을 남겼다. 버전은 모두 `0.0.0.0 / preview`이며 공식 발행 번호가 아니다.

| 구성 | 엔진 배포 ID | 게임 게시 ID |
|---|---|---|
| Debug | `65c7df2c-9d24-4cf6-b05d-5dfedf5f3085` | `db69d56f54274dfd8ddf6dc290801953` |
| Release | `3c21f644-0021-4ede-bed9-69c1c6723f62` | `db01753d30704761b79610e51f2a4974` |

배포 폴더는 `local-0.0.0.0-win-x64-<Config>-<엔진 배포 ID>`다. `Bin/x64-<Config>/engine.distribution.path`가
각 최종 묶음을 가리킨다. 검증 프로젝트의 설정별 pin도 이 두 ID와 일치한다.
게임은 해당 프로젝트 `Intermediate/Builds/<Config>-dx12/Game-<게임 게시 ID>`에 있다.

| 게임 검증 | Debug | Release |
|---|---:|---:|
| 콘텐츠 파일 / 런타임 파일 | 532 / 229 | 532 / 224 |
| 모델 generation 가공 / artifact | 14 / 262 | 14 / 262 |
| CEDO 문서 | 19 | 19 |
| Player 종료 코드 | 0 | 0 |
| GT 프레임 / display / promotions | 19869 / 2 / 2 | 95504 / 2 / 2 |
| 등록 C# 타입 | 48 | 48 |
| 초기화 → 시뮬레이션 시작 | 각각 1회, 순서 통과 | 각각 1회, 순서 통과 |
| CEMF identity / cooked entry | 238 / 444 | 238 / 444 |
| meta 파싱 / text parser 호출 | 0 / 0 | 0 / 0 |
| 추출 파일 해시·payload 불변·게시 | 통과 | 통과 |

게시 manifest 사본은 `Build/Tests/prebuilt-package-Debug.json`, `prebuilt-package-Release.json`에 보관했다.
Editor를 열고 닫은 뒤에도 배포본의 파일 목록과 해시 검증을 통과했다.

## 통과로 세지 않은 항목

- 전체 원본 사본의 `Test1.creator`·`Test2.creator`는 누락된 dependency GUID 8개 때문에 cook이 거부됐다.
  실패 로그는 `Build/Tests/prebuilt-project-all-scenes-rejected.log`다. 성공 검증에서는 명시적 옵션으로 이 두 씬을 **검증 사본에서만** 제외했다.
- `FT_Primitives` 모델 씬은 runtime text-parser 8회를 기록했다. `LoadModelAssetGeneration`이 쿠킹된
  `ProjectSetting/AssetIdentity.asset`을 `ReadText` → `ReadIdentityEpochHeader` → `ParseText`로 읽는 기존 계약 문제가 남아 있다.
  근거는 `Build/Tests/prebuilt-model-runtime.stdout.log`, `Engine/RenderEngine/Assets/ModelAssetGeneration.cpp`, `AssetIdentityEpoch.cpp`다.
- 성공한 `PrebuiltSmoke.creator`는 FT fixture의 루트·카메라·조명 세 Entity와 PackageSmokeProbe를 사용한다.
  모델 generation의 **가공** 통과를 모델 **렌더링** 통과로 해석하지 않는다. 실패 게이트를 완화하거나 모델 씬을 성공으로 게시하지 않았다.

## 산출물 정리

EXE 옆 공용 DLL 중복은 정리됐다. 중간 배포본 12개는 삭제 자동 승인 검토가 정책 차단하여,
삭제 대신 `Build/ValidationArchive/RuntimeDistributions`로 이동했다. 현재 선택된 두 묶음은 유지했다.
실패 후보와 이전 검증 게임 10개는 검증 프로젝트의 `Intermediate/ValidationArchive`로 이동했다.
기록은 `Build/Tests/shared-runtime-cleanup.json`, `prebuilt-candidates-archive.json`이다.

## 범위

설정별 `Engine.<Config>.lock.json`은 명시적 개발 adapter pin이다. 정식 `.creatorproject` parser/프로젝트
lock/Launcher/MSI, 공식 버전 발행과 서명/Stable 승격, PHASE 22의 FMOD-free 전환은 이번 검증의 완료 항목이 아니다.
최소 PATH 검증은 실제 clean VM에서 도구를 제거한 시험과 구분한다. Vulkan·Shipping 실행은 이번 최종 검사에 포함하지 않았다.
native plugin/Core ABI와 native hot reload도 후속 경계다. 기존 모델 씬을 포함한 모든 게임의 배포 준비 완료를 선언하지 않는다.

사용법: [사전 빌드 개발 환경](../../Tools/distribution/README.md), [CreatorBuildTool](../../BuildTool/README.md).
