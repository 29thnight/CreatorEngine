# 프레임·작업·표시의 동기화 경계

> 기준: master `493536e300e274cbf59989cdd62b20310c8ae64f`의 소스 정적 조사. 이 문서의 재작성에서 빌드·실행·성능 측정은 하지 않았다.

## 서로 다른 완료 시계

엔진에는 GT simulation 완료, scheduler dependency 완료, RT command recording, RHI submission/fence 완료, PT의 native Present가 있다. 어느 하나도 나머지 완료를 대신하지 않는다. AssetRequest의 Ready 역시 CPU descriptor 준비이며 GPU residency를 뜻하지 않는다.

EngineBootstrap은 profiler와 worker hook을 먼저 설치하고 scheduler를 시작한다. RuntimeFrame은 Initialization와 InputEvents 후 pause 분기를 처리하고, 구조 변경이 없는 조건에서 managed pre/post physics를 실행한다. Physics와 GameLogic 뒤 registration·physics/animation/script message·AI flush를 거쳐 post-physics를 마무리한다. pause 시 delta는 0으로 해석된다.

## GT에서 RT로

EnhancedSceneRenderer의 BuildLiveFramePacket은 GT에서 Scene/camera를 읽어 snapshot을 만든다. PublishLiveFrame은 소비 중 입력 하나와 교체 가능한 pending 하나를 관리한다. 같은 대상의 pending 갱신은 latest-wins여도 lifecycle delta를 누락하지 않도록 합친다. RT는 GPU credit을 확보한 다음 최신 입력을 선택한다. frame age 목표는 절대 latency 상한의 증명이 아니다.

Scene 교체와 GCCE collection은 render join이 보장된 owner 경계에서 수행한다. worker가 live Scene을 고치거나 RT가 GT 객체를 임의 순회하는 방식으로 작업을 분산하지 않는다.

## RT에서 PT로

renderer는 완료 이미지를 display sink에 알린다. Player의 presentation thread가 acquire·composition·resize·pacing·Present를 소유한다. callback은 PT를 깨울 뿐 RT에서 presentation GPU work를 기록하지 않는다. consumer lease와 completion fence가 producer slot 재사용 가능성을 결정한다. CPU 참조 감소나 Present 반환으로 완료를 추정하지 않는다.

## 종료 순서

publication을 닫고 RT를 drain/join한 뒤 callback과 scene renderer를 정리한다. PT도 명시적으로 stop/join하고 sink 연결을 끊는다. 공통 runtime 종료는 SceneManager를 파괴한 다음 scheduler를 drain/shutdown하고 후속 singleton·DataSystem을 정리한다. scheduler worker 안에서 blocking wait나 shutdown을 호출하지 않는다.

이 순서는 deadlock·UAF 검토의 시작점이다. 모든 lock 순서와 device-loss 복구를 정적 조사만으로 안전하다고 인증하지 않는다. [scheduler](../design/JobSchedulerDesign.md), [표시 소유권](../design/OwnedBoundedPresentation.md)을 함께 읽는다.

## 구현 근거

- [Engine/RuntimeHost/EngineBootstrap.h](../../Engine/RuntimeHost/EngineBootstrap.h)
- [Engine/SceneRuntime/RuntimeFrame.cpp](../../Engine/SceneRuntime/RuntimeFrame.cpp)
- [Engine/SceneRuntime/SceneManager.cpp](../../Engine/SceneRuntime/SceneManager.cpp)
- [Engine/RenderEngine/Render/Scene/EnhancedSceneRenderer.h](../../Engine/RenderEngine/Render/Scene/EnhancedSceneRenderer.h)
- [Player/PlayerMain.cpp](../../Player/PlayerMain.cpp)
- [Engine/Utility_Framework/ThreadPool.cpp](../../Engine/Utility_Framework/ThreadPool.cpp)
