# 메모리 snapshot의 현재 범위와 확장 계획

> 기준: master `493536e300e274cbf59989cdd62b20310c8ae64f`의 소스 정적 조사. 이 문서의 재작성에서 빌드·실행·성능 측정은 하지 않았다.

## 현재 수집 경계

Editor의 snapshot_service는 UI가 request를 증가시키고 GameThread의 `poll_game_thread`가 한 frame 경계에서 immutable snapshot을 게시한다. UI/Viewer가 DataSystem이나 CLR을 직접 읽지 않는다. DTO는 EngineDiagnostics의 ProfilerLiveDiagnostics 타입과 공유한다.

현재 수집은 OS process working set/private commit, Debug CRT 정보, CLR GC 통계, virtual address region과 자산 캐시 관찰을 구분한다. `VirtualQuery`의 private/image/mapped/reserve는 주소 영역이며 객체 할당 소유자나 실제 물리 상주량이 아니다.

asset 목록은 현재 model generation·material·texture 관찰을 사용하고 CPU texture bytes는 TextureImageCache의 liveBytes를 읽는다. 이는 SDK 내부 temporary, allocator overhead, 모든 native 객체 또는 GPU allocation graph의 완전한 census가 아니다. EngineResourceCensus도 선택된 Texture/Mesh/Material/Model instance 수만 세며 bytes를 세지 않는다.

## 해석 규칙

OS, CRT, CLR, asset live/retained와 VRAM은 중첩되는 축이다. 합산해 “총 엔진 메모리”를 만들지 않는다. cache에서 eviction된 뒤 consumer나 work가 pin한 bytes와 실제 OS 반환도 다르다. 비교 snapshot은 같은 workload/scene/frame 경계와 수집 capability를 기록해야 한다.

## 아직 구현·수용이 필요한 목표

과거 MP2~MP6 목표였던 allocation owner attribution, CLR object/reference graph, 개별 GPU resource 크기와 참조, 저장·비교 가능한 상세 snapshot 및 root 누수 분석은 현재 캐시 목록만으로 충족되지 않는다. GCCE root/trace 관계는 [NativeObjectMemoryPolicyPlan](NativeObjectMemoryPolicyPlan.md)와 연결해 별도 진단을 설계한다.

반복 scene load/unload, AssetDepot generation pin, CLR reload, background work drain 후 차이를 검증하고 수집 자체의 stall도 측정해야 한다. 이번 재작성에서는 GUI 확인이나 메모리 측정을 하지 않았다.

## 구현 근거

- [Editor/EngineGUIWindow/MemoryProfilerSnapshot.h](../../Editor/EngineGUIWindow/MemoryProfilerSnapshot.h)
- [Editor/EngineGUIWindow/MemoryProfilerSnapshot.cpp](../../Editor/EngineGUIWindow/MemoryProfilerSnapshot.cpp)
- [Engine/EngineDiagnostics/ProfilerLiveDiagnostics.h](../../Engine/EngineDiagnostics/ProfilerLiveDiagnostics.h)
- [Engine/Utility_Framework/EngineResourceCensus.h](../../Engine/Utility_Framework/EngineResourceCensus.h)
- [Engine/RenderEngine/DataSystem.h](../../Engine/RenderEngine/DataSystem.h)
