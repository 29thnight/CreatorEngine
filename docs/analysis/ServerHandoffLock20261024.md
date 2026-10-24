# 서버 사전 작업 인계 고정 — 2026-10-24

**검증한 서버 사전 작업의 소스·F# 도구·SDK·fixture·기록을 고정했다.** 네이티브 소스는 `b5d6e3680999ba9bb42c30114040b7c569fdcf94`이며, 이 문서와 lock을 담는 후속 커밋은 인계 메타데이터/검증 스크립트의 커밋이다. 두 리비전의 역할을 구분한다. 실제 2-client 서버·transport·운영 배포·soak 수용은 별도다.

[기계 판독 lock](ServerHandoffLock20261024.json) · [작업 순서와 완료 기준](NetworkWorkerHandoff20261024.md) · [실행 계약](../design/HeadlessSceneRuntimeContract.md) · [대시보드](../RefactoringPlanDashboard.html#phase-20).

## 고정한 식별자

| 항목 | 고정 값 |
|---|---|
| CreatorEngine 소스 | `b5d6e3680999ba9bb42c30114040b7c569fdcf94` / 서버 사전 작업 93파일 선별 커밋 |
| F# 도구 | `3351cbc80d7216eb10110c4c8b30dce98aef1d1f` / 완전한 Git bundle |
| SDK build ID | `7ea251ca-076d-4ec9-9fbe-a923b3fcbc5c` |
| SDK 구성 | Windows x64 Release, Script API45, host ABI1, content ABI creator-content-v1 |
| SDK 출처 | 위 소스 커밋의 깨끗한 checkout에서 네이티브 재빌드, manifest의 dirty=false |
| 파일 원장 | lock의 artifacts 9개에 bytes/SHA256 기록; SDK 내부 805개 파일도 검증 |
| 인계 자료 폴더 | `Build/Handoff/Server-b5d6e3680/Payload` |

원격 Git에는 소스와 문서·lock·검증 스크립트를 게시한다. SDK와 대용량 fixture/증거 archive는 이 로컬 인계 폴더에 고정했으며 별도 전달 대상이다. 원격 release asset 업로드를 수행한 것으로 표기하지 않는다. 기존 Effect·모델·workspace 변경은 이 소스 커밋에서 제외했고 원본 바이트를 보존했다.

## 인계 자료

| 파일 | 용도 |
|---|---|
| engine-source-b5d6e3680.zip | 해당 커밋의 전체 저장소 스냅샷. dependency cache와 외부 F# 저장소는 포함하지 않음 |
| engine-changes-b5d6e3680.bundle | 소스 커밋과 변경 객체. 기준 71504c4fd610a205c8c857538364e1d392e9e150이 있는 Git 저장소에 fetch |
| fsharp-tools-3351cbc80d.bundle | 고정된 main ref와 전체 이력. sibling 저장소 복원 |
| engine-sdk.zip | 고정 소스에서 만든 SDK. engine.manifest.json의 build ID·payloadDigest·모든 파일 검증 |
| headless-scene-fixture.zip | 실제 Scene/Entity 무렌더 host·CPU PhysX·관리 fixture·cooked 씬 |
| fsharp-cct-player-fixture.zip | 새 SDK로 package/smoke·고정틱/native read/write·정상 종료를 통과한 cooked Player |
| fixture-shader-meta-seed.zip | 제품 fixture 생성에 필요한 Git 제외 셰이더 .meta 126개. fresh source의 Dynamic_CPP/Assets/Shaders에 먼저 적용 |
| validation-evidence.zip | 이전/고정 소스의 로그·JSON·map·imports·GPU 이미지·manifest 등 1172파일과 portable evidence-index.json |
| verify-server-handoff.ps1 | 자료 폴더의 파일 크기/SHA256 검사. lock 사본과 함께 전달 |

Player fixture는 ZIP root 자체가 실행 stage다. headless와 Player archive의 런타임 파일을 서로 섞지 않는다. evidence-index.json은 primary/pinned 상대 경로로 원래 관찰 위치를 구분한다. 로그 안의 과거 절대 경로는 관찰 당시 위치이며 수신자의 설치 경로가 아니다. 폐기한 중간 산출물·의존 cache·이전 SDK/AnimationPlayer 전체 바이너리까지 포함한 archive는 아니다. 이전 SDK/애니메이션 소비자 관찰은 원장/로그로 보존하고 현재 SDK build ID의 검증과 구분한다.

## 수신 후 확인과 재현

먼저 인계 폴더에 있는 검증 스크립트와 lock 사본으로 파일을 검사한다. PowerShell 5와 pwsh를 지원한다.

```powershell
./Payload/verify-server-handoff.ps1 -ArtifactRoot ./Payload -LockPath ./Payload/ServerHandoffLock20261024.json
```

고정 소스는 원격 저장소에서 해당 SHA를 checkout하거나 source ZIP으로 복원한다. 후자는 Git 이력을 포함하지 않는다. F# 저장소는 source의 sibling에 둔다.

```powershell
git clone --branch main ./Payload/fsharp-tools-3351cbc80d.bundle ../FSharp_Tool_Pakage_for_CreatorEngine
Expand-Archive ./Payload/fixture-shader-meta-seed.zip -DestinationPath <fresh-source>/Dynamic_CPP/Assets/Shaders
Expand-Archive ./Payload/engine-sdk.zip -DestinationPath <SDK>
```

메타데이터 seed는 깨끗한 fixture 입력용이며 기존 사용자 프로젝트에 덮어쓰는 용도가 아니다. 셰이더 코드는 소스 커밋과 동일하고, 이전 수용 fixture의 누락된 .meta만 126개 고정했다. Git source가 깨끗해도 이 ignored 입력은 별도로 필요하다. **Editor/Player 제품 fixture를 생성하기 전에 적용한다.**

SDK를 펼친 뒤 아래 명령으로 SDK 자체를 검증한다.

```powershell
<SDK>/Bin/x64-Release/Tools/CreatorBuildTool/CreatorBuildTool.exe verify-engine --engine-distribution <SDK>
```

소스 재현에는 MSVC v145/C++23·Windows SDK·.NET 10 SDK·vcpkg/NuGet dependency가 필요하다. runtime 실행에는 설치된 .NET 10 x64와 MSVC runtime이 필요하다. SDK 패키징의 reparse-point 검사를 유지하기 위해 이번 고정 빌드는 vcpkg 설치 cache를 checkout 안의 실제 파일로 복사했다. cache 없는 clean VM·Linux·컨테이너·self-contained 배포 수용은 아니다.

필수 회귀 명령과 각 작업의 완료 기준은 [최종 인계 점검](NetworkWorkerHandoff20261024.md)을 따른다. 제품 gate의 CookedStage에는 전달한 F# Player fixture를 펼친 stage를 사용하거나 고정 SDK로 새 fixture를 만든다. 신규 검증의 산출물을 기존 수용 로그에 덮어쓰지 않는다.

## 이번 고정 검증

- 깨끗한 소스에서 Editor·Player·AssetCooker·AssetPacker·ProfilerViewer 및 SDK 빌드/패키징을 완료했다.
- 고정 SDK의 실제 F# CCT Player와 NetworkHandoff All -Product **17그룹**을 재실행해 통과했다. 여기에는 실제 Editor 30/60/144fps·moving GPU와 headless Release **19프로세스**가 포함된다.
- 별도 원본 Debug/Release/새 checkout host 57건의 관찰 기록도 보존한다. 이번 Release 재검증을 Debug 전체 재실행으로 표기하지 않는다.
- archive의 파일 크기/SHA256과 ZIP 6개의 CRC를 검사했다. 손상된 SHA의 음성 canary는 실패했다. 압축 해제한 headless archive도 실제 120틱을 실행해 GC 객체/루트/스크립트 핸들 0과 물리 resource balance를 확인했다.
- 첫 SDK 패키징의 junction 거부와 첫 F# package의 ignored .meta 누락은 실패/재시도 로그로 보존한다. archive 생성 뒤 발견한 PowerShell 5 기본 lock 경로 초기화 차이는 검증 스크립트에서 수정하고 최종 실제 자료 검사로 확인했다. 검사나 엔진 계약을 완화하지 않았다.

N0 → N2/N4 → N5 → N6 → N7 → N9 → N10의 순서와 콘텐츠별 provider 연결 조건은 그대로다. 로컬 handle/schema를 wire에 전송하거나 F# 예외 격리를 Verse식 세계 rollback으로 해석하지 않는다. 원격 CI 배선과 원격 실행 PASS도 구분한다.
