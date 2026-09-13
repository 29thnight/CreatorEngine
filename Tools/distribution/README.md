# 사전 빌드 엔진·도구 개발 환경

엔진 유지보수자는 C++/ScriptCore/도구를 빌드해 배포본을 만든다. 게임 제작 환경에서는 그 배포본의
Player·cooker·packer와 포함된 C# 컴파일러를 사용한다. 게임 패키징 과정에서 Visual Studio, vcpkg,
엔진 소스 또는 시스템 .NET SDK를 호출하지 않는다. 현재 스크립트 입력은 `Assets/Script/**/*.cs`와
ScriptCore 참조이며, 별도 NuGet/package restore·사용자 native plugin ABI는 이 경로의 지원 범위 밖이다.

## 출력 구조

```text
Bin/x64-Debug/                       Release는 별도 x64-Release
  Editor/CreatorEditor.exe           정적 CRT로 만든 작은 실행 진입점
  Editor/CreatorEditor.runtime.dll    Editor + 정적 엔진 라이브러리
  Player/Player.exe
  Player/Player.runtime.dll
  Tools/AssetCooker/AssetCooker.exe + AssetCooker.runtime.dll
  Tools/AssetPacker/AssetPacker.exe + AssetPacker.runtime.dll
  Runtime/Common/*.dll               공용 native 종속성, 구성별 한 사본
  Runtime/Editor/efsw.dll             Editor 전용
  Runtime/Manifests/<host>.json       실제 import closure와 ABI/파일 해시
  Runtime/layout.version
```

Windows는 `main`보다 먼저 EXE import를 로드한다. 작은 EXE가 안전한 DLL 검색 경로를 설정하고
해당 host DLL을 명시적으로 로드한다. Editor만 Editor 전용 경로를 추가한다. VS 디버거도 작은 EXE로
시작한다. EXE와 host 사이에는 빌려 쓰는 인자와 정수 반환값만 전달하며 engine 객체·할당·예외를
넘기지 않는다. host는 프로세스 종료까지 유지한다.

정적 엔진 프로젝트 여러 개가 하나의 host DLL로 링크되므로 프로젝트 개수만으로 singleton이 복제되지
않는다. 프로젝트를 하나로 합치는 일은 이 문제의 전제 조건이 아니다. 향후 사용자 native DLL을
자유롭게 붙이려면 별도의 Core/plugin ABI와 소유권 계약이 필요하다. 현재는 게임 **C# DLL**을 교체하는 경로다.

## 버전 정본

루트 `EngineVersion.json`이 제품명·기능 릴리스·네 자리 엔진 빌드·채널의 정본이다.
현재 `CreatorEngine 2 / 0.0.0.0 / preview / localDevelopment=true`는 **미발행 로컬 개발 상태**다.
기능 릴리스는 미지정이며 정책 문서의 예시 번호를 발행 번호로 사용하지 않는다.

명시적 버전 편집 후 다음 명령으로 native 상수, Windows 버전 리소스와 C# 도구의 버전 속성을 동기화한다.

```powershell
pwsh Tools/distribution/update-engine-version.ps1
pwsh Tools/distribution/update-engine-version.ps1 -Check
```

생성 파일은 정본 변경과 함께 검토·커밋한다. 일반 빌드는 일치 여부만 검사하며 소스, 번호, Git 상태를
자동 갱신하지 않는다. About·크래시 보고·`--engine-info`·EXE/DLL 파일 속성이 같은 엔진 빌드를 사용한다.
`buildId`는 UUID, `payloadDigest`는 SHA-256이며 Git revision/dirty/configuration과 각각 분리한다.
Script API는 현재 지원하는 정확한 계약 `24`만 선언한다. 더 큰 버전이 이전 계약을 지원한다고 간주하지 않는다.

배포 manifest의 native 표시용 projection인 `engine.info`는 운영체제/표준 라이브러리만으로 읽는다.
배포 검증은 이 값이 JSON manifest와 정확히 일치하는지도 검사한다. 채널과 배포 ID는 바이너리에
컴파일하지 않는다. 동일 파일을 Stable로 승격하는 릴리스 운영·서명 절차는 DL10의 후속 범위다.
발행 버전은 같은 플랫폼/구성/Shipping variant에서 다른 payload로 재발행할 수 없다.

## 엔진 유지보수자

VS18/v145로 Debug와 Release를 빌드한 뒤 각각 다음 명령을 실행한다.
독립 [CreatorBuildTool](../../BuildTool/README.md)의 `publish-engine --build`를 명시하면 엔진 빌드부터 수행한다.

```powershell
& .\Bin\x64-Debug\Tools\CreatorBuildTool\CreatorBuildTool.exe publish-engine --config Debug
& .\Bin\x64-Release\Tools\CreatorBuildTool\CreatorBuildTool.exe publish-engine --config Release
```

결과는 `Build/Distributions/local-...-<UUID>`에 놓이며 `Bin/x64-<config>/engine.distribution.json`이
선택 경로를 기록한다. 배포본에는 native host·종속 DLL·Resources·ScriptCore·고정 Roslyn 및 참조
assembly·source generator·사설 .NET 런타임·CreatorBuildTool EXE·라이선스가 포함된다.
배포·컴파일·패키징 로직은 BuildTool 전용 C# 프로젝트가 소유한다. 배포본에 PowerShell 실행 환경을 포함하지 않는다.

Debug 묶음은 개발 검증용이며 Debug CRT를 포함한다. 일반 사용자에게 발행하는 제품과 구별한다.
현재 오디오 closure는 실제 소스에 연결된 FMOD를 포함하며, PHASE 22의 FMOD 제거 완료를 뜻하지 않는다.

## 게임 프로젝트에서 사용

아래 `$engine`은 배포본의 절대 경로, `$project`는 기존 `Assets`와 `ProjectSetting`을 가진 프로젝트다.
엔진 변경은 `CreatorBuildTool select-engine`으로 명시적으로 수행한다. 설정별 개발 adapter pin은
`ProjectSetting/Engine.Debug.lock.json` 또는 `Engine.Release.lock.json`에 전체 버전과 배포 ID를 저장한다.

```powershell
$tool = Join-Path $engine 'Bin/x64-Release/Tools/CreatorBuildTool/CreatorBuildTool.exe'
& $tool select-engine --project $project --engine-distribution $engine
& $tool open-project --development-project $project --engine-distribution $engine
& $tool package-game --config Release --input-mode Project --project $project --engine-distribution $engine
```

에디터의 스크립트 빌드와 게임 빌드도 같은 배포본의 도구를 사용한다. 이미 컴파일한 게임 DLL은
`CreatorBuildTool package-game --game-scripts-assembly <GameScripts.dll>`로 전달할 수 있으며 해당 엔진을 기록한
`.engine.json` sidecar와 해시가 일치해야 한다.

이 디렉터리 입력 방식은 명시적 `--development-project` adapter다. 제품용
`--project <절대 .creatorproject>` parser/프로젝트 lock/Launcher/MSI는 아직 후속 DL1·DL6 범위다.
설정별 개발 pin을 정식 descriptor 구현 완료로 세지 않는다.

## 검증

```powershell
pwsh Tools/regression/verify-engine-distribution.ps1 -StaticOnly
pwsh Tools/regression/verify-engine-distribution.ps1 -Config Debug
pwsh Tools/regression/verify-engine-distribution.ps1 -Config Release -EngineDistribution $engine
```

버전 경계값·변조 거부·정확한 pin·native 표시 metadata 일치·최소 PATH와 다른 작업 폴더에서의 네 host
로딩·파일 버전·Editor DLL 격리·EXE 옆 중복 DLL 부재를 확인한다. 실제 게임 실행은 `CreatorBuildTool package-game`의
cook/pak/Player smoke 검증을 통과해야 게시된다. 빌드·게임 실행 결과는 별도 검증 기록에 남긴다.

[최종 Debug·Release 검증 기록](../../docs/analysis/SharedRuntimeDistributionValidation.md)에 배포 ID, 외부 Editor,
C# 생명주기·게임 게시 결과와 미해결 모델 씬 문제를 구분해 기록했다. 원본의 `GameScripts/*.cs` 검증 샘플은
외부 프로젝트 사본의 `Assets/Script`에 명시적으로 복사했으며, 원본 스크립트 배치를 자동 이전하지 않는다.
게임 C# 원본과 그 `.cs.meta`는 컴파일 후 native cook/PAK 입력에서 함께 제외한다.
