# PHASE 23 Launcher 착수 기준선

- 확인일: 2026-09-23
- 범위: DL0 선행 조사. 이 문서는 Launcher, descriptor, MSI의 구현 완료나 실행 검증을 뜻하지 않는다.
- 정본: [EngineDistributionAndLauncherPlan](../plans/EngineDistributionAndLauncherPlan.md), [EngineVersionPolicy](../design/EngineVersionPolicy.md)
- 확인한 checkout: `e98a8d4b716466c4735620ba562d53bbe95746ae`. 다른 작업의 변경이 있는 작업 트리에서 PHASE 23 관련 소스만 읽었다.

## 현재 연결점

| 경계 | 소스에서 확인한 상태 | Launcher 착수에 필요한 일 |
|---|---|---|
| Editor 진입 | `Editor/EngineEntry/App.cpp`의 `--project`는 미구현 오류를 낸다. `--development-project` 또는 checkout의 `Dynamic_CPP` 탐색으로 root를 정한다. | DL1에서 descriptor를 GUI 초기화 전에 검증하고 절대 root를 `EnginePaths`에 주입한다. 인자 누락·중복·상대 경로를 오류로 구분한다. |
| BuildTool 진입 | `BuildTool/Program.cs`의 `open-project`는 디렉터리와 설정별 `Engine.<Config>.lock.json`을 받으며 Editor에 `--development-project`를 전달한다. | 제품용 `--project <descriptor>` 경로를 개발 adapter와 분리한다. 기존 개발 명령은 이관 완료 전까지 명시적 개발 경로로 취급한다. |
| 배포 검증 | `EngineDistribution.Load`가 manifest의 UUID, payload digest, 파일별 해시, 미기록 파일, native 정보 일치를 검사한다. `EnginePublisher`가 설정별 배포를 만든다. | 설치 inventory는 디렉터리 이름만으로 등록하지 않고 이 검증을 재사용한다. manifest의 `version + buildId`를 descriptor와 정확히 비교한다. |
| 배포 배치 | 현 개발 배포는 `Bin/x64-<Config>/Editor/CreatorEditor.exe`와 private .NET/BuildTool을 포함한다. 계획의 `%ProgramFiles%/Engines/<version>/Bin/CreatorEditor.exe`는 목표 배치의 개략도다. | DL5/DL6 전에 설치 layout과 manifest `binaryRoot`의 단일 계약을 정한다. Launcher에서 EXE 경로를 임의 문자열로 조합하지 않는다. |
| 프로젝트 내용 | `GamePackager`와 `PackageInputs`는 `Assets`, `ProjectSetting`, 프로젝트 폴더명과 저장소 전용 Workspace/Tracked 모드에 의존한다. | descriptor의 root 정의와 기존 `ProjectSetting`을 잇는 adapter를 먼저 만든다. 프로젝트 ID를 폴더명 대신 사용한다. |
| Editor 경로 | `EnginePaths`는 Host가 제공하는 root를 받을 수 있다. 다만 `ResolveProcessExecutableDirectory`는 `MAX_PATH` 크기의 버퍼를 사용한다. | 설치 및 Unicode/긴 경로 fixture에서 Host 경로 해석을 검증하고, 잘린 실행 경로는 명시적으로 실패시킨다. |
| 감시 | `EditorAssetDatabase`는 현재 단일 efsw root를 사용한다. | DL2/DL3에서 Editor가 다중 root와 callback queue를 소유한다. Launcher는 asset watcher를 갖지 않는다. |

## 로컬 배포 관찰

`Build/Distributions`에서 Debug와 Release 개발 배포 각 1개를 확인했다. 둘 다 `version=0.0.0.0`, `localDevelopment=true`, `channel=preview`, `scriptApi=24`, `hostAbi=1`이다. 이 값은 발행된 제품 버전이나 설치된 엔진 inventory가 아니다. `Dynamic_CPP`에는 `*.creatorproject`가 없다. 검사는 manifest 읽기와 소스 조사에 한정하며 현재 배포본을 이번 조사에서 다시 빌드하거나 실행하지 않았다.

## 첫 제품 경로의 작업 순서

1. **DL0 실패 기준 고정:** 정상/깨진 descriptor, 틀린 `buildId`, 누락/변조 manifest, 다른 프로젝트 ID가 같은 index에 들어오는 경우를 fixture로 둔다. 기존 `BuildTool/Tests/Program.cs`에는 배포 변조와 개발 pin 불일치 거부가 있지만 제품 descriptor와 MSI 제거 보존 canary는 없다.
2. **DL1 descriptor와 Host:** UTF-8 schema v1을 읽고 UUID, 정확한 엔진 빌드와 배포 ID, 상대 root의 프로젝트 내부 경계를 검증한다. `--project`로 Editor를 직접 실행할 때도 같은 검증이 적용되어야 한다. Launcher가 먼저 검증했다는 사실만 믿지 않는다.
3. **DL4 최소 Launcher:** Import, 목록 제거, exact engine resolve, Open을 먼저 연결한다. 목록 제거의 변경 대상은 사용자 index뿐이다. Create는 template stage→검증→publish가 준비된 다음 연결한다.
4. **DL5/DL6 설치 경계:** 설치된 manifest와 Windows Installer 등록 정보를 대조하고 engine payload를 읽기 전용으로 취급한다. 현재 개발 배포를 설치 완료로 간주하지 않는다.

## DL0에서 결정하거나 검증할 항목

| 항목 | 다음 판정에 필요한 증거 |
|---|---|
| Launcher와 engine의 설치 scope·MSI 식별자 | Launcher 업그레이드와 엔진 두 빌드의 동시 설치·개별 복구·개별 제거 fixture. 프로젝트 파일 digest 전후 비교. |
| 네 자리 엔진 빌드와 MSI 버전 | [Windows Installer는 `ProductVersion`의 앞 세 필드만 비교한다](https://learn.microsoft.com/en-us/windows/win32/msi/productversion). `Revision`만 다른 배포 두 개의 identity·설치 경로·등록 정보 매핑을 실제 MSI로 검증한다. |
| 채널과 정확한 고정 | `version + buildId`가 실행 배포 선택의 기준이다. 채널은 제안/표시 정보이며 Preview→Stable 승격 때 동일 payload를 어떻게 표시할지 fixture로 고정한다. |
| 엔진 catalog 신뢰 | 설치 등록, manifest, payload digest, 서명 검증의 책임과 오류 종류를 분리한다. 서명이 없는 로컬 개발 배포를 제품 catalog에 자동 등록하지 않는다. |
| clean VM 전제 | repository, VS, vcpkg, 전역 .NET SDK가 없는 VM에서 Launcher→Editor 실행에 필요한 OS/runtime 의존을 계측한다. 이 checkout의 배포 검증을 clean VM 증거로 바꾸어 말하지 않는다. |

## 현재 판정

- DL0: 소스 및 로컬 배포 기준선을 확보했으나 설치 topology, 서명, descriptor/MSI 실패 fixture와 clean VM 전제가 확정되지 않았다. 완료 표시 불가.
- DL1/DL4/DL6: 제품 descriptor, Launcher executable, MSI가 없으므로 미착수.
- DL5: 개발 배포 경로만 부분 구현. 정식 descriptor 선택, 제품 설치, 공식 발행, PHASE 22 오디오 종속 제거는 별도 게이트다.
