# CreatorBuildTool EXE 전환 검증

검증일: 2026-09-13. 작업 공간의 기존 변경을 포함한 개발 빌드이며 공식 릴리스 검증이 아니다.

## 구현 범위

- `BuildTool/CreatorBuildTool.csproj`를 솔루션 Tools 그룹에 추가했다.
- 엔진 배포본 생성, 프로젝트 pin/검증, Roslyn 컴파일, cook·PAK·Player 검증·불변 게시를 C# EXE로 이관했다.
- Editor 게임 빌드와 비동기 스크립트 컴파일은 배포본의 EXE를 직접 실행한다.
- 기존 PowerShell 진입점은 소스 checkout에서 인자를 전달한다. 배포본에 PowerShell과 빌드 스크립트를 넣지 않는다.
- 배포본의 private .NET을 찾는 apphost를 일반 Build에서도 생성한다. SDK의 기본 Build는
  `AppHostDotNetSearch`를 적용하지 않아 시스템 .NET을 사용했으며, 생성 단계를 보완하고 실행 추적으로 재검증했다.
- 큰 에셋 목록은 AssetCooker의 UTF-8 인자 파일로 전달한다. 셸 해석 없이 한 줄을 한 인자로 읽는다.
  cooker 작업 디렉터리도 명시한다.

사용법: [BuildTool/README.md](../../BuildTool/README.md).

## 통과한 검증

| 항목 | 결과 |
|---|---|
| BuildTool Debug·Release 빌드 | 오류 0, 경고 0 |
| VS18 MSBuild의 BuildTool 프로젝트 직접 빌드 | 통과 |
| CreatorEditor Debug 빌드 | 변경한 게임 빌드·스크립트 컴파일 호출을 포함해 통과 |
| BuildTool 회귀 | `BUILD_TOOL_TESTS_OK checks=47` |
| 기존 stage 경계 검사 | reparse·extended/device·8.3 alias 거부, 입력 sentinel 보존 |
| 기존 엔진 배포 검사 | `ENGINE_DISTRIBUTION_VERIFIED ... checks=45` |
| PowerShell 호환 진입점 | 6개 파일 구문 검사 통과 |
| 배포본의 도구·런타임 | app-relative private .NET 사용을 host trace로 확인 |
| 검증용 게임 | Debug/DX12 compile → cook → PAK → Player → publish 통과 |

47개 회귀는 경로 이탈·reparse·중복/변조 payload·엔진 pin·PAK 재열거·Player 실패 판정·원자 게시·
하위 프로세스 timeout 정리, 실제 Roslyn 컴파일, 컴파일 실패 시 이전 DLL 보존, prebuilt DLL 변조 거부,
배포 apphost의 private runtime 선택과 JSON 출력을 검사한다. 한글·공백·`&`가 있는 프로젝트 경로를 포함한다.
컴파일 실패 테스트의 C# 오류 출력은 의도한 음성 검증이다.

에디터 빌드에서 기존 `LNK4229`(`/DELAYLOAD:vulkan-1.dll`) 경고가 있었으며 이번 변경으로 수정하지 않았다.
에디터 버튼 클릭·취소 UI의 실제 조작 검증은 수행하지 않았다.

## 실제 패키지 근거

엔진은 로컬 개발용 `0.0.0.0 / preview`, 배포 ID는 `aa49870f-7f86-4997-a675-da94685a6b23`이다.
배포본 위치는 `Build/Tests/BuildToolDistributions/local-0.0.0.0-win-x64-Debug-<배포 ID>`다.
최종 배포에서 795개 파일과 PowerShell/PS1 파일 0개를 확인했다.
작업 공간의 기본 엔진 선택 포인터는 변경하지 않았다.

입력은 `Build/Tests/BTGame`의 독립 사본이다. `Dynamic_CPP`의 Assets·ProjectSetting·model generation을
복사하고, 해석할 수 없는 참조가 있는 `Test1.creator`/`Test2.creator`를 이 사본에서만 제외했다.
`GameScripts/PackageSmokeProbe.cs`를 사본의 스크립트 입력에 추가했다.
성공한 시작 씬 `BuildToolSmoke.creator`는 `FT_Primitives`의 루트·카메라·조명 세 Entity만 남겨 만든
검증 씬이며 모델 MeshRenderer를 포함하지 않는다. 씬 ID는 별도로 부여했다.

실행 시 `PATH`에는 Windows/System32만 남기고 `DOTNET_ROOT`는 없는 경로로 설정했다.
배포본의 EXE를 사용했으며 시작부터 끝까지 JSON Lines 59개가 정상 파싱됐다.

| 패키지 항목 | 실측 |
|---|---|
| 게시 디렉터리 | `Build/Tests/BTP/BTGame-3fd4703992714cf79ea7cb81df0bd0e8` |
| current pointer | `Build/Tests/BTP/BTGame.current.json` |
| 콘텐츠 / 런타임 파일 | 532 / 229 |
| cook | 모델 generation 14개, artifact 262개, CEDO 문서 19개 |
| Player | exit 0, GT 10870, display 2, promotions 2 |
| 런타임 입력 | CEMF identities 238, cooked entries 444, `.meta` 파싱 0, text parser 0 |
| 관리 스크립트 | 1종 등록, 초기화·시뮬레이션 시작 marker 각각 1회 |
| 게시 판정 | `verification=passed`, 추출 파일 해시 및 실행 payload 불변 검사 통과 |

`managedLifecycle` manifest 필드는 고정 FT fixture의 필수 판정 여부이므로 이 씬에서는 false다.
위 두 lifecycle marker는 JSON 로그에서 별도로 확인했다. 모델 14개의 **가공** 통과를 모델 **렌더링**
통과로 해석하지 않는다.

로그: `Build/Tests/buildtool-tests.log`, `buildtool-editor-debug-build.log`,
`buildtool-distribution-check.log`, `buildtool-private-package.jsonl`.

## 남아 있는 검증과 기존 실행 문제

- 원본 프로젝트의 Test1/Test2에는 해석되지 않는 dependency GUID가 있다. cook이 이를 거부했고 게시하지 않았다.
- `FT_Primitives`의 모델 렌더링 gate는 실패했다. 쿠킹된 `ProjectSetting/AssetIdentity.asset`을
  `ModelAssetGeneration.cpp::LoadModelAssetGeneration`의 `ReadText` → `ReadIdentityEpochHeader` 경로가
  텍스트로 읽는다. 당시 로그는 identityHeader 오류와 text-parser 8회를 기록했다.
  BuildTool은 이 실패를 우회하지 않으며 해당 candidate를 게시하지 않았다.
- Release·Shipping 게임 실행, Vulkan, Tracked clean checkout, clean VM은 이번 검증 범위가 아니다.
- Editor 게임 빌드는 기존 동기 대기를 유지한다. 진행률·취소 UI는 B2 운영성 후속 범위다.
- 정식 프로젝트 descriptor·Launcher·MSI·공식 버전 발행은 PHASE 23의 별도 미완료 범위다.

따라서 **EXE 이관과 제한된 게임 패키지의 전체 흐름은 검증했고, 기존 모델 씬을 포함한 모든 게임의
배포 준비 완료는 선언하지 않는다.**


## 2026-09-20 Player Job 검증용 패키지 갱신

- 현재 렌더러는 `WorldSprite.slang`을 읽지만 BuildTool 입력 검사는 은퇴한
  `WorldSprite.hlsl`을 요구했다. 실제 소비 파일로 검사를 바꾸고 Slang 성공/legacy-only
  거절 회귀를 추가했다. Release BuildTool 빌드 0경고·0오류, 테스트 44개 통과.
- 과거 BTGame 콘텐츠는 Decal.slang 누락과 model artifact v8/v9 차이로 재사용할 수 없었다.
  격리 MinimalFixture에 최신 셰이더·CreatorRobot generation 3과 기존 카메라/조명/관리
  스크립트 씬을 넣어 현재 EXE로 재패키징했다. 원본 프로젝트·기본 배포 포인터는 보존했다.
- Release/DX12 package-game 전체 단계와 Player 표시 슬롯 회전 검증 통과.
  `Build/Obj/Phase13Jobs/PlayerRuntime/Packages/MinimalFixture.current.json`은
  `Game-6a91b88da394424c9f66447279e10cf4`, `verification=passed`를 가리킨다.
  로그는 같은 PlayerRuntime 폴더의 `package5.log`다.
- 모델은 패키지 cook 입력에만 포함된다. 시작 씬은 모델을 그리지 않으므로 모델 렌더링,
  Vulkan/Shipping/clean VM 검증으로 확대하지 않는다. 비동기 씬 전환·종료 검증은
  TaskSchedulerUnificationPlan의 별도 Player gate에 기록한다.
