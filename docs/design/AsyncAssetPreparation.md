# 비동기 CPU 준비와 Scene owner-thread 게시

> 기준: master `493536e300e274cbf59989cdd62b20310c8ae64f`의 소스 정적 조사. 이 문서의 재작성에서 빌드·실행·성능 측정은 하지 않았다.

## 비동기 범위

`SceneManager::BeginSceneLoad`는 owner thread에서 요청 ID와 activation epoch를 만든다. `SubmitScenePreparation`의 scheduler callback은 PendingSceneLoad만 보유하고 문서를 parse한 뒤 `DataSystem::PrepareSceneAssets`를 호출한다. callback이 SceneManager/Scene/Entity를 capture해 worker에서 scene mutation을 하는 구조가 아니다.

문서의 `m_requiredLoadAssetsBundle` 또는 legacy `AssetsBundle`을 hint로 읽지만, 현재 문서에서 실제 요구되는 dependency를 준비하는 주체는 DataSystem이다. callback은 추가 작업을 제출한 뒤 돌아오므로 단일 worker에서 하위 작업을 기다리는 교착을 피하는 형태다.

## 완료와 handoff

owner의 `CompleteSceneLoads`가 preparation/job 상태와 CPU 자산 readiness를 poll한다. asset 변경으로 preparation이 outdated인 경우 현재 구현은 최대 8회 재준비하고, genuine failure를 무한 재시도하지 않는다. `SceneAssetHandoff`는 게시 중 asset root/revision 변경과의 경계를 담당한다.

`BuildPreparedScene`은 owner에서 GC Scene candidate를 만들고 entity를 역직렬화한다. DDOL identity 중복 생성을 피하고 prefab/index를 재연결하며 hierarchy bulk build와 world matrix 동기화를 마친다. 준비된 texture image pin을 scene에 보관한다. 취소·종료·activation epoch 교체·asset revision 변경을 재검사한 후 `AdoptScene`으로 넘기며 실패 candidate는 retire한다.

## 수명과 오류

`AssetRequest` Ready는 CPU object만 준비되었다는 뜻이다. GPU upload, PSO 준비, render publication, 실제 화면 표시 완료는 뒤의 별도 경계다. consumer Cancel 이후에도 이미 수락된 work/capture는 정상 drain될 수 있다. Completion token을 받은 것이 GT/RT blocking을 허용하지 않는다.

문서 parse/decode/I/O는 worker로 옮겨졌지만 entity 구성, script/lifecycle, physics handoff 및 일부 host 동기 작업은 여전히 owner에 남는다. 따라서 전체 scene load가 프레임 영향 없이 완료된다는 약속은 없다.

## 유지할 수용 조건

한 worker, 빠른 연속 scene 선택, load 중 asset import, Play/Stop/종료, malformed 문서와 dependency failure, DDOL/prefab 재연결을 실제로 검증해야 한다. source path가 유지되는 Editor authoring과 source-free Player의 cooked 문서 경로도 별도 판정한다. API 전체는 [AssetDepotImplementationPlan](../plans/AssetDepotImplementationPlan.md)에 연결된다.

## 구현 근거

- [Engine/SceneRuntime/SceneManager.cpp](../../Engine/SceneRuntime/SceneManager.cpp)
- [Engine/SceneRuntime/SceneManager.h](../../Engine/SceneRuntime/SceneManager.h)
- [Engine/RenderEngine/DataSystem.h](../../Engine/RenderEngine/DataSystem.h)
- [Engine/RenderEngine/AssetDepot/AssetRequest.h](../../Engine/RenderEngine/AssetDepot/AssetRequest.h)
