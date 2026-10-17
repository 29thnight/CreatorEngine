# Player가 실제로 소유하는 모듈 경계

> 기준: master `493536e300e274cbf59989cdd62b20310c8ae64f`의 소스 정적 조사. 이 문서의 재작성에서 빌드·실행·성능 측정은 하지 않았다.

## 링크에서 확인되는 범위

Player.vcxproj는 DynamicLibrary이며 결과 이름은 Player.runtime이다. 직접 참조는 SceneRuntime·RenderEngine·Physics·Utility_Framework·GCCE·EngineDiagnostics와 non-Shipping CommandService를 포함한다. Editor 프로젝트를 직접 참조하지 않는다. source runtime DLL은 EngineHost.targets를 통해 launcher와 함께 배치된다.

이 분석은 project 선언을 읽은 결과다. 외부 DLL의 transitive import, 정확한 실행 프로세스 module inventory와 깨끗한 PC에서의 로드는 별도 검증이 필요하다. BuildTool의 PlayerVerification에는 exact PID module 경로·hash 검사 코드가 있으나 이 문서 작업에서 실행하지 않았다.

## PlayerMain의 조립

PlayerMain은 packaged content 경로와 runtime setting을 구성하고 공통 bootstrap을 호출한다. backend에 맞는 presentation을 만든 뒤 GT·RT·PT의 수명을 조정한다. 실제 화면 출력은 Player presentation이 소유한다. Vulkan의 CPU bridge와 DX12 shared image는 다른 capability이며 공통 RenderEngine 링크만으로 동일한 성능을 보장하지 않는다.

Development 명령은 PlayerCommands의 whitelist와 descriptor role을 함께 통과한다. Shipping은 service source/reference를 제외하고 관련 입력도 거부한다. Editor의 모든 console 명령을 Player에서 자동 제공하는 구조는 아니다.

## 콘텐츠와 저작 경계

Player는 cooked manifest와 typed document를 소비한다. scene text parser나 source asset recovery가 필요한 package는 정상 source-free 수용 기준을 충족하지 않는다. Editor의 AuthoringPort가 제공하는 복구를 Player에서 호출할 수 있는 일반 fallback으로 해석해서는 안 된다.

미완료 요구는 legacy global run mode 의존 축소와 구성별 실행·종료·device-loss 검증이다. [레이어](../plans/EngineLayerSeparationPlan.md), [native 표시](../design/PlayerNativePresentation.md), [패키징 검증](CreatorBuildToolValidation.md)이 후속 정본이다.

## 구현 근거

- [Player/Player.vcxproj](../../Player/Player.vcxproj)
- [Player/PlayerMain.cpp](../../Player/PlayerMain.cpp)
- [Player/PlayerCommands.cpp](../../Player/PlayerCommands.cpp)
- [BuildTool/PlayerVerification.cs](../../BuildTool/PlayerVerification.cs)
- [Engine/RuntimeHost/EngineLaunchConfig.h](../../Engine/RuntimeHost/EngineLaunchConfig.h)
