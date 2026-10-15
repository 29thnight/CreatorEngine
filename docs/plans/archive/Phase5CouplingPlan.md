# 보관: 구조적 결합 제거와 현재 경계

> 기준: master `493536e300e274cbf59989cdd62b20310c8ae64f`의 소스 정적 조사. 이 문서의 재작성에서 빌드·실행·성능 측정은 하지 않았다.

## 폴더 이동보다 중요한 소비 방향

옛 ScriptBinder 중심 include 절단 수치는 과거 모듈 배치의 조사다. 현재 gameplay runtime은 Engine/SceneRuntime에 있으며 Editor의 저작/UI와 Player host는 별도 구성이다. 오래된 root ScriptBinder 경로를 새로운 분석 도구의 입력으로 재사용해서는 안 된다.

실제 render 경계는 owner가 만든 proxy·sealed frame 입력이다. EnhancedSceneRenderer의 GT BuildLiveFramePacket과 RT 소비를 따라 scene pointer를 읽는 위치를 확인한다. 타입 헤더를 Interfaces로 옮겼다는 사실만으로 live gameplay 접근이나 callback 수명이 제거된 것은 아니다.

## 명시적인 역전 지점

AssetAuthoringPort는 RenderEngine에서 Editor 저작 기능을 직접 소유하지 않도록 하는 계약이다. EngineLaunchConfig는 host가 정책과 path를 조립하고 bootstrap이 적용하도록 한다. Player의 직접 Editor project reference는 없지만 legacy compatibilityRunMode는 남아 있다. 따라서 결합 제거의 최종 목표를 모두 달성했다고 기록하지 않는다.

## include 감사의 한계

scripts/check_include_boundary.py와 allowlist는 정적 include 방향을 검사하는 도구다. source file이 어느 project와 preprocessor 구성에 속하는지, virtual callback이 어디서 실행되는지, shared state가 어떤 thread에 속하는지는 별도 검토가 필요하다. 예전 허용 간선 수를 현재 값으로 보존하지 않는다.

활성 후계는 [EngineLayerSeparationPlan](../EngineLayerSeparationPlan.md), [동기화 구조](../../analysis/EngineSynchronizationArchitecture.md), [자원 소유권](../../design/ResourceOwnershipDesign.html)이다. 포괄적 폴더 재구성은 현행 실행 지시로 부활시키지 않는다.

## 구현 근거

- [scripts/check_include_boundary.py](../../../scripts/check_include_boundary.py)
- [scripts/include_boundary_allowlist.txt](../../../scripts/include_boundary_allowlist.txt)
- [Engine/RuntimeHost/EngineLaunchConfig.h](../../../Engine/RuntimeHost/EngineLaunchConfig.h)
- [Engine/RenderEngine/Interfaces/AssetAuthoringPort.h](../../../Engine/RenderEngine/Interfaces/AssetAuthoringPort.h)
- [Engine/RenderEngine/Render/Scene/EnhancedSceneRenderer.h](../../../Engine/RenderEngine/Render/Scene/EnhancedSceneRenderer.h)
- [Player/Player.vcxproj](../../../Player/Player.vcxproj)
