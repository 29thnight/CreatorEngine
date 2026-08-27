# CreatorEngine

![Windows x64](https://img.shields.io/badge/Platform-Windows%20x64-0078D4?style=flat-square&logo=windows11&logoColor=white)
![C++23](https://img.shields.io/badge/C%2B%2B-23-00599C?style=flat-square&logo=cplusplus&logoColor=white)
![MSVC v145](https://img.shields.io/badge/MSVC-v145-5C2D91?style=flat-square&logo=visualstudio&logoColor=white)
![DirectX 12](https://img.shields.io/badge/Graphics-DirectX%2012-107C10?style=flat-square)
![Vulkan](https://img.shields.io/badge/Graphics-Vulkan-AC162C?style=flat-square&logo=vulkan&logoColor=white)
![.NET 10](https://img.shields.io/badge/.NET-10-512BD4?style=flat-square&logo=dotnet&logoColor=white)
![PhysX](https://img.shields.io/badge/Physics-PhysX-76B900?style=flat-square&logo=nvidia&logoColor=white)
![FMOD](https://img.shields.io/badge/Audio-FMOD-000000?style=flat-square)

Windows용 C++23 게임 엔진, 에디터, 플레이어 및 콘텐츠 빌드 도구를 한 저장소에서 개발하는 프로젝트입니다.

CreatorEngine은 실시간 편집과 게임 실행이 같은 런타임 계층을 공유하도록 구성되어 있습니다. 렌더링은 DX12와 Vulkan을 지원하는 RHI 경계를 사용하고, 에디터는 Dear ImGui 기반 제작 도구를 제공하며, 게임 로직은 .NET 10 C# 스크립트로 확장할 수 있습니다.

> **개발 상태:** 이 저장소는 대규모 구조 개선이 진행 중인 개발 브랜치입니다. `docs/plans/`의 문서는 목표와 작업 순서를 설명하며, 현재 구현 여부는 소스와 회귀 결과를 기준으로 판단합니다. 공개 CI는 FMOD 바이너리 재배포 제약 때문에 Editor/Player 링크 대신 핵심 엔진 라이브러리의 Debug·Release 빌드를 검증합니다.

## 핵심 구성

| 영역 | 현재 역할 |
|---|---|
| Editor | 도킹 워크스페이스, Scene/Game View, Inspector, 콘텐츠 브라우저, 기즈모, Play Mode, 자산 변경 감지 |
| Rendering | RenderGraph 기반 프레임 구성, DX12/Vulkan RHI, 백엔드 공용 렌더 패스, Slang 셰이더 컴파일 경계 |
| Runtime | Scene·Component 수명주기, 렌더 프록시 발행, 입력, 애니메이션, UI, 오디오, 물리 |
| Scripting | CoreCLR 호스트, .NET 10 `ScriptCore`, 교체 가능한 `GameScripts` 어셈블리, Roslyn 소스 제너레이터 |
| Content | 자산 메타데이터, 모델·텍스처 처리, 씬·프리팹 직렬화, `AssetPacker` 기반 게임 패키징 |
| Diagnostics | RHI 자가 검증, 렌더 패리티 테스트, 프로파일링, 생명주기·계층 경계 회귀 스크립트 |

### 기술 기준

- **플랫폼:** Windows x64, Win32 창·입력 계층
- **네이티브:** C++23, MSVC v145, MSBuild
- **그래픽스:** DirectX 12, Vulkan, Slang, Dear ImGui, ImGuizmo
- **관리 코드:** .NET 10, C#, CoreCLR hosting API, Roslyn generator
- **물리·오디오:** NVIDIA PhysX, FMOD Core API
- **데이터:** YAML, JSON, 자체 `.creator`·`.prefab`·`.meta` 및 pak 포맷
- **의존성:** vcpkg manifest + 저장소 고정형 `ThirdParty/`

DX11은 현재 Scene Renderer 백엔드가 아닙니다. 런타임 렌더 백엔드는 프로세스 시작 시 `dx12` 또는 `vulkan`으로 고정되며, Editor의 Scene 출력과 ImGui 표시 계층이 같은 백엔드를 사용합니다.

## 빠른 시작

### 1. 요구 환경

- Windows 10/11 x64
- Git
- Visual Studio의 **Desktop development with C++** 워크로드
  - MSVC v145 toolset
  - Windows 10 SDK
  - x64 MSBuild
- .NET 10 SDK와 x64 Runtime
  - 네이티브 호스트 팩 버전은 [`EngineOutput.props`](EngineOutput.props)의 `DotNetHostPackVersion`과 일치해야 합니다. 현재 값은 `10.0.11`입니다.
- vcpkg
  - 포트와 버전은 [`vcpkg.json`](vcpkg.json)의 manifest와 `builtin-baseline`이 고정합니다.
- FMOD Core API 2.02.26 x64 개발 파일

Vulkan SDK는 일반 빌드에 필요하지 않습니다. Vulkan 헤더는 저장소에 고정되어 있고 런타임 진입점은 `vulkan-1.dll`에서 동적으로 읽습니다. SDK는 셰이더 재생성이나 validation layer를 이용한 검증에만 필요합니다.

### 2. 저장소와 의존성 준비

```powershell
git clone https://github.com/29thnight/CreatorEngine.git
Set-Location CreatorEngine

& "$env:VCPKG_ROOT\vcpkg.exe" integrate install
```

MSBuild가 솔루션을 평가하면 manifest mode가 `vcpkg_installed/`에 필요한 패키지를 복원합니다. 바이너리 캐시가 없는 환경의 첫 복원은 PhysX와 Assimp 등의 소스 빌드로 오래 걸릴 수 있습니다.

FMOD SDK의 재배포 제한으로 아래 네 파일은 저장소에 포함되지 않습니다. 로컬 SDK에서 직접 배치해야 Editor와 Player를 링크·실행할 수 있습니다.

```text
ThirdParty/Fmod/lib/x64/fmod_vc.lib
ThirdParty/Fmod/lib/x64/fmodL_vc.lib
ThirdParty/Fmod/bin/x64/fmod.dll
ThirdParty/Fmod/bin/x64/fmodL.dll
```

벤더링 의존성의 선정 이유와 갱신 규칙은 [`ThirdParty/README.md`](ThirdParty/README.md)에 정리되어 있습니다.

### 3. Editor 빌드

Visual Studio에서 `CreatorEngine.sln`을 열고 `CreatorEditor`를 시작 프로젝트로 지정하거나, x64 Developer PowerShell에서 다음 명령을 실행합니다.

```powershell
msbuild .\CreatorEngine.sln `
  /m `
  /t:CreatorEditor `
  /p:Configuration=Debug `
  /p:Platform=x64 `
  /v:minimal
```

`CreatorEditor` 빌드는 `ScriptCore`와 `GameScripts`도 함께 빌드합니다. 주요 산출물은 역할별 디렉터리에 분리됩니다.

```text
Bin/x64-Debug/Editor/       CreatorEditor 실행 번들
Bin/x64-Debug/Player/       Player 실행 번들
Bin/x64-Debug/Managed/      ScriptCore와 GameScripts
Bin/x64-Debug/Resources/    엔진 리소스
Build/Lib/x64-Debug/        네이티브 정적 라이브러리
Build/Obj/                  프로젝트별 중간 산출물
```

### 4. 실행

```powershell
.\Bin\x64-Debug\Editor\CreatorEditor.exe
```

Editor는 실행 파일 위치에서 저장소 루트를 찾고 `Dynamic_CPP/`를 기본 프로젝트로 엽니다. 로컬 프로젝트 설정이 아직 없다면 기본값으로 시작한 뒤 Editor가 `Dynamic_CPP/ProjectSetting/EngineSettings.asset`을 생성합니다.

렌더 백엔드는 Editor 설정에서 선택하거나 프로젝트 설정에 아래 값을 저장한 뒤 프로세스를 다시 시작합니다.

```yaml
render:
  backend: dx12 # 또는 vulkan
```

Vulkan을 선택한 시스템에는 Vulkan 로더를 제공하는 그래픽 드라이버가 설치되어 있어야 합니다.

## 저장소 구조

| 경로 | 책임 |
|---|---|
| [`Engine/Utility_Framework/`](Engine/Utility_Framework/) | 공용 타입, 컨테이너, 로깅, 리플렉션, 직렬화, 런타임 설정 |
| [`Engine/RenderEngine/`](Engine/RenderEngine/) | RHI, RenderGraph, 렌더 패스, GPU 자원 및 렌더 씬 |
| [`Engine/SceneRuntime/`](Engine/SceneRuntime/) | Scene·Component, 시스템 갱신, CoreCLR 호스트, 렌더 프록시 연결 |
| [`Engine/Physics/`](Engine/Physics/) | PhysX 초기화, 시뮬레이션, 쿼리와 컴포넌트 연결 |
| [`Engine/EngineDiagnostics/`](Engine/EngineDiagnostics/) | 프로파일링과 진단 인프라 |
| [`Editor/`](Editor/) | Editor 애플리케이션, UI, ImGui 표시 계층, 렌더 회귀 모음 |
| [`Player/`](Player/) | 패키지된 게임을 실행하는 독립 호스트 |
| [`ScriptCore/`](ScriptCore/) | C# 엔진 API와 네이티브 바인딩 |
| [`ScriptCore.Generators/`](ScriptCore.Generators/) | 스크립트 등록·직렬화 코드를 생성하는 Roslyn analyzer |
| [`GameScripts/`](GameScripts/) | 기본 프로젝트의 게임 스크립트와 회귀 probe |
| [`Dynamic_CPP/`](Dynamic_CPP/) | Editor가 여는 기본 저작 프로젝트와 추적 가능한 테스트 자산 |
| [`Tools/`](Tools/) | 패키저, 빌드 오케스트레이터, 검증·회귀 도구 |
| [`ThirdParty/`](ThirdParty/) | vcpkg 밖에서 버전을 고정하는 외부 코드와 런타임 |
| [`docs/`](docs/) | 계획, 설계 결정, 시점별 분석과 진행 대시보드 |

네이티브 실행 파일은 공통 정적 라이브러리를 조합합니다.

```text
CreatorEditor ─┬─ Editor / HostImGuiPresentation / RenderTests
               └─ SceneRuntime / RenderEngine / Physics / Diagnostics / Utility

Player ────────┬─ HostImGuiPresentation
               └─ SceneRuntime / RenderEngine / Physics / Diagnostics / Utility

AssetPacker ───── 독립 패키징 도구
```

## 빌드와 검증

### 핵심 엔진 라이브러리

공개 CI와 같은 범위는 다음 명령으로 확인할 수 있습니다.

```powershell
$targets = @(
  'Engine\Utility_Framework'
  'Engine\Physics'
  'Engine\SceneRuntime'
  'Engine\RenderEngine'
) -join ';'

msbuild .\CreatorEngine.sln `
  "/t:$targets" `
  /p:Configuration=Debug `
  /p:Platform=x64 `
  /p:EnableUnitySupport=false `
  /m /v:minimal /nologo
```

Debug CI는 각 번역 단위의 include 자급성을 확인하기 위해 non-unity로 빌드하고, Release CI는 기본 unity 구성을 검증합니다.

### 정적 경계 검사

```powershell
python .\scripts\check_include_boundary.py
```

이 검사는 Editor/Core include 방향, 프로젝트 참조, 소스 편입과 허용 목록의 회귀를 확인합니다.

### 추가 회귀

- [`Tools/regression/README.md`](Tools/regression/README.md) — 생명주기, 리플렉션, 계층, 패키징 등 구조 회귀
- [`Tools/dx12-validation/README.md`](Tools/dx12-validation/README.md) — DX12 검증 환경과 실행 절차
- [`Tools/profiling-validation/README.md`](Tools/profiling-validation/README.md) — 프로파일링 수집 검증
- `Editor/RenderTests/` — DX12/Vulkan 공용 패스와 백엔드별 렌더 테스트

AddressSanitizer는 별도 솔루션 구성을 늘리지 않고 빌드 속성으로 켭니다.

```powershell
msbuild .\CreatorEngine.sln `
  /p:Configuration=Debug `
  /p:Platform=x64 `
  /p:EngineAsan=true
```

## 게임 패키지 만들기

[`Tools/build.ps1`](Tools/build.ps1)은 Player와 AssetPacker 빌드, 관리 어셈블리 빌드, 스테이징, pak 생성, smoke test와 게시를 한 흐름으로 수행합니다.

```powershell
pwsh .\Tools\build.ps1 `
  -Config Release `
  -InputMode Project `
  -RenderBackend dx12 `
  -BuildNative
```

검증에 성공한 결과는 기본적으로 `Build/Staging/` 아래의 버전된 디렉터리에 게시되고 `*.current.json` 포인터가 최신 배포를 가리킵니다. 재현 가능한 추적 파일만으로 패키지 입력을 제한하려면 `-InputMode Tracked`를 사용합니다.

`-SkipVerify` 결과는 candidate로만 남고 게시되지 않습니다. Release 패키지는 .NET 10 x64 Runtime과 Microsoft Visual C++ Redistributable을 외부 런타임 전제로 기록합니다.

## 문서 읽는 순서

1. [`docs/README.md`](docs/README.md) — 문서 트리와 문서 성격 구분
2. [`docs/RefactoringPlanDashboard.html`](docs/RefactoringPlanDashboard.html) — 전체 페이즈, 의존 관계와 현재 판정
3. `docs/design/` — 채택한 구조와 기각한 대안
4. `docs/analysis/` — 특정 시점의 코드·성능 분석 기록
5. `docs/plans/` — 아직 남은 작업, 슬라이스와 완료 게이트

계획 문서의 체크리스트나 정적 문서 검증은 구현·빌드·런타임 통과를 의미하지 않습니다. 변경 시에는 관련 소스, 호출 경로, 빌드 산출물과 회귀 증거를 함께 확인해야 합니다.

## 의존성과 라이선스

- vcpkg가 관리하는 직접 의존성은 [`vcpkg.json`](vcpkg.json)을 기준으로 합니다.
- 저장소가 직접 포함하는 의존성의 출처와 라이선스는 [`ThirdParty/README.md`](ThirdParty/README.md) 및 각 하위 디렉터리 문서를 기준으로 합니다.
- 저장소 루트에는 현재 프로젝트 전체에 적용되는 별도 `LICENSE` 파일이 없습니다. 외부 사용·재배포가 필요하면 프로젝트 소유자에게 먼저 확인하십시오.
