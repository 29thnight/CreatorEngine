# 엔진 모듈과 런타임 배포 단위

> 기준: master `493536e300e274cbf59989cdd62b20310c8ae64f`의 소스 정적 조사. 이 문서의 재작성에서 빌드·실행·성능 측정은 하지 않았다.

## 빌드 단위와 배포 단위

vcxproj는 컴파일·링크 단위이며 최종 배포 폴더와 일대일 대응하지 않는다. Player 프로젝트는 Player.runtime.dll을 만들고 EngineHost.targets가 RuntimeLauncher를 배치한다. Editor·AssetCooker·AssetPacker도 같은 host 규약을 사용한다. 도구별 entrypoint는 분리되어도 공통 native DLL과 .NET runtime을 공유할 수 있다.

Runtime/layout.version을 찾은 launcher는 DLL 검색을 System32와 명시적 Runtime 디렉터리로 제한한다. Runtime/Common이 공통 closure이며 Editor host는 Runtime/Editor를 추가한다. PATH에 우연히 설치된 DLL에 기대는 배포 계약이 아니다.

## 배포본의 identity

EnginePublisher가 새 buildId와 engine.manifest.json, native engine.info, payload hash를 만든다. EngineDistribution.Load가 config/platform/version 및 모든 payload를 검증한다. 프로젝트의 설정별 pin은 이 배포 identity를 고정한다. 폴더명만 같거나 engine version 문자열만 같은 재빌드를 동일 배포로 취급하지 않는다.

ProfilerViewer는 독립 실행 배포 계약을 갖고 game package와 Editor UI를 혼합하지 않는다. .NET10 runtime·Roslyn·reference assembly는 발행된 엔진 도구 closure에 들어간다. Debug/Release와 Shipping variant의 native runtime 요구는 구분해야 한다.

## 게임 결과

package-game은 immutable 엔진을 수정하지 않고 unique candidate에서 게임 managed assembly·cooked content·PAK·Player closure를 조합한다. 검증 성공 후 Game 결과와 current pointer를 게시한다. 실패·취소 또는 skip-verify 결과는 current를 바꾸지 않는다. candidate가 남았다는 사실은 게시 성공이 아니다.

현재 상세 command 계약은 [BuildPipelinePlan](BuildPipelinePlan.md), source-free 입력은 [SourceFreeAssetSetPackaging](../design/SourceFreeAssetSetPackaging.md)에 있다. 설치 프로그램·업데이트 채널·서명된 Launcher는 별도 미구현 제품 의도다. 현재 folder publication과 installer의 기능을 같은 것으로 설명하지 않는다.

## 구현 근거

- [Player/Player.vcxproj](../../Player/Player.vcxproj)
- [Tools/runtime/EngineHost.targets](../../Tools/runtime/EngineHost.targets)
- [Tools/runtime/RuntimeLauncher.cpp](../../Tools/runtime/RuntimeLauncher.cpp)
- [Tools/runtime/deploy-runtime.ps1](../../Tools/runtime/deploy-runtime.ps1)
- [BuildTool/EnginePublisher.cs](../../BuildTool/EnginePublisher.cs)
- [BuildTool/EngineDistribution.cs](../../BuildTool/EngineDistribution.cs)
- [BuildTool/GamePackager.cs](../../BuildTool/GamePackager.cs)
