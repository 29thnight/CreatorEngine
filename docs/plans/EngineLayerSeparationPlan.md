# Editor·Player·공통 runtime의 실제 분리

> 기준: master `493536e300e274cbf59989cdd62b20310c8ae64f`의 소스 정적 조사. 이 문서의 재작성에서 빌드·실행·성능 측정은 하지 않았다.

## 제품 진입점

CreatorEditor와 Player는 다른 host다. Editor/EngineEntry/App.cpp와 Player/PlayerMain.cpp가 각자의 EngineLaunchConfig를 만들며, 공통 EngineBootstrap은 경로·설정·window·content callback을 적용한다. Editor는 저작 기능과 GUI를 소유하고 Player는 packaged content와 native presentation을 소유한다.

EngineLaunchConfig에는 아직 compatibilityRunMode가 있다. SceneManager와 TagManager가 legacy global mode를 소비하므로 “공통 runtime이 모든 제품 구분을 모른다”는 최종 목표는 완전히 달성한 상태가 아니다. 새 분기는 가능한 한 host composition 또는 capability에 두되 기존 전달 필드를 없는 것으로 문서화하지 않는다.

## 물리 프로젝트 경계

SceneRuntime은 scene·component·managed lifecycle·audio 등의 실행 계층이다. RenderEngine은 asset/runtime graphics와 렌더링을, Physics는 물리 서비스를, Utility_Framework와 GCCE는 공통 수명을 제공한다. Player.vcxproj의 직접 project reference에는 Editor가 없으며 CommandService는 non-Shipping에서만 연결된다. 이 사실은 직접 링크 경계의 확인이지 전체 DLL dependency graph의 실행 검증은 아니다.

RuntimeHost/CommandCore는 Editor 명령 UI와 Player whitelist가 공유하는 schema·result·session 계층이다. handler와 operation은 각 host에 남는다. Editor 전용 asset authoring은 AssetAuthoringPort로 경계를 통과하며 mounted Player가 원본을 복구·수정하는 fallback으로 사용하지 않는다.

## 배포와 ABI

EngineHost.targets는 host runtime DLL과 작은 launcher를 조합한다. HostAbi는 정수와 process-lifetime borrowed 문자열만 전달하며 engine object·allocation·exception을 DLL 경계로 넘기지 않는다. common Runtime 디렉터리와 Editor 전용 디렉터리는 loader의 명시적인 검색 정책으로 구분된다.

이 구조를 하나의 거대 Engine.dll로 다시 합치는 것이 현행 계획은 아니다. [패키징 경계](EnginePackagingPlan.md)와 [Player 직접 의존](../analysis/PlayerModuleBoundaryAnalysis.md)이 배포와 실행 역할을 설명한다.

## 남은 의도

legacy run-mode 소비 제거, 더 작은 공통 header 경계, 독립 host 수용 테스트는 후속 요구다. source-free asset packaging이나 Editor 없는 직접 링크만으로 모든 authoring 의존 제거를 선언하지 않는다.

## 구현 근거

- [Editor/EngineEntry/App.cpp](../../Editor/EngineEntry/App.cpp)
- [Player/PlayerMain.cpp](../../Player/PlayerMain.cpp)
- [Engine/RuntimeHost/EngineLaunchConfig.h](../../Engine/RuntimeHost/EngineLaunchConfig.h)
- [Engine/RuntimeHost/EngineBootstrap.h](../../Engine/RuntimeHost/EngineBootstrap.h)
- [Player/Player.vcxproj](../../Player/Player.vcxproj)
- [Tools/runtime/EngineHost.targets](../../Tools/runtime/EngineHost.targets)
- [Tools/runtime/HostAbi.h](../../Tools/runtime/HostAbi.h)
- [Engine/RenderEngine/Interfaces/AssetAuthoringPort.h](../../Engine/RenderEngine/Interfaces/AssetAuthoringPort.h)
