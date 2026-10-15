# CreatorBuildTool 빌드·패키지 파이프라인

> 기준: master `493536e300e274cbf59989cdd62b20310c8ae64f`의 소스 정적 조사. 이 문서의 재작성에서 빌드·실행·성능 측정은 하지 않았다.

## 현재 실행 계약

빌드의 정본은 C# `BuildTool/Program.cs`이다. PowerShell wrapper나 Editor UI가 독립된 패키징 알고리즘을 갖는 구조가 아니다. `CreatorBuildTool`은 `net10.0`, `win-x64` apphost이며, 네이티브 소스 빌드와 이미 발행된 엔진을 이용한 게임 빌드를 구분한다.

- `publish-engine`: 네이티브 host와 도구의 build record를 검증해 불변 배포 디렉터리를 만든다. `--build`일 때만 MSBuild를 호출한다
- `compile-game`: 선택된 배포의 ScriptCore, Roslyn, reference assembly로 `Assets/Script`의 C#을 컴파일한다
- `package-game`: 선택된 엔진의 Player를 복사하고, 게임 콘텐츠를 준비·패킹·실행 검증한 뒤 current pointer를 바꾼다
- `build-asset-set`, `build-runtime-bootstrap`: typed AssetSet과 source-free 패키징 입력을 별도로 생산한다
- `verify-engine`, `select-engine`, `open-project`: 배포 무결성 확인, 설정별 pin 기록, 개발 Editor 실행을 담당한다

## 입력과 경로

`EngineDistribution.Load`는 `engine.manifest.json`의 version, win-x64, Debug/Release, UUID buildId, 정확한 `binaryRoot`, payload digest, 각 파일 hash와 미기록 파일까지 검사한다. 게임 프로젝트는 `ProjectSetting/Engine.<Configuration>.lock.json`의 version/buildId/configuration이 선택 배포와 일치해야 한다. 채널명이나 폴더명만 같은 것으로 대체할 수 없다.

`Paths`는 로컬 DOS 경로를 전제로 한다. UNC/device 경로, ADS, 예약 이름, 끝의 점·공백, reparse 조상, 입력·출력 트리 겹침을 거부한다. 이는 Linux 범용 빌드 도구의 경로 계약이 아니다. stage, Assets, ProjectSetting, 배포 root 사이의 격리를 호출자가 우회해서는 안 된다.

## package-game의 여섯 단계

1. Engine: 배포와 프로젝트 pin을 확인하고 stage의 프로젝트별 독점 lock을 연다. 결과는 고유 `.candidate` 아래에 먼저 생성한다
2. BuildManaged: ScriptCore와 GameScripts를 준비한다. prebuilt assembly도 `.engine.json`의 engineBuildId, scriptApi, SHA-256 검증을 통과해야 한다
3. Cook 또는 Prebuilt content: Legacy는 선택한 Project/Workspace/Tracked 입력으로 model generation, typed cook, 문서를 만든다. PrebuiltAssetSets는 검증된 bootstrap을 사용한다
4. Stage: Player runtime record의 파일, private .NET, 환경·blue-noise·runtime font 및 notice를 복사한다. Development는 Player runtime PDB를 요구한다
5. Pak: 최종 Assets/ProjectSetting 논리 inventory, cooked 문서와 AssetSet activation을 검사하고 `AssetPacker` 결과 entry를 대조한다
6. Verify: 별도 TEMP/TMP와 제한된 PATH에서 Player smoke를 실행한다. 검증 후 runtime/PAK digest가 바뀌지 않았는지 재확인하고 release와 current pointer를 발행한다

`--skip-verify`는 후보를 남길 뿐 current package를 갱신하지 않는다. 실패·취소도 기존 published package를 보존한다. 후보/검증 로그 위치가 오류에 남으므로 이를 성공 배포로 취급하지 않는다. `BuildContext.Run`은 stdout/stderr를 읽고 timeout 및 Ctrl+C 때 자식 process tree 종료를 시도한다. 정상 0, 오류 1, 사용자 취소 130이 Program의 종료 계약이다.

## Verify가 실제로 판정하는 것

`PlayerVerification`은 exit code만 보지 않는다. CEMF-only catalog, cooked scene document, authoring text parser 0회, Development/Shipping marker, managed script 등록, 필요한 lifecycle 순서, native presenter에 제출된 game frame, 최소 display promotion을 검사한다. 압축 해제 root가 실행 PID 하나에 귀속되는지와 unpacked inventory/hash도 검사한다.

이 gate가 통과해도 모든 장면의 그래픽 정확성, clean VM 설치, 장시간 메모리 안정성, 모든 backend/driver 조합이 증명되지는 않는다. 이 checkout에서 그 gate를 새로 실행했다는 뜻도 아니다.

## 빌드 사용 순서

Windows 개발 환경에서 solution과 CreatorBuildTool을 먼저 빌드하고 `publish-engine --repository <repo> --config Debug`로 일치하는 배포를 만든다. `select-engine --engine-distribution <distribution> --project <project>`로 pin을 기록한 뒤 `package-game --engine-distribution <distribution> --project <project> --config Debug --input-mode Project`를 실행한다. `--build-native`는 기존 배포 재사용과 다른 명시적 네이티브 빌드 경로다.

설정 축은 Debug/Release와 Development/Shipping의 조합이다. Shipping은 `EngineShipping=true`로 별도 컴파일되는 제품 경계이며 Release의 동의어가 아니다. 기존 개발용 Workspace/Tracked는 저장소 입력 재현용이고, 외부 프로젝트의 기본 경로는 Project다.

## 남은 제품화 요구

서명된 설치 프로그램, Launcher의 설치 inventory와 `*.creatorproject` 검증, clean Windows VM 수용, 배포 다운로드 신뢰 검증은 [배포 계획](EngineDistributionAndLauncherPlan.md)의 미구현 범위다. 이 파이프라인의 성공만으로 해당 계획을 닫지 않는다. Source-free 입력의 상세 계약은 [SourceFreeAssetSetPackaging](../design/SourceFreeAssetSetPackaging.md)을 따른다.

## 구현 근거

- [BuildTool/Program.cs](../../BuildTool/Program.cs)
- [BuildTool/GamePackager.cs](../../BuildTool/GamePackager.cs)
- [BuildTool/GameCompiler.cs](../../BuildTool/GameCompiler.cs)
- [BuildTool/EngineDistribution.cs](../../BuildTool/EngineDistribution.cs)
- [BuildTool/PlayerVerification.cs](../../BuildTool/PlayerVerification.cs)
- [BuildTool/BuildContext.cs](../../BuildTool/BuildContext.cs)
- [BuildTool/Paths.cs](../../BuildTool/Paths.cs)
- [BuildTool/CreatorBuildTool.csproj](../../BuildTool/CreatorBuildTool.csproj)
