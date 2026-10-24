# CreatorEngine 데디케이티드 서버 개발 인계서

2026-10-24 · 수신: 데디케이티드 서버 개발 담당자

공통 런타임의 서버 사전 작업을 인계합니다. 기존 SceneManager·Scene·Entity·Component를 사용해 렌더 없이 cooked 씬을 실행하고, CPU 물리와 F# 고정 틱을 구동하는 기반을 준비했습니다. 이 기반 위에 **wire ID/schema, transport, replication, 실제 서버 프로그램과 운영 검증**을 구현하는 것이 인계 범위입니다.

첫 착수는 **N0 계측 → N2 ID/schema와 N4 loopback 병행 → N5 생성·파괴/full snapshot**입니다. 아래의 N 번호는 대시보드 PHASE 20의 작업 ID입니다. 실제 서버의 2-client 동기화·connection drain·배포·soak는 앞으로 완료해야 합니다.

## 1. 받을 자료와 고정 버전

| 항목 | 고정 값 / 받는 위치 |
|---|---|
| 엔진 저장소 | [29thnight/CreatorEngine](https://github.com/29thnight/CreatorEngine) |
| 개발 기준 소스 | [`6752bf2db16d5a5220c00b5ac667659abafaa4d1`](https://github.com/29thnight/CreatorEngine/tree/6752bf2db16d5a5220c00b5ac667659abafaa4d1) |
| 인계 lock·검증 스크립트 기준 | [`be057dca750fa2618076584d6c08e1e5d6ee61f6`](https://github.com/29thnight/CreatorEngine/commit/be057dca750fa2618076584d6c08e1e5d6ee61f6) |
| F# 도구 저장소 | [29thnight/FSharp_Tool_Pakage_for_CreatorEngine](https://github.com/29thnight/FSharp_Tool_Pakage_for_CreatorEngine/tree/3351cbc80d7216eb10110c4c8b30dce98aef1d1f) |
| F# 도구 리비전 | `3351cbc80d7216eb10110c4c8b30dce98aef1d1f` |
| SDK build ID | `5f76f908-9df7-493e-8f39-5d24656a142a` |
| SDK 구성 | Windows x64 / Release / Script API45 / host ABI1 / content ABI `creator-content-v1` |

SDK는 위 소스 리비전의 깨끗한 checkout에서 재빌드했습니다. `engine.manifest.json`의 `source.revision`은 `6752bf2db1…`, `source.dirty`는 `false`입니다. 개발 기준을 복원할 때에는 최신 master 대신 표의 소스 SHA를 사용하십시오. 인계 lock을 담은 후속 커밋과 SDK가 빌드된 소스 커밋은 역할이 다릅니다.

### 발신자가 함께 전달할 폴더

이 문서와 **Payload 폴더 전체**를 함께 전달해야 합니다. Git clone만으로는 SDK·cooked fixture·검증 archive를 받을 수 없습니다.

- 발신자 PC의 자료 위치: `C:\Users\idene\source\repos\CreatorEngine\Build\Handoff\Server-6752bf2db1\Payload`
- 위 경로는 발신자 로컬 경로입니다. 수신자는 별도로 복사받아 아래 예시의 `C:\CreatorServerHandoff\Payload`처럼 배치합니다.
- 원격 Git에는 소스·문서·lock·검증 스크립트가 게시돼 있습니다. 바이너리 자료의 원격 다운로드 URL은 제공되지 않았습니다. 자료를 받지 못했으면 발신자에게 Payload 폴더를 요청하십시오.
- SHA256 정본: [고정 커밋의 ServerHandoffLock20261024.json](https://github.com/29thnight/CreatorEngine/blob/be057dca750fa2618076584d6c08e1e5d6ee61f6/docs/analysis/ServerHandoffLock20261024.json). Payload 안의 동명 사본과 함께 보관합니다.

| Payload 파일 | 용도 |
|---|---|
| `engine-source-6752bf2db1.zip` | 고정 소스 스냅샷. Git 이력·외부 dependency는 포함하지 않음 |
| `engine-changes-6752bf2db1.bundle` | 기존 Git 저장소에 가져올 변경 객체. 기준 커밋 `8bb1abfe1d45d41a497156a5ae9ed31453954e6f` 필요 |
| `fsharp-tools-3351cbc80d.bundle` | 고정된 F# 도구의 전체 Git 이력과 main ref |
| `engine-sdk.zip` | SDK. 펼친 폴더의 root에 `engine.manifest.json`이 있음 |
| `headless-scene-fixture.zip` | 실행 가능한 무렌더 host·CPU PhysX·F# 어셈블리·cooked 씬 |
| `fsharp-cct-player-fixture.zip` | 고정 SDK로 검증한 F# CCT Player stage |
| `fixture-shader-meta-seed.zip` | 제품 회귀 입력용 셰이더 `.meta` 126개 |
| `validation-evidence.zip` | 검증 기록 1,518개와 `evidence-index.json` |
| `verify-server-handoff.ps1` | 위 고정 자료의 크기/SHA256 검사 |

추가로 `ServerHandoffLock20261024.json`, `README.md`, 이 인계서를 전달합니다. lock은 9개 payload 파일의 크기와 SHA256을 지정합니다. 파일 이름이나 SDK/fixture 내부 파일을 바꾸거나 다른 빌드의 DLL을 섞지 마십시오.

## 2. 환경 준비와 첫 실행

### 필요한 환경

- 전달한 host/SDK 실행: Windows x64, .NET 10 x64 runtime, MSVC x64 runtime. 검증에 사용한 .NET runtime은 `10.0.13`입니다.
- 소스 빌드: Visual Studio 18 계열 C++ 워크로드, MSVC v145/C++23, Windows SDK, .NET SDK `10.0.402`, Git, PowerShell 7, Python, vcpkg/NuGet dependency.
- 전체 Editor/Player 제품 회귀: DX12 지원 GPU와 드라이버. **무렌더 fixture의 단독 실행에는 렌더 장치가 필요하지 않습니다.**
- F# 도구 폴더는 엔진 소스의 sibling인 `FSharp_Tool_Pakage_for_CreatorEngine`에 둡니다.

의존성 설치는 고정 소스의 `vcpkg.json`과 `ports`를 따릅니다. 기존 재현에서는 dependency/NuGet cache를 재사용했습니다. cache 없는 clean VM 설치, Linux·컨테이너·self-contained 배포는 아직 수용하지 않았습니다.

### ① 수신 자료 검사

PowerShell 7에서 실행합니다. 아래에서는 새 작업 폴더를 `C:\CreatorServerHandoff`로 가정합니다. 다른 경로를 쓰면 첫 변수만 바꿉니다.

```powershell
$ErrorActionPreference = 'Stop'
$handoffRoot = 'C:\CreatorServerHandoff'
$payloadRoot = Join-Path $handoffRoot 'Payload'

& "$payloadRoot\verify-server-handoff.ps1" `
    -ArtifactRoot $payloadRoot `
    -LockPath "$payloadRoot\ServerHandoffLock20261024.json"

if (-not $?) { throw '인계 자료 검사가 실패했습니다.' }
```

기대 결과는 `SERVER_HANDOFF_ARTIFACTS_OK`, artifacts `9`, 위 표의 소스/F# 리비전과 SDK build ID입니다. 크기나 SHA256이 다르면 해당 자료를 다시 받아야 합니다. 이후 압축 해제는 기존 개발 폴더가 아닌 새 폴더에 합니다.

### ② SDK·fixture·기록 펼치기

```powershell
$sdkRoot = Join-Path $handoffRoot 'EngineSDK'
$headlessRoot = Join-Path $handoffRoot 'HeadlessFixture'
$playerRoot = Join-Path $handoffRoot 'FSharpPlayerFixture'
$evidenceRoot = Join-Path $handoffRoot 'Evidence'

Expand-Archive -LiteralPath "$payloadRoot\engine-sdk.zip" -DestinationPath $sdkRoot
Expand-Archive -LiteralPath "$payloadRoot\headless-scene-fixture.zip" -DestinationPath $headlessRoot
Expand-Archive -LiteralPath "$payloadRoot\fsharp-cct-player-fixture.zip" -DestinationPath $playerRoot
Expand-Archive -LiteralPath "$payloadRoot\validation-evidence.zip" -DestinationPath $evidenceRoot

& "$sdkRoot\Bin\x64-Release\Tools\CreatorBuildTool\CreatorBuildTool.exe" `
    verify-engine --engine-distribution $sdkRoot

if ($LASTEXITCODE -ne 0) { throw 'SDK 검사가 실패했습니다.' }
```

SDK build ID는 `5f76f908-9df7-493e-8f39-5d24656a142a`여야 합니다. Player fixture는 압축 해제한 폴더 자체가 실행 stage입니다.

### ③ 컴파일 없이 무렌더 실행 확인

아래 실행은 60Hz 입력 120프레임, 고정 틱 120회를 확인합니다. 입력 프레임 간격을 합성한 기능 검사이며 FPS 성능 측정은 아닙니다.

```powershell
$savedProbeSetting = $env:CE_PHYSICS_RESOURCE_PROBE
try {
    $env:CE_PHYSICS_RESOURCE_PROBE = '1'

    & "$headlessRoot\HeadlessHost.exe" `
        --scene "$headlessRoot\Scene.cedo" `
        --managed "$headlessRoot\Managed" `
        --report "$handoffRoot\headless-smoke.json" `
        --frames 120 `
        --frame-seconds 0.016666666666666666 `
        --inspect-type CreatorEngine.Scripts.HeadlessTickProbe `
        --inspect-method State

    if ($LASTEXITCODE -ne 0) { throw '무렌더 fixture 실행이 실패했습니다.' }
} finally {
    $env:CE_PHYSICS_RESOURCE_PROBE = $savedProbeSetting
}
```

`headless-smoke.json`에서 `result=HEADLESS_SCENE_OK`, `scene.committedTicks/managedBatches/physicsSteps=120`, `scene.liveObjects/roots/liveScriptHandles=0`, `physicsResources.balanced=true`를 확인합니다. headless archive를 압축 해제한 상태에서도 이 검사를 통과했습니다.

### ④ 개발 소스와 F# 도구 복원

전체 회귀는 Git 기준 비교를 사용하므로 개발용 소스는 Git checkout을 권장합니다. source ZIP은 소스 열람·복원용이며 Git 이력을 포함하지 않습니다.

```powershell
$workRoot = Join-Path $handoffRoot 'Work'
$engineSource = Join-Path $workRoot 'CreatorEngine'
$fsharpSource = Join-Path $workRoot 'FSharp_Tool_Pakage_for_CreatorEngine'
New-Item -ItemType Directory -Force $workRoot | Out-Null

if ((Test-Path $engineSource) -or (Test-Path $fsharpSource)) {
    throw '소스 복원은 비어 있는 새 작업 폴더에서 실행하십시오.'
}

git clone https://github.com/29thnight/CreatorEngine.git $engineSource
if ($LASTEXITCODE -ne 0) { throw '엔진 저장소 복제 실패' }
git -C $engineSource checkout --detach 6752bf2db16d5a5220c00b5ac667659abafaa4d1
if ($LASTEXITCODE -ne 0) { throw '고정 소스 checkout 실패' }

git clone --branch main "$payloadRoot\fsharp-tools-3351cbc80d.bundle" $fsharpSource
if ($LASTEXITCODE -ne 0) { throw 'F# 도구 복원 실패' }
git -C $fsharpSource rev-parse HEAD

Expand-Archive -LiteralPath "$payloadRoot\fixture-shader-meta-seed.zip" `
    -DestinationPath "$engineSource\Dynamic_CPP\Assets\Shaders"
```

F# 도구의 HEAD는 `3351cbc80d7216eb10110c4c8b30dce98aef1d1f`여야 합니다. seed는 Git에서 제외된 셰이더 메타데이터입니다. **Editor/Player 제품 fixture를 만들기 전에** 깨끗한 소스 폴더에 적용하십시오. 기존 사용자 프로젝트의 메타데이터를 덮어쓰는 용도로 사용하지 않습니다.

## 3. 준비된 기반과 구현할 범위

| 준비된 기반 | 구현할 작업 |
|---|---|
| 계약 헤더 6종, 변경 절차, wire 금지 타입/원장 검사 | 실제 registry·transport·schema·권한 정책 |
| 엔티티 생성/파괴·DDOL·일반 쓰기 관찰자 | 관찰 결과를 wire ID와 replication 상태로 변환 |
| 공통 60Hz clock, 명령 적용, 성공 커밋 snapshot | 입력·상태 동기화, full/delta snapshot, late join |
| 기존 Scene/Entity를 쓰는 무렌더 host, CPU 물리·F# 수명 | 접속과 운영을 갖춘 실제 DedicatedServer 프로그램 |
| Editor/Player/무렌더 회귀와 실패 canary | 새 네트워크·서버 기능의 필수 회귀 및 운영 soak |

추가 사내 선행 구현 잔여는 **현재 수용한 Windows 콘텐츠 범위**에서 닫았습니다. 실제 서버 콘텐츠가 추가 컴포넌트·project layer·asset bundle·외부 collision geometry를 요구하면 엔진 담당자와 reader/provider를 연결하고 그 콘텐츠를 별도 수용해야 합니다. 현재 기본 reader는 Transform·PhysicsBodyComponent·CharacterMovementComponent·ScriptComponent입니다. 미설치 기능이 필요한 씬은 입장 오류로 처리합니다.

## 4. 착수 순서와 단계별 완료 기준

| 순서 / ID | 산출물 | 완료 기준 |
|---|---|---|
| 1 / N0 | 기준선·telemetry·실패 검사 | 부하와 함께 tick 비용, encode/decode, bytes/tick, 복사/할당, queue depth/drop, RTT/loss 기록. malformed·포화·실패 주입 시 검사가 실제 실패할 것 |
| 2A / N2 | wire ID/schema·권한·registry | 로컬 handle/typeID/token과 wire ID 분리. ID 원장·tombstone·버전·field schema 확정. stale/reused ID·비권한 쓰기 거부. DDOL Rebind revision·expected binding 검증 |
| 2B / N4 | bounded loopback·framing·handshake | 손실/재정렬/중복/손상, unknown message, 초과 길이, 작은 수신 버퍼, 포화 검증. Send 접수와 상대 적용 ack 구분. drain 뒤 잔여 callback·누수 없음 |
| 3 / N5 | spawn/despawn·full snapshot·late join | N2/N4 수용 후 통합. 두 endpoint의 동일 커밋 서수에서 권한 상태 수렴. 늦은 접속·씬 교체·DDOL·재접속 검증. 오래된 snapshot으로 파괴 개체가 부활하지 않을 것. 미완료 커밋 결과를 전송하지 않을 것 |
| 4 / N6 | delta·interest·quantization·backpressure | full 기준과 delta 복원 비교. baseline 유실 시 full 재동기화. 오차·대역폭·CPU·할당 예산을 정하고 통과. 느린 peer에서도 메모리/큐 상한 유지 |
| 5 / N7 | 실제 transport 비교·adapter | 동일 fixture·부하·실패 조건으로 비교해 선택 근거 제출. 라이브러리는 INetworkTransport 뒤에 격리. 실제 소켓의 접속·종료·패키지 의존을 수용 |
| 6 / N9 | 실제 DedicatedServer 제품 | 실제 산출물로 cooked 시작 → 렌더 장치 없는 tick/CPU 물리/F# → 실제 2-client 상태 수렴 → 정상 종료/connection drain. import/map/패키지/로드 모듈의 렌더·Editor·저작 parser 의존 차단 |
| 7 / N10 | hardening·운영 관측·soak | 접속·payload/rate·큐·메모리·지연·오류율·시험 시간/부하를 먼저 명시. 반복 reconnect·손실/포화·실패 복구·shutdown을 포함해 장시간 예산 통과 |
| 선택 / N8 | prediction/reconciliation | gameplay 요구가 확정될 때 착수. 보정 오차, tick history/replay 범위와 비용을 별도로 수용 |

N2와 N4는 N0 이후 병행할 수 있습니다. N5는 양쪽 계약 수용 뒤 통합합니다. 서버 bootstrap과 실패 하네스는 일찍 만들 수 있지만, N9 제품 완료와 N10 최종 soak는 통합한 실제 서버로 판정합니다.

### 엔진 담당자와 공동 확정할 사항

- 실제 서버 씬의 컴포넌트·layer·외부 geometry/asset bundle 목록과 필요한 서비스.
- quantizer·snapshot 배치·backpressure 정책과 수치. N0/N5 실측 후 제안하고 계약 변경 절차로 확정합니다.
- 실제 transport 의존성을 추가할 때의 vcpkg 매니페스트와 패키징 변경.
- 운영 OS·배포 방식·목표 접속 수·부하·지연/오차 예산. 현재 자료의 Windows 기준을 다른 환경의 수용으로 확대하지 않습니다.

## 5. 기존 API에 연결할 때 지킬 계약

| 연결 지점 | 사용할 API | 유지할 동작 |
|---|---|---|
| 엔티티/쓰기 관찰 | `SceneManager::Observations()`, `SceneObservation::Subscribe`, `ISceneEntityObserver` / `IComponentWriteObserver` | copied 로컬 값과 구독 수명 유지. N2에서 wire ID로 변환. renderer dirty 큐를 일반 변경 스트림으로 전용하지 않음 |
| 명령/커밋 결과 | `SceneManager::SimulationCommands()`, `SimulationExchange::Bind/Submit/Read`, `SimulationAdapter::Apply/Capture` | GT에서 Bind/Reset, epoch 검사, 성공 커밋 뒤 Read. Capture는 커밋 전이므로 안에서 외부 전송하지 않음 |
| 고정 틱 | `SceneManager::SimulationClock()`, `ExecuteSimulationTick` | 단일 60Hz clock·최대 catch-up 4 유지. 명령 → F# → 물리 → 관찰/결과 → 성공 커밋 순서 유지 |
| 씬·컴포넌트 | `CookedSceneLoader::RegisterComponentReader`, 기존 Entity/Component factory | 별도 엔티티 저장소나 수명 큐를 만들지 않음. 기존 identity·핸들·GC·DDOL 유지 |

현재 구현에 `ISimulationFrameObserver` API는 없습니다. 위 실제 연결 지점을 사용하십시오. Bind는 단일 adapter를 교체하므로 기존 로컬 명령/snapshot 소비자가 사라지지 않도록 조합·호환 정책과 회귀가 필요합니다.

게임 저작은 F# 일반 멤버 `OnTickSimulate(dt)`/API45입니다. 필요한 생명주기만 선언하고 생성기가 정적으로 등록합니다. C#은 ScriptCore의 ABI 바인딩입니다. OnSimulate·Scope.Delay 전용 구조나 새 생명주기를 추가하지 않습니다.

개별 F# 콜백 예외는 해당 스크립트를 격리하고 나머지 배치를 계속합니다. 배치 전체 실패의 미완료 snapshot 미공개와 구분하십시오. 세계 상태 전체 rollback, 임의 Task 완료 순서, 플랫폼 간 PhysX bitwise 결정성을 보장하는 계약은 아닙니다.

로컬 exchange의 명령 256개/64바이트·미래 128틱·snapshot 8개/4096바이트, schema 1과 local handle/token은 wire 규격이 아닙니다. N5/N6에서 네트워크 저장소·분할·압력 정책을 설계하고 공통 계약을 바꿀 때 기존 소비자까지 검증합니다.

## 6. 검증 기록을 찾는 위치

`validation-evidence.zip`을 펼친 `$evidenceRoot` 아래에서 확인합니다. 아래 경로는 수신자의 Evidence 폴더 기준입니다. 로그 안의 `C:\Users\idene\...`는 실행 당시의 경로이며 수신자 PC에 만들 경로가 아닙니다.

| 확인할 기록 | Evidence 폴더 안의 상대 위치 |
|---|---|
| 파일 원장·SHA256 | `evidence-index.json` |
| 고정 소스 제품 회귀 17그룹 | `repinned/pinned/Build/Verification/NDA/Suite-b1337ef598854c31b052ac849ec4f674/result.json` |
| 같은 실행의 그룹별 로그 | 위 Suite 폴더의 `Product.log`, `W4NDBProduct.log`, `N1Product.log`, `N1DiagnosticProduct.log`, `N3PacingProduct.log`, `N3MovingGpuProduct.log`, `HeadlessProduct.log` |
| 무렌더 Release 19프로세스 | `repinned/pinned/Build/Verification/Headless/Run-570cfbef83a74947b8347b8d2a467e9d/result.json` 및 같은 폴더의 case별 JSON·imports·link.map |
| 새 SDK F# Player | `repinned/pinned/Build/Verification/FSharp/Player-0b6913df73644df3a263183680e7bf50/result.json` |
| SDK 최종 패키징 | `repinned/primary/Build/Verification/ArtifactHistoryRewrite20261024/sdk-build.log` |
| 셰이더 metadata seed 원장 | `primary/Build/Verification/ServerPublish20261024/fixture-meta-seed.json` |
| 이전 Debug/Release/새 checkout 관찰 | `primary/Build/Verification/SharedScene20261024/` 및 `pinned/Build/Verification/SharedSceneFinal/` |

고정 소스·새 SDK의 F# Player와 All -Product 17그룹은 실제 재실행을 통과했습니다. 별도 host 57건은 앞선 Debug/Release/새 checkout 관찰 기록입니다. 이번 Release 고정 검증을 Debug 전체 재실행이나 실제 서버/원격 CI/다른 장치의 PASS로 해석하지 마십시오. 첫 패키징의 junction 거부와 ignored `.meta` 누락은 실패/재시도 기록으로 남겼습니다.

## 7. 전체 회귀와 작업 제출 기준

전체 회귀에는 먼저 고정 소스의 네이티브 Release 산출물이 필요합니다. Visual Studio/vcpkg 환경을 구성한 뒤 아래 명령을 소스 루트에서 실행합니다. 전체 Editor 제품 검사는 `$engineSource\Bin`도 사용하므로 SDK 압축 해제만으로 소스 빌드를 대신할 수 없습니다.

```powershell
Set-Location $engineSource
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$vsInstall = & $vswhere -latest -products '*' `
    -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
$engineMsbuild = Join-Path $vsInstall 'MSBuild/Current/Bin/amd64/MSBuild.exe'

foreach ($project in @(
    'Editor/CreatorEditor.vcxproj',
    'Player/Player.vcxproj',
    'Tools/AssetCooker/AssetCooker.vcxproj',
    'Tools/AssetPacker/AssetPacker.vcxproj',
    'HeadlessHost/HeadlessHost.vcxproj'
)) {
    & $engineMsbuild $project /m:4 /t:Build /p:Configuration=Release /p:Platform=x64 /v:minimal /nologo
    if ($LASTEXITCODE -ne 0) { throw "빌드 실패: $project" }
}

./Tools/regression/verify-net-wire-boundary.ps1 `
    -Configuration All -BaseRef 6752bf2db16d5a5220c00b5ac667659abafaa4d1

./Tools/regression/run-all.ps1 -Suite NetworkHandoff -Configuration All -Product `
    -EditorExe "$engineSource\Bin\x64-Release\Editor\CreatorEditor.exe" `
    -CookedStage $playerRoot

./Tools/regression/verify-headless-scene.ps1 -Configuration Debug
```

위 `BaseRef`는 이번 인계 기준입니다. 이후 변경은 해당 변경의 검토 기준 커밋으로 비교합니다. 기본 suite는 8등록 항목/Debug·Release 10그룹, Product는 7제품 검사를 더한 17그룹입니다. Debug headless 제품 검사는 별도로 유지합니다. 새 서버 기능의 통합 검증은 그 기능을 포함한 SDK/fixture도 새로 만들고 기존 원장과 구분해야 합니다.

각 단계 제출물에는 다음을 포함하십시오.

- 소스·F# 도구 리비전, SDK/패키지 식별자, 변경한 계약과 기존 호출자 영향.
- 사용한 fixture·부하·예산, 실제 실행 명령·종료 코드, 실패 주입 결과와 로그.
- 해당 단계의 완료 기준 충족 여부와 남은 미수용 범위.
- 재실행할 수 있는 필수 gate 등록. gate 삭제·실행 누락·하위 실패는 전체 실패로 처리합니다.

운영 soak의 시간·부하·허용 오류율·메모리/큐/지연 상한은 시험 전에 정합니다. 기존 17그룹 PASS만으로 새 transport·replication·서버 기능을 완료 처리하지 않습니다.

## 8. 계약을 확인할 고정 문서

- [네트워크 계약 6종·ID 원장·변경 절차](https://github.com/29thnight/CreatorEngine/blob/be057dca750fa2618076584d6c08e1e5d6ee61f6/docs/design/NetworkContractChangePolicy.md)
- [로컬 엔티티/쓰기 관찰 계약](https://github.com/29thnight/CreatorEngine/blob/be057dca750fa2618076584d6c08e1e5d6ee61f6/docs/design/SceneObservationContract.md)
- [명령·커밋 snapshot 계약](https://github.com/29thnight/CreatorEngine/blob/be057dca750fa2618076584d6c08e1e5d6ee61f6/docs/design/SimulationTickExchangeContract.md)
- [F# 저작·수명 계약](https://github.com/29thnight/CreatorEngine/blob/be057dca750fa2618076584d6c08e1e5d6ee61f6/docs/design/FSharpScriptingContract.md)
- [기존 Scene/Entity 무렌더 실행 계약](https://github.com/29thnight/CreatorEngine/blob/be057dca750fa2618076584d6c08e1e5d6ee61f6/docs/design/HeadlessSceneRuntimeContract.md)
- [Archive/Parser 경계](https://github.com/29thnight/CreatorEngine/blob/be057dca750fa2618076584d6c08e1e5d6ee61f6/docs/design/ArchiveParserBoundaryContract.md)
- [인계 고정 원장](https://github.com/29thnight/CreatorEngine/blob/be057dca750fa2618076584d6c08e1e5d6ee61f6/docs/analysis/ServerHandoffLock20261024.md)

실제 콘텐츠/provider와 계약·패키지 변경은 엔진 담당자와 조정합니다. 서버 담당자는 위 확장점을 사용해 네트워크 구현을 진행하고, 공통 씬·수명·고정 틱을 별도로 다시 구현하지 않습니다.

## 9. Artifacts 이력 정리와 자료 교체 안내

이번 자료는 Git 이력의 `Artifacts/` 제거 후 다시 빌드·검증한 인계본입니다. 소스 `6752bf2db1`, SDK `5f76f908-9df7-493e-8f39-5d24656a142a`와 이 문서가 가리키는 lock을 함께 사용하십시오. F# 도구 리비전과 착수 순서·완료 기준은 유지합니다. 기존 자료의 DLL이나 lock을 새 Payload에 섞지 마십시오.

기존 개발 checkout에서 일반 pull/merge하면 제거한 이력이 다시 합쳐질 수 있습니다. 작업 파일과 로컬 측정 자료를 보존한 뒤 새 clone에서 이 문서의 고정 SHA로 시작하십시오. 이전 실행 로그와 이전 로컬 Payload는 관찰 기록으로 보존했으며, 로그의 옛 SHA를 새 실행의 리비전으로 바꾸지 않았습니다. 새 회귀 기록은 Evidence의 `repinned/` 아래에 있습니다.
