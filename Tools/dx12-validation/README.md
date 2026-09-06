# DX12 렌더러 검증 환경

이 폴더는 DX11에서 DX12로 메인 렌더러를 교체하는 동안 같은 조건으로 빌드하고,
D3D12 검증 레이어·GPU-Based Validation·DRED·PIX·RenderDoc을 반복 실행하기 위한
진입점이다.

## 설치 기준

- Visual Studio Community 2026 / MSBuild v18
- Windows SDK 10.0.26100
- CUDA 12.8
- Microsoft PIX 2603.25
- RenderDoc 1.45
- Windows Developer Mode(PIX replay·CSV/스크린샷 export에 필요)
- vcpkg baseline `b02e341c927f16d991edbd915d8ea43eac52096c`

vcpkg 기준점은 `vcpkg.json`의 `builtin-baseline`이 고정한다(ImGui 1.91.9와
efsw 1.4.1을 동시에 제공하는 커밋). 최신 vcpkg 포트로 올리면 렌더 검증과
무관한 API 차이 때문에 빌드 결과가 달라지므로 이 기준점을 먼저 맞춘다.
(구 `vcpkg-installed-ports.json`은 classic 모드 시절 스냅샷 — 매니페스트
전환으로 2026-08-24 은퇴했다.)

```powershell
git -C C:\Users\idene\source\vcpkg fetch --shallow-since=2025-03-01 origin master
git -C C:\Users\idene\source\vcpkg checkout --detach b02e341c927f16d991edbd915d8ea43eac52096c
C:\Users\idene\source\vcpkg\bootstrap-vcpkg.bat -disableMetrics

# PhysX를 제외한 고정 포트를 먼저 복원한다.
$ports = Get-Content .\vcpkg-response.txt | Where-Object { $_ -notmatch '^physx:' }
& C:\Users\idene\source\vcpkg\vcpkg.exe install $ports

# 구형 기준점의 PhysX만 VS18 호환 CMake로 빌드한다.
git -C C:\Users\idene\source\vcpkg apply C:\path\to\CreatorEngine\Tools\dx12-validation\vcpkg-vs18.patch
$tools = @(
  'C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin',
  'C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja',
  'C:\Users\idene\source\vcpkg\downloads\tools\7zip-24.09-windows',
  'C:\Users\idene\source\vcpkg\downloads\tools\7zr-24.09-windows'
)
$env:Path = ($tools -join ';') + ';' + $env:Path
$env:VCPKG_FORCE_SYSTEM_BINARIES = '1'
& C:\Users\idene\source\vcpkg\vcpkg.exe install physx:x64-windows
```

`vcpkg-vs18.patch`는 구형 기준점에 `Visual Studio 18 2026` 생성기 이름만 추가한다.
이 기준점이 요구하는 CMake 3.30은 VS18 생성기를 모르므로, 패키지를 복원할 때는
VS2026에 포함된 CMake를 사용한다.

## 권장 실행 순서

저장소 루트에서 실행한다.

```powershell
pwsh -NoProfile -File .\Tools\dx12-validation\Invoke-DX12Validation.ps1 -Action Preflight
pwsh -NoProfile -File .\Tools\dx12-validation\Invoke-DX12Validation.ps1 -Action Verify -Validation Gpu
```

`Verify`는 전체 Debug x64 빌드, 독립 DX12 자가 검증, 메인 렌더러 배선 스모크를
순서대로 수행한다. 결과 PNG와 stdout/stderr는
`artifacts\dx12-validation\<timestamp>-*` 아래에 남는다.

검증 모드는 다음 환경 변수로 엔진에 전달된다.

- `CREATOR_DX12_VALIDATION=off|basic|gpu`: Debug Layer와 GPU-Based Validation 선택
- `CREATOR_DX12_DRED=0|1`: Auto Breadcrumb, page fault, breadcrumb context 기록
- `CREATOR_DX12_BREAK_ON_ERROR=0|1`: 디버거 연결 시 ERROR/CORRUPTION에서 중단

GPU-Based Validation은 실행 순서와 성능을 크게 바꿀 수 있다. 올바름 검사는 `Gpu`,
PIX/RenderDoc 성능·프레임 분석은 `Basic`으로 분리해서 캡처한다.

## 전수 스윕 — `Invoke-Dx12Suite.ps1`

`Invoke-DX12Validation.ps1`은 `dx12.selftest` 하나만 돈다. 슬라이스 전후를
대조하려면 `dx12.*` **35종 전부**를 같은 자로 재야 한다.

```powershell
# 변경 전 기준선
pwsh -NoProfile -File .\Tools\dx12-validation\Invoke-Dx12Suite.ps1 -OutDir artifacts\suite-before

# 변경 후 — 판정 줄을 대조하고 차이가 있으면 exit 1
pwsh -NoProfile -File .\Tools\dx12-validation\Invoke-Dx12Suite.ps1 -OutDir artifacts\suite-after `
    -Baseline artifacts\suite-before\verdicts.csv
```

**이 스크립트가 구조로 막는 것 셋** — 셋 다 실제로 오독을 낸 적이 있다
(RhiBoundaryPlan §7.2.7):

| 함정 | 막는 방법 |
|---|---|
| 검사 목록을 CLI 도움말에서 뽑으면 35 중 26만 돈다(도움말은 손으로 유지해서 코드보다 낡는다) | 목록을 `ConsoleCommandSystem.cpp`의 `cmd == "dx12.*"`에서 뽑는다 |
| 판정 어휘가 셋(`통과`/`실패`/`완료`)인데 둘로 읽으면 계측 검사가 실패로 잡힌다. 반대로 문자열로만 찾으면 진행 마커(`[1/4] … 완료`)가 판정으로 잡힌다 | `[CLI] <검사> <판정>` 형태로만 읽는다 |
| 워밍업 없이 부르면 `dx12.gizmoscene`이 점등 0으로 실패한다 — 에디터 씬의 살아 있는 카메라를 쓰기 때문 | `-WarmupFrames`(기본 240)를 검사 앞에 준다 |

**기준선 (2026-08-11, 워밍업 240):** `통과 28 · 완료 4 · 실패 2 · 무판정 1`.

- 실패 `dx12.scene` — 열린 씬에 메시가 0개다(코드가 아니라 리소스 부재).
  메시를 태우려면 `scene.switch <절대경로>` + `wait 240`이 앞에 필요하다.
- 실패 `dx12.bench11` — `_DEBUG`에서 설계된 거부. Release로 재야 한다.
- 무판정 `dx12.live` — 상태를 찍는 것이라 판정 줄을 내지 않는다.

> **※ `dx12.uploadring`은 2026-09-06에 제거됐다.** 같은 검사(`RunUploadSegmentTest`)를
> `rhi.uploadsegments`가 그대로 부르므로 검증은 하나도 잃지 않았다 — 그쪽은 Vulkan
> 자가 검증까지 함께 돌린다. `Invoke-Dx12Suite`의 `dx12.*` 전수 스윕에서도 빠졌으니
> 아래 제외 지침은 더 필요 없다. 아래 실측 기록은 **왜 뺐는지의 근거**로 남긴다.

**⚠ `dx12.uploadring`은 비결정적이었다 (2026-08-23 실측).** 같은 바이너리로 단독
실행 9회에 8통과·1실패였고, 전체 스위트 실행에서도 한 번 실패했다. 실패 줄은

    [7/7] 병렬 CAS·worker growth 실패 (무효 0 · 겹침 0 · CAS 재시도 6 · worker 생성 0)

인데, `worker 생성 0`이 핵심이다 — 링이 새 worker 세그먼트를 늘리는 경로가 경합에
의존해서 매번 태워지지 않는다. 무효·겹침이 0이므로 **정확성 위반은 아니고 커버리지
누락**이다.

그래서 이 검사 하나 때문에 `통과 28 · 실패 2`가 `통과 27 · 실패 3`으로 보일 수 있다.
**절대값이 기준선과 어긋나면 회귀로 단정하기 전에 그 검사만 단독으로 몇 번 다시
돌려라.** 기준선 대조를 게이트로 쓸 때는 이 검사를 제외하거나 불일치 시 재시도해야
한다 — 안 그러면 거짓 실패가 나고, 거짓 실패는 거짓 통과만큼 나쁘다(게이트를 못
믿게 되면 진짜 회귀가 지나간다).

`-WarmupFrames 0`으로 재면 `통과 27`이 나온다. **회귀가 아니라 다른 자다** —
절대값을 기준선과 견줄 때는 자가 같은지 먼저 확인한다.

검사당 프로세스를 하나씩 쓴다. 한 프로세스에 몰면 어서션 모달 하나가 뒤의
검사를 통째로 막고 로그에는 아무것도 안 남는다.

**반드시 `pwsh`(7+)로 실행한다.** Windows PowerShell 5.1은 이 스크립트의 한글을
잘못 읽어 파싱이 무너진다.

## PIX

```powershell
pwsh -NoProfile -File .\Tools\dx12-validation\Invoke-DX12Validation.ps1 `
  -Action PixCapture -Validation Basic -WaitFrames 1800

pwsh -NoProfile -File .\Tools\dx12-validation\Invoke-DX12Validation.ps1 `
  -Action PixAnalyze -CapturePath C:\path\to\creator-dx12.wpix
```

`PixCapture`는 현재 DX12 씬 출력이 오프스크린이고 최종 Present가 DX11 ImGui
셸인 구조에서도 동작하도록 `IDXGraphicsAnalysis::BeginCapture/EndCapture` 경계를
사용한다. 이 DX11 셸은 UI/표시 브리지이며 SceneRenderer는 실행하지 않는다. 원시 PIX
이벤트 패킷은 쓰지 않는다. `PixAnalyze`는 캡처를 Debug Layer로 재생하고 D3D 이벤트
CSV와 스크린샷을 내보낸다.

PIX replay/export가 `E_PIX_FEATURE_REQUIRES_DEVELOPER_MODE`로 실패하면 관리자 계정으로
Windows 설정에서 `개발자 모드`를 검색해 켠 뒤 다시 실행한다.

## RenderDoc

```powershell
pwsh -NoProfile -File .\Tools\dx12-validation\Invoke-DX12Validation.ps1 `
  -Action RenderDoc -Validation Basic -WaitFrames 20000 -TimeoutSec 600
```

창이 열린 뒤 목표 프레임에서 F12를 누른다. `.rdc`는 해당 실행의 artifact 폴더에
저장된다.

## 판정 기준

- `dx12.selftest 통과`와 PNG 생성
- `render.backend status`가 `활성: enhanced-dx12 (단독)`
- `[dx12.live 검증]`, `[CORRUPTION]`, `[DRED]` 출력 없음
- 프로세스 종료 코드 0
- 캡처에서 RTV/DSV, descriptor heap, resource state, PSO/root signature가 의도와 일치

원시 PIX 이벤트 패킷을 커맨드 리스트에 직접 쓰지 않는다. 과거 잘못된 이벤트
payload와 짝이 맞지 않는 EndEvent가 커맨드 스트림을 깨뜨린 적이 있으므로, 마커가
필요하면 정식 PIX API 배선을 별도 변경으로 검증한 뒤 도입한다.
