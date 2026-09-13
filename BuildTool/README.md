# CreatorBuildTool

배포본 생성, 게임 C# 컴파일, 콘텐츠 cook·PAK·Player 검증·게시를 담당하는 독립 실행 프로젝트다.
Editor와 CLI는 같은 `CreatorBuildTool.exe`를 호출한다. 빌드 구현은 이 디렉터리의 C# 코드에 있으며,
PowerShell을 내부에서 호출하거나 스크립트를 EXE에 내장하지 않는다.

## 빌드와 배포

엔진 유지보수자 환경에서 .NET 10 SDK로 빌드한다. 외부 NuGet 패키지는 사용하지 않는다.

```powershell
dotnet build BuildTool/CreatorBuildTool.csproj -c Debug
dotnet build BuildTool/CreatorBuildTool.csproj -c Release
```

솔루션의 Tools 그룹에도 프로젝트가 등록된다. 구성별 출력은 다음과 같다.

```text
Bin/x64-<Config>/Tools/CreatorBuildTool/
  CreatorBuildTool.exe
  CreatorBuildTool.dll
  CreatorBuildTool.deps.json
  CreatorBuildTool.runtimeconfig.json
```

EXE는 .NET apphost이며 실제 빌드 코드는 함께 있는 DLL에 들어 있다. 배포본에서는 상대 경로
`../../Runtime/DotNet`의 사설 .NET 10 런타임을 먼저 사용한다. 로컬 개발 환경에서는 시스템 런타임을
사용할 수 있다. 게임 제작자에게 Visual Studio·.NET SDK·PowerShell 설치를 요구하지 않는다.
파일 버전은 루트 `EngineVersion.json`에서 생성한 `EngineVersion.props`를 통해 읽으며 빌드 과정에서 원본 버전을 자동 변경하지 않는다.

## 명령

```powershell
$tool = '.\Bin\x64-Debug\Tools\CreatorBuildTool\CreatorBuildTool.exe'

# 엔진 유지보수자: 미리 빌드한 host·도구·종속성을 불변 배포본으로 묶는다.
& $tool publish-engine --repository . --config Debug
# --build는 명시적 native host 빌드, --shipping은 Shipping Player 선택이다.
# --no-pointer는 검증용 배포를 만들면서 작업 공간의 선택 포인터를 보존한다.

# 아래 두 값은 사용할 배포본과 프로젝트의 실제 절대 경로로 지정한다.
$engine = 'C:\Engines\<배포 디렉터리>'
$project = 'C:\Projects\MyGame'
$tool = "$engine\Bin\x64-Debug\Tools\CreatorBuildTool\CreatorBuildTool.exe"

& $tool verify-engine --engine-distribution $engine
& $tool select-engine --engine-distribution $engine --project $project
& $tool compile-game --engine-distribution $engine --project $project --config Debug --output "$project\Intermediate\Managed\Debug"
& $tool package-game --engine-distribution $engine --project $project --config Debug --startup-scene FT_Primitives.creator --render-backend dx12
& $tool open-project --engine-distribution $engine --development-project $project
```

- `publish-engine`: native build record·파일 버전·해시와 API를 확인하고 엔진, Roslyn, 참조 어셈블리,
  source generator, 사설 .NET 런타임, BuildTool, 라이선스를 배치한다. PowerShell 런타임·패키징 스크립트는 배포하지 않는다.
- `compile-game`: 정확한 엔진 pin을 확인하고 포함된 Roslyn으로 `Assets/Script/**/*.cs`를 컴파일한다.
  성공한 DLL의 엔진 ID·API·해시를 sidecar에 기록한다. C# 컴파일 실패는 기존 DLL을 교체하지 않는다.
  `--prebuilt-assembly`도 동일 엔진 ID와 해시가 필요하다. 별도 NuGet 복원은 지원하지 않는다.
- `package-game`: Project/Workspace/Tracked 입력, 모델 generation, cook 결과, CEDO 문서, PAK 재열거,
  격리된 Player smoke와 추출 파일 해시를 검증한다. 성공한 candidate만 불변 release로 옮긴 뒤 current pointer를 교체한다.
- `select-engine` / `open-project`: 기존 개발용 프로젝트 pin·`--development-project` adapter를 사용한다.
  정식 `.creatorproject` parser·Launcher·MSI 구현을 의미하지 않는다.

`package-game`의 기본 구성은 Debug, `publish-engine`과 `compile-game`의 기본 구성은 Release다.
명령 간 구성을 명시적으로 맞춘다. Shipping 배포를 패키징할 때는 `--shipping`도 지정한다.
`--input-mode Workspace|Tracked`는 엔진 소스 checkout의 `Dynamic_CPP`만 지원한다.
`--build-native`는 엔진 유지보수자용이며 일반 게임 패키징에서는 지정하지 않는다.

## 실패·취소·진단

- `--log-path <파일>`은 오류와 하위 도구 출력을 기록한다. `--json`은 schemaVersion 1의 JSON Lines를 출력한다.
  이벤트는 `log`, `stage`, `result`, `error`이며 `message`가 본문이다. JSON 출력에 일반 하위 도구 출력을 섞지 않는다.
- Ctrl+C는 작업을 취소하고 프로세스 트리를 종료한다. 각 하위 프로세스는 Windows Job에 속해
  빌드 도구 종료 시에도 후손 프로세스가 정리된다. 성공은 0, 실패는 1, 취소는 130을 반환한다.
- 파일 목록은 대소문자 중복, 경로 이탈, reparse point와 변조를 거부한다. 입력 원본과 출력 트리는 겹칠 수 없다.
- 프로젝트별 stage lock으로 동시 게시를 막는다. 실패한 candidate와 Player 로그는 진단용으로 남기고,
  기존 release와 current pointer는 보존한다. 새 release 이동 뒤 pointer 교체 실패 시 새 디렉터리만 남을 수 있다.
- `--skip-verify`는 검증되지 않은 candidate만 남기며 current pointer를 바꾸지 않는다.
- 에디터의 스크립트 컴파일은 기존 비동기 Job/로그 경로를 유지한다. 게임 빌드 버튼의 동기 대기 UI는
  기존 동작이며, 진행률 UI·취소 버튼 추가는 BuildPipelinePlan B2 운영성 후속 범위다.

## 호환 진입점

`Tools/build.ps1`과 `Tools/distribution/{publish-engine,compile-game,select-engine,open-project}.ps1`은
소스 checkout에서 기존 명령을 받아 EXE로 전달하는 얇은 adapter다. 해당 스크립트에 두 번째 빌드 구현을 두지 않는다.
native 엔진 개발용 MSBuild의 리소스 생성·배포 검증 스크립트와 `update-engine-version.ps1`은 별도 개발 도구로 유지한다.
완성된 게임은 Player와 native/managed 런타임을 사용하며 BuildTool도 필요하지 않다.

## 검증

```powershell
dotnet run --project BuildTool/Tests/CreatorBuildTool.Tests.csproj -c Debug
dotnet run --project BuildTool/Tests/CreatorBuildTool.Tests.csproj -c Debug -- --engine $engine
```

외부 테스트 패키지 없이 경로·배포 변조·pin·PAK 목록·Player 판정·원자 게시·프로세스 트리 종료를 검증한다.
`--engine`을 주면 실제 배포본의 Roslyn 컴파일, 실패 시 기존 DLL 보존, prebuilt DLL 검증도 실행한다.
실제 게임 패키지 완료 판정은 `package-game`의 Player 검증까지 통과한 결과로 별도 기록한다.
2026-09-13 결과와 모델 씬의 기존 실행 제한은 [검증 기록](../docs/analysis/CreatorBuildToolValidation.md)에 있다.

관련 정본: [BuildPipelinePlan](../docs/plans/BuildPipelinePlan.md),
[EngineDistributionAndLauncherPlan](../docs/plans/EngineDistributionAndLauncherPlan.md),
[EngineVersionPolicy](../docs/design/EngineVersionPolicy.md).
