# CreatorEngine

![Windows x64](https://img.shields.io/badge/Platform-Windows%20x64-0078D4?style=flat-square&logo=windows11&logoColor=white)
![C++23](https://img.shields.io/badge/C%2B%2B-23-00599C?style=flat-square&logo=cplusplus&logoColor=white)
![MSVC v145](https://img.shields.io/badge/MSVC-v145-5C2D91?style=flat-square&logo=visualstudio&logoColor=white)
![DirectX 12](https://img.shields.io/badge/Graphics-DirectX%2012-107C10?style=flat-square)
![Vulkan](https://img.shields.io/badge/Graphics-Vulkan-AC162C?style=flat-square&logo=vulkan&logoColor=white)
![.NET 10](https://img.shields.io/badge/.NET-10-512BD4?style=flat-square&logo=dotnet&logoColor=white)
![PhysX](https://img.shields.io/badge/Physics-PhysX-76B900?style=flat-square&logo=nvidia&logoColor=white)
![FMOD](https://img.shields.io/badge/Audio-FMOD-000000?style=flat-square)
![AI Assisted](https://img.shields.io/badge/Development-AI%20Assisted-412991?style=flat-square)

Windows x64용 C++23 게임 엔진과 Dear ImGui 에디터, 독립 Player, 콘텐츠 빌드 도구를 함께 개발하는 프로젝트입니다.

Editor와 Player는 같은 런타임 계층을 사용합니다. 렌더링은 DirectX 12·Vulkan RHI와 `EnhancedRenderGraph`로 구성하고, 게임 로직은 .NET 10 C# 스크립트로 확장합니다. 엔진 개발 환경과 게임 제작 환경을 분리하기 위해, 엔진 배포·C# 컴파일·콘텐츠 cook·게임 패키징을 독립 실행 파일인 **CreatorBuildTool**로 통합하고 있습니다.

> **개발 상태:** 현재는 구조 개선이 진행 중인 개발 버전입니다. [`EngineVersion.json`](EngineVersion.json)의 `CreatorEngine 2 / 0.0.0.0 / preview / localDevelopment=true`는 미발행 로컬 개발 상태이며 정식 릴리스 번호가 아닙니다. 아래 설명은 **2026-09-15의 소스와 빌드 설정**을 기준으로 합니다. 계획서의 목표와 현재 구현·검증 범위는 구분합니다.

## 현재 구성

| 영역 | 구현과 역할 |
|---|---|
| Editor | 중앙 ViewportHost, 도킹 도구 창, 씬·프리팹 문서, Inspector·Content Browser, 기즈모, Play/Stop 전환, 테마와 메뉴·창 선언 계층 |
| Rendering | DX12/Vulkan RHI, 백엔드 공용 렌더 패스, `EnhancedRenderGraph`의 패스 검증·컬링·배리어·자원 수명 관리, Slang 셰이더 컴파일 |
| Runtime | Scene·Component 수명주기, 렌더 프록시 발행, 입력, 애니메이션, UI, PhysX 물리와 FMOD 오디오 |
| Scripting | CoreCLR 호스트, .NET 10 `ScriptCore`, 교체 가능한 게임 C# 어셈블리, Roslyn 소스 제너레이터 |
| Content | fastgltf·ufbx 모델 임포트, MikkTSpace 탄젠트 생성, ryml 기반 저작 YAML, 자산 메타데이터, AssetCooker와 AssetPacker |
| Build & Distribution | C# `CreatorBuildTool`, 엔진 배포본과 프로젝트 pin, 포함된 Roslyn 컴파일러·사설 .NET 런타임, cook → PAK → Player 검증 → 게시 |
| Diagnostics | RHI·렌더 회귀, 프로파일링, Editor/Player 명령 서비스, 에디터 시각·성능 회귀와 계층 경계 검사 |

### 구현 범위와 후속 작업

**DX12와 Vulkan 코드가 존재하는 것과 에디터 전체가 양쪽에서 동일하게 검증된 것은 다릅니다.** 현재 [에디터 워크스페이스 개편](docs/plans/EditorWorkspaceRedesignPlan.md)의 검증 대상은 DX12입니다. Vulkan RHI와 공용 패스 경로는 별도로 다루며, 빠른 시작과 아래 게임 패키징 예제는 DX12를 사용합니다. DX11은 현재 Scene Renderer 백엔드가 아닙니다.

[`EnhancedRenderGraph::BuildOrder`](Engine/RenderEngine/Render/Graph/EnhancedRenderGraph.cpp)는 현재 **패스 선언 순서**로 실행 순서를 구성합니다. 리소스 의존성에 따른 자동 정렬, 리소스 버전 모델, aliasing·async compute는 [후속 스케줄링 계획](docs/plans/RenderGraphDependencySchedulingPlan.md)의 범위이며 현재 기능으로 표시하지 않습니다.

[enkiTS 이관](docs/plans/TaskSchedulerUnificationPlan.md), [FMOD를 대체할 오디오 백엔드](docs/plans/AudioBackendModernizationPlan.md), [정식 프로젝트 descriptor·Launcher·MSI](docs/plans/EngineDistributionAndLauncherPlan.md)도 현재 제공되는 빌드·배포 경로와 구분합니다. 전체 작업 상태는 [문서 색인](docs/README.md)과 [대시보드](docs/RefactoringPlanDashboard.html)에서 확인할 수 있습니다.

## 기술 기준

| 항목 | 기준 |
|---|---|
| 플랫폼·네이티브 | Windows x64, Win32, C++23, MSVC v145, MSBuild |
| 렌더링·에디터 | DirectX 12, Vulkan, Slang, Dear ImGui, ImGuizmo·ImViewGuizmo |
| 관리 코드·빌드 도구 | .NET 10, C#, CoreCLR hosting API, Roslyn |
| 모델·텍스처 | fastgltf + simdjson, ufbx, MikkTSpace, meshoptimizer, DirectXTex, stb |
| 물리·오디오 | NVIDIA PhysX, FMOD Core API |
| 직렬화·패키징 | ryml 기반 YAML, JSON, `.creator`·`.prefab`·`.meta`, 쿠킹된 런타임 문서, PAK |
| 의존성 관리 | [`vcpkg.json`](vcpkg.json)의 manifest·baseline + [`ThirdParty/`](ThirdParty/README.md)의 고정 의존성 |

수학 라이브러리 교체에는 `ThirdParty/Mathematics`를 사용합니다. 구조 이주와 픽셀·물리 런타임 검증의 잔여 범위는 [Mathematics 이주 계획](docs/plans/MathematicsMigrationPlan.md)에서 구분합니다.

## 소스에서 시작하기

이 절은 **엔진을 수정·빌드하는 개발자**를 위한 안내입니다. 이미 만들어진 엔진 배포본으로 게임을 제작하는 경우에는 [엔진 배포본으로 게임 만들기](#엔진-배포본으로-게임-만들기)를 참고하십시오.

### 1. 요구 환경

- Windows 10/11 x64와 Git
- Visual Studio의 **Desktop development with C++** 워크로드, **MSVC v145**, Windows SDK, x64 MSBuild
- **.NET 10 SDK와 x64 Runtime** — ScriptCore, GameScripts, CreatorBuildTool 빌드용
- **vcpkg**, **PowerShell 7 (`pwsh`)** — 네이티브 의존성과 빌드·배포 스크립트용
- **FMOD Core API 2.02.26 x64 개발 파일** — 아래 로컬 배치 필요

정적 경계 검사는 Python 3를 사용합니다. .NET 네이티브 호스트의 nethost 헤더·lib·DLL은 [`ThirdParty/DotNetHost`](ThirdParty/DotNetHost/README.md)에 고정되어 있으므로 설치된 SDK의 패치 폴더에 맞춰 경로를 수정하지 않습니다.

Vulkan 헤더와 Slang 런타임은 저장소에 고정되어 있어 일반 네이티브 빌드에 Vulkan SDK를 요구하지 않습니다. Vulkan 실행에는 드라이버의 Vulkan 로더가 필요하고, validation layer 등을 이용한 별도 검증에는 해당 개발 환경이 필요합니다.

### 2. 저장소와 의존성 준비

```powershell
git clone https://github.com/29thnight/CreatorEngine.git
Set-Location CreatorEngine

# VCPKG_ROOT는 vcpkg가 설치된 실제 경로로 설정합니다.
& "$env:VCPKG_ROOT\vcpkg.exe" integrate install
```

MSBuild의 manifest mode가 `vcpkg_installed/`에 필요한 패키지를 복원합니다. 바이너리 캐시가 없는 환경에서는 PhysX 등 네이티브 의존성을 소스에서 빌드하므로 첫 복원 비용이 큽니다. 현재 모델 임포트 경로는 저장소에 고정된 fastgltf·ufbx이며, Assimp 설치 목록을 따로 구성하지 않습니다.

FMOD 개발 바이너리는 저장소에 포함되지 않으므로 로컬 SDK에서 다음 파일을 배치해야 합니다.

```text
ThirdParty/Fmod/lib/x64/fmod_vc.lib
ThirdParty/Fmod/lib/x64/fmodL_vc.lib
ThirdParty/Fmod/bin/x64/fmod.dll
ThirdParty/Fmod/bin/x64/fmodL.dll
```

의존성의 출처·판본·갱신 규칙은 [`ThirdParty/README.md`](ThirdParty/README.md)와 각 하위 문서를 기준으로 합니다.

### 3. Editor와 빌드 도구 빌드

Visual Studio에서 `CreatorEngine.sln`을 열고 `CreatorEditor`를 시작 프로젝트로 지정합니다. 명령행에서는 **v145 도구가 준비된 x64 Developer PowerShell**에서 다음 명령을 실행합니다.

```powershell
dotnet build .\BuildTool\CreatorBuildTool.csproj -c Debug

msbuild .\CreatorEngine.sln `
  /m `
  /t:CreatorEditor `
  /p:Configuration=Debug `
  /p:Platform=x64 `
  /v:minimal
```

`CreatorEditor`의 네이티브 빌드는 `ScriptCore`와 `GameScripts`도 함께 빌드합니다. Player·AssetCooker·AssetPacker는 별도 타깃이며, 게임 제작용 엔진 배포본 생성 절차에서 함께 준비합니다.

주요 산출물의 배치는 다음과 같습니다. 아래의 Player·도구 항목은 해당 타깃을 빌드했을 때 생성됩니다.

```text
Bin/x64-Debug/
  Editor/CreatorEditor.exe + CreatorEditor.runtime.dll
  Player/Player.exe + Player.runtime.dll
  Tools/AssetCooker/AssetCooker.exe + AssetCooker.runtime.dll
  Tools/AssetPacker/AssetPacker.exe + AssetPacker.runtime.dll
  Tools/CreatorBuildTool/        C# 빌드 도구
  Runtime/Common/               공용 네이티브 DLL
  Runtime/Editor/               Editor 전용 DLL
  Runtime/Manifests/             호스트별 의존성·해시 기록
  Managed/                      ScriptCore와 GameScripts
  Resources/                    엔진 리소스
Build/Lib/x64-Debug/             네이티브 정적 라이브러리
Build/Obj/                       프로젝트별 중간 산출물
```

각 호스트의 작은 EXE가 DLL 검색 경로를 설정한 뒤 `*.runtime.dll`을 로드합니다. 엔진 정적 라이브러리는 해당 호스트 DLL에 링크됩니다. **EXE 하나만 복사해서 실행하는 구조가 아니므로** `Runtime/`을 포함한 산출물 배치를 유지해야 합니다. 자세한 경계는 [공용 런타임·배포 구조](Tools/distribution/README.md)에 정리되어 있습니다.

### 4. 실행

```powershell
.\Bin\x64-Debug\Editor\CreatorEditor.exe
```

소스 checkout의 기본 저작 프로젝트는 `Dynamic_CPP/`입니다. 외부 게임 프로젝트는 아래의 `select-engine`·`open-project` 경로로 특정 엔진 배포본에 연결합니다.

## 엔진 배포본으로 게임 만들기

### 엔진 개발자: 배포본 생성

[`CreatorBuildTool`](BuildTool/README.md)이 엔진 배포본 생성, 게임 C# 컴파일, 콘텐츠 cook·PAK·Player 검증·게시를 담당합니다. Editor와 CLI는 같은 실행 파일을 사용하며, [`Tools/build.ps1`](Tools/build.ps1)은 기존 호출을 전달하는 **호환 진입점**입니다.

```powershell
dotnet build .\BuildTool\CreatorBuildTool.csproj -c Release

$tool = '.\Bin\x64-Release\Tools\CreatorBuildTool\CreatorBuildTool.exe'
& $tool publish-engine --repository . --config Release --build
```

`--build`는 엔진 개발 환경에서 네이티브 호스트·도구 빌드까지 수행하는 명시적 옵션입니다. 결과는 기본적으로 `Build/Distributions/` 아래에 생성됩니다. 배포본에는 네이티브 호스트와 의존 DLL, 엔진 리소스, ScriptCore, Roslyn·참조 어셈블리·소스 제너레이터, 사설 .NET 런타임, CreatorBuildTool과 라이선스 자료가 포함됩니다.

### 게임 제작자: 프로젝트 연결과 패키징

다음 예제의 `$engine`은 **위 단계에서 생성했거나 전달받은 실제 Release 배포본의 절대 경로**, `$project`는 **`Assets/`와 `ProjectSetting/`이 있는 기존 게임 프로젝트의 절대 경로**로 바꿉니다. 아래 명령은 프로젝트를 새로 생성하지 않습니다.

```powershell
$engine = 'C:\Engines\MyEngineDistribution'
$project = 'C:\Projects\MyGame'
$tool = Join-Path $engine 'Bin/x64-Release/Tools/CreatorBuildTool/CreatorBuildTool.exe'

& $tool verify-engine --engine-distribution $engine
& $tool select-engine --engine-distribution $engine --project $project
& $tool open-project --engine-distribution $engine --development-project $project

# 에디터를 종료한 뒤, 같은 Release 배포본으로 패키징합니다.
& $tool package-game `
  --engine-distribution $engine `
  --project $project `
  --config Release `
  --input-mode Project `
  --stage-root "$project\Build\Staging" `
  --render-backend dx12
```

게임 C# 소스는 `Assets/Script/**/*.cs`에서 읽습니다. 배포본에 포함된 Roslyn으로 컴파일하므로 이 경로의 게임 제작자에게 Visual Studio·vcpkg·시스템 .NET SDK·PowerShell 설치를 요구하지 않습니다. 별도 NuGet 복원이나 사용자 네이티브 플러그인 ABI는 이 경로의 지원 범위가 아닙니다.

`select-engine`은 프로젝트에 구성별 엔진 pin을 저장합니다. **현재는 개발용 프로젝트 adapter이며 정식 `.creatorproject` parser나 Launcher를 의미하지 않습니다.** 엔진 배포본과 패키징 구성(Debug/Release 및 Shipping variant)을 일치시켜야 합니다.

패키징은 관리 코드 컴파일 → cook → 스테이징 → PAK 생성 → 격리된 Player smoke 검증 순서로 진행됩니다. 검증에 성공한 candidate만 불변 `Game-<ID>` 디렉터리로 게시하고 `*.current.json` 포인터를 갱신합니다. 위 예제는 `--stage-root`로 게임 프로젝트의 `Build/Staging/`을 명시합니다.

Release 게임 패키지는 현재 **.NET 10 x64와 필요한 네이티브 런타임을 함께 배치**합니다. Debug 결과는 개발 검증용입니다. `--skip-verify`는 미검증 candidate만 남기며 게시 포인터를 바꾸지 않습니다. `Workspace`·`Tracked` 입력은 엔진 소스 checkout의 `Dynamic_CPP` 전용입니다.

명령별 옵션과 실패·취소 동작은 [BuildTool 사용법](BuildTool/README.md), 엔진 pin과 배포 경계는 [배포 가이드](Tools/distribution/README.md)를 참고하십시오.

## 저장소 구조

| 경로 | 책임 |
|---|---|
| [`Engine/Utility_Framework/`](Engine/Utility_Framework/) | 공용 타입·컨테이너, 로깅, 리플렉션, 직렬화와 런타임 설정 |
| [`Engine/RenderEngine/`](Engine/RenderEngine/) | RHI, RenderGraph, 렌더 패스, GPU 자원과 렌더 씬 |
| [`Engine/SceneRuntime/`](Engine/SceneRuntime/) | Scene·Component, 시스템 갱신, CoreCLR 호스트와 렌더 프록시 연결 |
| [`Engine/Physics/`](Engine/Physics/) | PhysX 초기화, 시뮬레이션·쿼리와 컴포넌트 연결 |
| [`Engine/EngineDiagnostics/`](Engine/EngineDiagnostics/) | 프로파일링과 진단 인프라 |
| [`Engine/CommandService/`](Engine/CommandService/) | 개발용 Editor/Player 명령 서비스 |
| [`Editor/`](Editor/) | Editor 호스트, 워크스페이스·도구 창·메뉴, ImGui 표시 계층과 렌더 회귀 |
| [`Player/`](Player/) | 패키지된 게임을 실행하는 독립 호스트 |
| [`BuildTool/`](BuildTool/README.md) | C# CreatorBuildTool: 엔진 배포, 게임 컴파일·패키징·게시 |
| [`Tools/`](Tools/) | AssetCooker, AssetPacker, 배포 adapter와 검증·회귀 도구 |
| [`ScriptCore/`](ScriptCore/) · [`ScriptCore.Generators/`](ScriptCore.Generators/) | C# 엔진 API·네이티브 바인딩과 Roslyn 소스 제너레이터 |
| [`GameScripts/`](GameScripts/) | 소스 checkout의 게임 스크립트·회귀 샘플. 외부 프로젝트의 `Assets/Script` 배치와 구분 |
| [`Dynamic_CPP/`](Dynamic_CPP/) | 소스 checkout의 기본 저작 프로젝트와 테스트 자산 |
| [`ThirdParty/`](ThirdParty/README.md) | vcpkg 밖에서 버전을 고정하는 외부 코드·런타임과 출처 |
| [`docs/`](docs/README.md) | 활성·보관 계획, 설계 결정, 시점별 분석과 진행 대시보드 |

## 빌드와 검증

### 공개 CI의 범위

[현재 워크플로](.github/workflows/build.yml)는 Editor/Core 경계 검사와 핵심 엔진 라이브러리의 Debug·Release 빌드를 대상으로 합니다. FMOD 개발 바이너리가 저장소에 없으므로 **Editor/Player 링크·실행이나 실제 게임 패키징까지 확인하는 CI는 아닙니다.**

Debug 레그는 non-unity로 각 번역 단위의 include 자급성을 확인하고, Release 레그는 기본 unity 구성을 사용하도록 설정되어 있습니다. 로컬에서 같은 Debug 빌드 범위를 실행하려면 다음 명령을 사용합니다.

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

### 정적 검사와 BuildTool 테스트

```powershell
python .\scripts\check_include_boundary.py

dotnet run --project .\BuildTool\Tests\CreatorBuildTool.Tests.csproj -c Debug
```

첫 명령은 Editor/Core include 방향·프로젝트 참조·소스 편입을 검사합니다. BuildTool 테스트는 경로·pin·변조 거부·PAK 목록·게시와 프로세스 종료 계약 등을 확인합니다. 실제 엔진 배포본을 사용하는 컴파일 검증은 테스트에 `-- --engine <배포본의 실제 경로>`를 추가하고, 게임 실행 완료는 별도의 `package-game` Player 검증으로 판단합니다.

### 진단 빌드와 추가 회귀

```powershell
# 별도 솔루션 구성 없이 AddressSanitizer를 켭니다.
msbuild .\CreatorEngine.sln `
  /t:CreatorEditor `
  /p:Configuration=Debug `
  /p:Platform=x64 `
  /p:EngineAsan=true

# Player의 개발용 명령·진단 경로를 분리하는 Shipping 빌드입니다.
msbuild .\CreatorEngine.sln `
  /t:Player `
  /p:Configuration=Release `
  /p:Platform=x64 `
  /p:EngineShipping=true
```

Shipping 산출물은 `Bin/x64-Release-Shipping/`으로 분리되며 Editor의 별도 구성은 아닙니다. 게임 패키징에도 일치하는 Shipping 엔진 배포본과 `--shipping` 옵션이 필요합니다.

| 안내 | 범위 |
|---|---|
| [구조·에디터 회귀](Tools/regression/README.md) | 생명주기·리플렉션·계층·워크스페이스·패키징 등 개별 회귀 절차 |
| [DX12 검증](Tools/dx12-validation/README.md) | DX12 검증 환경과 실행 절차 |
| [프로파일링 검증](Tools/profiling-validation/README.md) | 수집·프로파일링 경로의 검증 |
| [렌더 테스트 소스](Editor/RenderTests/) | DX12/Vulkan 공용 패스와 백엔드별 테스트 |
| [BuildTool 검증 기록](docs/analysis/CreatorBuildToolValidation.md) | 시점별 실행 결과와 모델 씬 등 검증 한계 |

검사 스크립트가 존재하거나 계획서에 완료 표시가 있는 것만으로 현재 빌드·런타임이 통과했다고 판단하지 않습니다. 성능 수치도 해당 기록의 구성·장면·측정 조건과 함께 읽어야 합니다.

## 문서 읽는 순서

[문서 색인](docs/README.md)에서 시작해 [진행 대시보드](docs/RefactoringPlanDashboard.html)로 전체 상태를 확인합니다. 사용 절차는 [BuildTool](BuildTool/README.md)과 [배포 가이드](Tools/distribution/README.md), 설계 근거는 `docs/design/`, 측정 기록은 `docs/analysis/`에 있습니다.

`docs/plans/`는 활성·미래 작업, [`docs/plans/archive/`](docs/plans/archive/README.md)는 완료·중단·대체된 계획입니다. 보관 문서의 과거 목표나 측정 결과를 현재 기능 목록으로 읽지 않습니다.

## 개발 방식과 AI 활용

CreatorEngine은 Legacy 버전과 달리 현재의 재설계, 기능 개발, 리팩터링 및 문서화를 사실상 1인이 주도하는 개인 프로젝트입니다. 저장소에는 Legacy 버전에서 이어진 코드와 외부 오픈소스 의존성이 포함되어 있으므로, 이는 전체 코드가 단독으로 작성되었다는 의미는 아닙니다.

제한된 개발 인력으로 프로젝트의 규모와 복잡도를 관리하기 위해 생성형 AI 도구를 보조 수단으로 활용합니다. 활용 범위에는 코드베이스 탐색과 영향 범위 분석, 설계 대안 검토, 반복적인 코드·문서 초안 작성, 테스트 절차 정리 및 문제 진단이 포함될 수 있습니다.

AI가 제안하거나 생성한 결과는 그 자체로 구현 완료나 품질을 보증하지 않습니다. 변경 사항은 관련 소스와 호출 경로를 검토하고, 가능한 범위에서 컴파일, 정적 검사, 회귀 테스트 및 런타임 검증을 거친 뒤 반영합니다.

최종 설계 결정, 변경 승인, 품질 및 라이선스 준수에 대한 책임은 저장소 관리자에게 있습니다. 계획 문서나 AI가 작성한 설명보다 현재 소스 코드와 검증 결과를 우선합니다.

## 의존성과 라이선스

- vcpkg가 관리하는 직접 의존성은 [`vcpkg.json`](vcpkg.json)을 기준으로 합니다.
- 저장소가 직접 포함하는 의존성의 출처와 라이선스는 [`ThirdParty/README.md`](ThirdParty/README.md) 및 각 하위 디렉터리 문서를 기준으로 합니다.
- 저장소 루트에는 현재 프로젝트 전체에 적용되는 별도 `LICENSE` 파일이 없습니다. 외부 사용·재배포가 필요하면 프로젝트 소유자에게 먼저 확인하십시오.
