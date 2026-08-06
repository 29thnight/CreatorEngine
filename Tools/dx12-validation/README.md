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

vcpkg 기준점은 `vcpkg-installed-ports.json`에 기록된 ImGui 1.91.9와 efsw 1.4.1을
동시에 제공한다. 최신 vcpkg 포트로 올리면 렌더 검증과 무관한 API 차이 때문에
빌드 결과가 달라지므로 이 기준점을 먼저 맞춘다.

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
