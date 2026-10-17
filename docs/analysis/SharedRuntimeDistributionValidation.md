# 공용 runtime 배포의 검증 경계

> 기준: master `493536e300e274cbf59989cdd62b20310c8ae64f`의 소스 정적 조사. 이 문서의 재작성에서 빌드·실행·성능 측정은 하지 않았다.

## 현재 host 형태

Player 프로젝트는 `Player.runtime.dll`을 만드는 DynamicLibrary다. `Tools/runtime/EngineHost.targets`와 `RuntimeLauncher`가 실행 진입·배포 closure를 연결한다. 공용 runtime 분리는 DLL 파일 개수로 판단하지 않고 host record의 ABI와 실제 import closure, 런처의 검색 경로로 판단해야 한다.

발행기는 Editor/Player/AssetCooker/AssetPacker의 build record, Windows file version, script API, content ABI와 hash를 맞춘다. 별도 ProfilerViewer도 고유 deployment record를 요구한다. 게임 패키지는 발행된 Player record의 closure를 복사하고 private .NET, 환경·blue-noise·font를 더한다. Development는 runtime PDB 누락을 실패로 처리한다.

## 무엇을 증명하지 않는가

`engine.manifest.json` 검증은 파일 무결성 증거이며 MSI 설치·서명·clean VM 실행 증거가 아니다. 개발 checkout에 존재하는 DLL이 system PATH를 통해 우연히 로드되지 않았는지, Debug runtime의 배포 정책, 모든 backend의 driver dependency는 실제 실행에서 따로 확인해야 한다.

현재 문서는 소스를 다시 읽어 검증 지점을 정리한 것이다. 과거의 특정 local 배포 및 smoke 통과 기록을 현재 master의 통과로 재인증하지 않는다. 동일 revision의 재현 자료에는 host deployment record, engine manifest, package manifest, smoke marker와 failure log를 함께 남긴다.

[BuildPipelinePlan](../plans/BuildPipelinePlan.md)은 발행 순서, [PlayerModuleBoundaryAnalysis](PlayerModuleBoundaryAnalysis.md)는 ABI 경계를 설명한다.

## 구현 근거

- [Player/Player.vcxproj](../../Player/Player.vcxproj)
- [Tools/runtime/EngineHost.targets](../../Tools/runtime/EngineHost.targets)
- [Tools/runtime/RuntimeLauncher.cpp](../../Tools/runtime/RuntimeLauncher.cpp)
- [Tools/runtime/HostAbi.h](../../Tools/runtime/HostAbi.h)
- [BuildTool/EnginePublisher.cs](../../BuildTool/EnginePublisher.cs)
- [BuildTool/GamePackager.cs](../../BuildTool/GamePackager.cs)
