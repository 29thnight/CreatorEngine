# PPL 컨테이너를 교체할 때의 동시성 경계

> 기준: master `493536e300e274cbf59989cdd62b20310c8ae64f`의 소스 정적 조사. 이 문서의 재작성에서 빌드·실행·성능 측정은 하지 않았다.

## 현재 남은 실제 표면

`ProxyCommandQueue`는 concurrent_queue를 이용한다. producer의 push와 consumer의 try_pop을 전제로 한 frame/proxy 전달 계약이므로 std::queue로 이름만 바꾸면 안전하지 않다. EditorAssetPresentation에도 texture import queue가 남는다. RenderScene의 concurrent_unordered_map include나 오래된 주석만으로 살아 있는 concurrent map 인스턴스를 단정하지 않는다.

옛 분석의 “렌더 큐 8종”, shader cache 소비자 수와 migration 비용은 당시 구조에 대한 수치다. 현재 전체 소스의 live consumer census로 재측정하지 않았으므로 목록과 수치를 현재 inventory로 제공하지 않는다.

## 교체 기준

단일 owner가 모든 mutation을 수행한다면 std 컨테이너가 기본값이다. 여러 producer와 한 consumer라면 명시적 mutex queue나 값 전달 inbox가 대안이다. 진짜 동시 append/lookup이 필요한 경우에는 해당 작업과 iteration/clear의 안정성을 분리해 검증한다. concurrent 컨테이너라는 이름이 element lifetime, 복합 check→insert 또는 clear와 순회의 안전성을 보증하지 않는다.

ProxyCommandQueue를 변경하려면 publish epoch, producer 종료, queue drain과 proxy payload owner를 함께 보존해야 한다. texture import queue는 filesystem callback과 Editor owner의 mutation 경계를 먼저 확인해야 한다. scheduler 통합은 container synchronization을 자동 해결하지 않는다.

## 남은 작업

교체 제안은 각 인스턴스의 실제 producer/consumer, 잠금, 종료·취소 경로를 기록한 뒤 진행한다. throughput뿐 아니라 lost/duplicate command, stale generation, shutdown 접근을 검증한다. 본 문서는 수정 계획의 기준이며 현재 PPL을 전면 제거했다는 보고가 아니다.

## 구현 근거

- [Engine/RenderEngine/ProxyCommandQueue.h](../../Engine/RenderEngine/ProxyCommandQueue.h)
- [Engine/RenderEngine/RenderScene.h](../../Engine/RenderEngine/RenderScene.h)
- [Editor/EngineEntry/EditorAssetPresentation.h](../../Editor/EngineEntry/EditorAssetPresentation.h)
- [Engine/Utility_Framework/JobScheduler.h](../../Engine/Utility_Framework/JobScheduler.h)
