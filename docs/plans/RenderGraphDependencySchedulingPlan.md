# RenderGraph 의존성·수명·기록 계약

기준: master `493536e300e274cbf59989cdd62b20310c8ae64f`의 소스와 실제 호출 배선. 이번 개정에서는 빌드·테스트·GPU 실행을 하지 않았다. 구현 존재와 실행 수용을 구분한다.

## 현재 구현의 기준

제품 라이브 렌더러는 ExplicitVersioned와 DependencyOrder를 명시한다. DeclarationOrder는 기본 생성자 모드와 기존 진단 소비자를 위한 별도 계약이고, ExplicitSingleWriter는 단일 writer의 명시적 접근을 정렬하는 모드다. 버전 선언 방식과 실행 순서 정책을 같은 옵션으로 취급하지 않는다.

EnhancedRenderGraph가 유일한 실행 그래프다. LivePipelineDesc는 슬롯 버전을 연결하는 조립 기술이다. C# 저작 그래프나 별도 실행 엔진을 구현된 것으로 전제하지 않는다.

## 핸들·접근·버전

RGHandle에는 index, version, kind, epoch가 있다. IsValid는 invalid index 여부만 확인하므로 올바른 그래프의 최신 리소스라는 보증이 아니다. Compile의 ValidVersionHandle과 선언 검사까지 통과해야 한다. texture와 buffer는 kind를 구분하고 한 물리 import는 하나의 논리 버전 계보를 갖는다.

Write는 이전 내용을 사용하지 않는 다음 버전을, Modify는 이전 내용을 읽고 갱신하는 다음 버전을 만든다. imported v0에 직접 쓰거나, stale parent에서 분기하거나, 한 pass에 같은 리소스를 중복 선언하면 오류다. 버전별 일반 writer는 하나여야 하며 사용한 모든 비-import 버전에는 producer가 있어야 한다.

AtomicMin64는 8바이트 정렬된 지원 UAV buffer의 제한된 결합 쓰기 계약이다. 일반적인 다중 writer 허용이 아니다. device capability와 storage 유효성을 구조 캐시를 재사용할 때도 다시 검사한다.

## 컴파일과 컬링

Compile은 선언 오류·최종 상태·반복 pass를 검사하고 구조 신원을 생성한다. 완전히 동일한 structural key면 order, waves, edges와 culling 결과를 재사용한다. 그렇지 않으면 순서를 만들고 불필요한 pass를 제거한 다음 dependency wave를 계산한다. 그 뒤 transient 생성과 barrier 계획을 매번 수행한다.

생존 root는 side effect와 외부에 게시되는 imported 출력이다. 읽기 데이터 의존성이 생산자를 유지한다. WAR/WAW는 살아남은 접근의 안전한 순서를 정하지만 버려진 버전이나 dead reader를 불필요하게 살리지 않는다. stable tie-break만 저작 순서를 사용하며 순환·누락 producer를 임의 순서로 숨기지 않는다.

## 리소스 상태와 메모리

RGPassUsage는 사용 상태와 접근 의미를 함께 선언한다. depth write, shader read, UAV, indirect argument 등의 전이는 그래프가 유도한다. imported 리소스의 최초 상태와 RequireImportedFinalState를 정확히 알려야 외부 owner와 이어진다. 반복·split pass는 내부 phase의 접근까지 명시한다.

Transient pool 재사용은 호출자가 GPU 완료를 확인한 뒤에만 가능하다. 풀에는 마지막 상태도 함께 돌아간다. optional placed-resource aliasing은 실제 수명·배치와 clear 조건을 검증하며, 자원을 단순히 마지막 CPU callback 후에 재활용하지 않는다. owned multi-queue 경로는 aliasing을 허용하지 않는다.

## CPU 기록과 GPU 순서

RecordParallel은 준비된 command pool·descriptor 세대가 맞는지 검사하고 worker별 기록 단위를 wave로 배치한다. GPU RAW/WAR/WAW가 곧 CPU callback 간 데이터 전달은 아니다. DeclareRecordingIndependent는 준비된 불변 입력만 사용하는 callback에 대해 CPU join을 줄이는 약속이고, DeclareRecordingSideEffect는 CPU 부수효과의 순서를 보존한다. 잘못된 independent 표시는 데이터 경합을 만들 수 있다.

그래프·pass capture와 batch lifetime token은 제출 GPU 작업이 끝날 때까지 살아 있어야 한다. queue 실행을 시작한 그래프는 실패했더라도 재특수화된 상태이므로 완료 회수와 Reset 없이 일반 Execute로 재사용할 수 없다.

## 멀티큐 정책

BuildQueueSchedule은 graphics/compute capability, cross-queue timeline, 측정 비용, 명시적 compute 호환성·상태를 확인한다. 일반 프레임과 capture 측정은 별도 domain이다. 비용이 없거나, 상태가 불법이거나, 예측 이득이 부족하면 graphics에 남는다. Overlap은 handoff 비용을 포함해 DAG의 겹침을 찾으며 Threshold는 결정적 계약 검사에 쓰이는 별도 정책이다.

SubmitQueues는 fresh owned graph, 일치하는 device generation, 실제 queue owner와 비-alias 자원을 요구한다. 같은 자원의 reader joins, release/acquire, signal/wait와 마지막 graphics join을 보존한다. 계획의 예측 span은 측정된 GPU 시간도 성능 보장도 아니다.

## 남은 수용과 변경 순서

현재 구현을 보완할 때는 다음 순서로 근거를 모은다.

1. 잘못된 버전·중복 import·순환·누락 producer·unsupported state를 의도적으로 넣어 거부를 확인한다
2. 같은 장면·재질·shader·해상도·backend에서 attachment와 실행 topology를 비교한다
3. CPU compile/prepare/record/schedule/submit, queue별 GPU span, 실제 frame 비용을 각각 측정한다
4. normal/capture domain, real/generated frame과 native/reconstructed 상태가 섞이지 않았는지 검사한다
5. resize·device 재생성·중도 실패·모든 queue 완료 후 자원 회수까지 확인한다

RG1~RG8이라는 과거 단계명이나 PR 병합은 이 기준의 현재 실행 통과를 대신하지 않는다. 구조 캐시·aliasing·compute queue 각각을 켠 결과와 끈 결과를 구별하고, 독립 그래픽 작업이 없는 장면의 graphics fallback도 정상 결과로 인정한다.

## 소스 근거

- [Engine/RenderEngine/Render/Graph/EnhancedRenderGraph.h](../../Engine/RenderEngine/Render/Graph/EnhancedRenderGraph.h)
- [Engine/RenderEngine/Render/Graph/EnhancedRenderGraph.cpp](../../Engine/RenderEngine/Render/Graph/EnhancedRenderGraph.cpp)
- [Engine/RenderEngine/Render/Graph/EnhancedRenderGraphQueues.cpp](../../Engine/RenderEngine/Render/Graph/EnhancedRenderGraphQueues.cpp)
- [Engine/RenderEngine/Render/Scene/EnhancedSceneRenderer.cpp](../../Engine/RenderEngine/Render/Scene/EnhancedSceneRenderer.cpp)
- [Engine/RenderEngine/RHI/RHIQueueContract.h](../../Engine/RenderEngine/RHI/RHIQueueContract.h)
