# RHI GPU 메모리와 완료 기반 수명

기준: master `493536e300e274cbf59989cdd62b20310c8ae64f`의 소스와 실제 호출 배선. 이번 개정에서는 빌드·테스트·GPU 실행을 하지 않았다. 구현 존재와 실행 수용을 구분한다.

## 네 가지 시점을 분리한다

CPU 메모리 예약, command recording, native queue admission, GPU 완료는 서로 다른 사건이다. RHIUploadTransactionState는 Recording·Queued·Resident·Quarantined를 구분하고, listener는 Submitted 예약 통지와 Accepted 실제 admission 통지를 구분한다. Submit 호출을 시도했다는 이유로 캐시를 재사용 가능한 resident로 게시하면 안 된다.

IRenderDeviceServices::ReserveUploadBatch는 묶음을 한 segment에서 all-or-none으로 예약한다. 실패 시 cursor와 출력이 변경되지 않는 계약이다. AllocateUpload 단건은 같은 의미 기반 요청을 사용한다. raw bytes/alignment overload는 호환 표면이며 새 호출은 usage를 지정한다.

## 장기 버퍼 heap과 예산

RHIPersistentHeapPolicy는 native 메모리를 직접 만들지 않고 compatibility key별 segment와 정렬된 offset 범위를 관리한다. 기본 segment는 64 MiB, dedicated 판단 기준은 32 MiB이며 구성으로 바꿀 수 있다. generation을 갖는 segment handle로 낡은 할당을 구분한다. 재사용된 DX12 범위는 requiresAliasingBarrier로 보수적 첫 접근 경계를 알린다.

DX12PersistentHeap와 VulkanPersistentHeap가 실제 resource/heap을 만들고 정책의 범위를 적용한다. 완전히 빈 segment만 trim 대상으로 되며 allocator free byte와 실제 device 예산 감소는 같은 값이 아니다.

RHIDeviceMemoryBudgetCoordinator는 한 물리 device의 여러 allocator가 같은 budget snapshot을 공유하게 한다. 새 segment 전에 TryReserveGrowth ticket을 받고 성공하면 CommitGrowth, 실패하면 CancelGrowth한다. snapshot 후 dedicated/committed allocation도 RecordAllocation으로 반영해 동시 과예약을 줄인다. DX12는 local memory domain, Vulkan은 heap별 budget을 게시한다. estimated budget은 실제 driver budget과 구분해서 보여야 한다.

## 캐시 은퇴와 quarantine

RHIAssetEvictionPolicy의 기본 미사용 기준은 정상 120 frame, pressure 3 frame이다. 이는 회수 후보 선정 기준이며 GPU 안전 판정이 아니다. pressure pass는 여러 cache가 공유하는 목표를 소비하고 최근 사용·upload pending 후보를 보호한다. retiredBytes는 completion 대기 묘지로 이동한 논리 자산 바이트이며 당장 줄어든 VRAM이 아니다.

RHICompletionRetireQueue는 완료점 이상에서만 payload releaser를 호출한다. 값 0은 제출 완료를 증명하지 못한 quarantine으로 Collect가 회수하지 않는다. Drain은 모든 producer/consumer가 멈춘 device teardown 같은 경계에서만 허용된다. 임의의 N-frame 지연으로 이를 대체하지 않는다.

RHICompletionPoint는 한 graphics queue의 scalar 값이다. 서로 다른 큐의 값을 비교할 때는 RHIQueueIdentity와 RHITimelinePoint를 사용한다. queue identity는 device incarnation과 queueId를 갖고 wait는 발행된 point·동일 장치·유효 timeline을 확인한다. device-lost/Unavailable을 완료로 취급하지 않는다.

## 그래프·descriptor·표시 소유권

EnhancedRenderGraph의 imported state writeback은 Compile에서 외부 상태를 확정하는 방식이 아니다. OnUploadAccepted가 해당 recording의 실제 admission 뒤 최종 상태를 게시한다. graph Reset/destructor는 caller가 GPU 완료를 보장한 다음 transient를 반납하는 계약이다.

Live RenderOnce는 제출한 graph를 display slot과 recorded batch lifetimeToken에 보존한다. graph를 다음 프레임에서 Reset하지 않고 immutable structural metadata만 복사한다. FG가 입력을 더 오래 읽으면 그 최종 소비까지 같은 graph owner가 살아 있어야 한다. descriptor version과 upload recording 신원도 PrepareParallel/recording 사이에 바뀌면 재사용을 거부한다.

## 남은 검증과 개선 요구

대형 mesh·texture·여러 view·resize·자산 교체·queue 실패를 실제 장치에서 반복하여 예약, accepted, pending, quarantined, native release 수가 맞는지 확인해야 한다. OOM 시 partial cursor·불완전 cache publication이 남지 않아야 한다. 예산 pressure에서 논리 eviction과 native segment trim을 각각 계측한다.

이 구조는 CPU GC처럼 GPU 메모리를 이동시키는 collector가 아니다. 지속적인 모든 자산의 무제한 residency, 자동 defragmentation, 모든 multi-queue aliasing을 보장하지 않는다. 과거 고정 업로드 한도·대형 메시 실패 기록은 현행 allocator의 재현 결과와 분리한다.

## 소스 근거

- [Engine/RenderEngine/RHI/IRenderDeviceServices.h](../../Engine/RenderEngine/RHI/IRenderDeviceServices.h)
- [Engine/RenderEngine/RHI/RHIResourceTypes.h](../../Engine/RenderEngine/RHI/RHIResourceTypes.h)
- [Engine/RenderEngine/RHI/RHIPersistentHeapPolicy.h](../../Engine/RenderEngine/RHI/RHIPersistentHeapPolicy.h)
- [Engine/RenderEngine/RHI/RHIDeviceMemoryBudgetCoordinator.h](../../Engine/RenderEngine/RHI/RHIDeviceMemoryBudgetCoordinator.h)
- [Engine/RenderEngine/RHI/RHIAssetEvictionPolicy.h](../../Engine/RenderEngine/RHI/RHIAssetEvictionPolicy.h)
- [Engine/RenderEngine/RHI/RHICompletionRetireQueue.h](../../Engine/RenderEngine/RHI/RHICompletionRetireQueue.h)
- [Engine/RenderEngine/RHI/DX12/DX12PersistentHeap.cpp](../../Engine/RenderEngine/RHI/DX12/DX12PersistentHeap.cpp)
- [Engine/RenderEngine/RHI/Vulkan/VulkanPersistentHeap.cpp](../../Engine/RenderEngine/RHI/Vulkan/VulkanPersistentHeap.cpp)
- [Engine/RenderEngine/Render/Graph/EnhancedRenderGraph.cpp](../../Engine/RenderEngine/Render/Graph/EnhancedRenderGraph.cpp)
- [Engine/RenderEngine/Render/Scene/EnhancedSceneRenderer.cpp](../../Engine/RenderEngine/Render/Scene/EnhancedSceneRenderer.cpp)
