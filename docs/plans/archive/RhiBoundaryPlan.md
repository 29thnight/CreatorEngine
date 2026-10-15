# RHI 경계의 현행 계약과 이식 계획 승계

기준: master `493536e300e274cbf59989cdd62b20310c8ae64f`의 소스와 실제 호출 배선. 이번 개정에서는 빌드·테스트·GPU 실행을 하지 않았다. 구현 존재와 실행 수용을 구분한다.

## 현재 중립 경계

IRenderDeviceServices는 upload·resource/cache·기록 준비 서비스를, RHIEncoder는 command 기록을 제공한다. format/state/handle/pipeline layout/queue 타입은 중립 헤더에 있고 DX12/Vulkan 구현이 실제 native 변환·리소스·제출을 소유한다. 이름만 공통이고 DX12 헤더를 반드시 포함해야 하는 인터페이스를 새로 만들지 않는다.

공용 pass의 Declare는 graph handle·access·state를 밝히고 ExecuteContext의 encoder로 기록한다. raw native pointer가 pass 경계의 일반 저장 형식이 되지 않게 한다. descriptor·upload slice·recording owner·GPU completion의 유효 기간은 API 호출 성공과 별도다.

## 이식 이후 추가된 핵심

upload reservation은 native admission이 아니다. OnUploadSubmitted·OnUploadAccepted·Completed·Aborted·Rejected를 구분하며 cache와 imported state는 정확한 admission·completion에서 게시/회수한다. ReserveUploadBatch는 한 segment의 all-or-none 요청 묶음이다.

persistent heap policy와 device memory budget coordinator는 여러 consumer의 압력·성장 예산을 공유한다. 후보 eviction age가 GPU 안전을 증명하지 않는다. scalar completion은 단일 graphics scope이며 multi-queue는 queue identity/timeline 계약이 필요하다. DX12 owned queue 구현을 Vulkan 동등 구현이라고 부르지 않는다.

## 구현·지원 판정

공통 헤더 존재, native include 건수, 예전 17개 pass 목록만으로 RHI 완료를 판정하지 않는다. 현재 METEOR mesh indirect·atomic/raster capability, temporal vendor API, upload/retire·resize/device-loss를 각 backend에서 확인해야 한다. 선언만 있고 호출되지 않는 표면과 실제 제품 사용을 구분한다.

옛 DX11 wrapper·고정 GBuffer/Deferred 이식 목록·Vulkan 20/20 결과는 당시 전환 기록이다. 이번 문서 개정은 현재 GPU 실행을 하지 않았다. 상세 현재 메모리/queue 설계는 [RhiGpuMemoryLifetimeDesign](../../design/RhiGpuMemoryLifetimeDesign.md)와 [RhiQueueContractQ0](../../analysis/RhiQueueContractQ0_20261008.md), 실제 교차 수용은 [BackendParityPlan](../BackendParityPlan.md)을 따른다.

## 유지하는 후속 원칙

필요한 실제 소비자와 함께 작은 end-to-end 경계를 추가한다. unsupported는 조용히 no-op 성공하지 않고 capability·오류로 드러낸다. 일반/진단/capture·Graphics/Compute·source generation을 구분하고 종료 시 모든 소비자가 멈춘 뒤 최종 owner를 해제한다. 과거 문서의 전체 완료 표시는 새로운 queue/range/RT 기능의 완료로 확장하지 않는다.

## 소스 근거

- [Engine/RenderEngine/RHI/IRenderDeviceServices.h](../../../Engine/RenderEngine/RHI/IRenderDeviceServices.h)
- [Engine/RenderEngine/RHI/RHIEncoder.h](../../../Engine/RenderEngine/RHI/RHIEncoder.h)
- [Engine/RenderEngine/RHI/RHIQueueContract.h](../../../Engine/RenderEngine/RHI/RHIQueueContract.h)
- [Engine/RenderEngine/RHI/RHIPersistentHeapPolicy.h](../../../Engine/RenderEngine/RHI/RHIPersistentHeapPolicy.h)
- [Engine/RenderEngine/RHI/RHIDeviceMemoryBudgetCoordinator.h](../../../Engine/RenderEngine/RHI/RHIDeviceMemoryBudgetCoordinator.h)
- [Engine/RenderEngine/RHI/DX12/DX12DeviceResources.cpp](../../../Engine/RenderEngine/RHI/DX12/DX12DeviceResources.cpp)
- [Engine/RenderEngine/RHI/Vulkan/VulkanRenderServices.cpp](../../../Engine/RenderEngine/RHI/Vulkan/VulkanRenderServices.cpp)
- [Engine/RenderEngine/Render/Graph/EnhancedRenderGraphQueues.cpp](../../../Engine/RenderEngine/Render/Graph/EnhancedRenderGraphQueues.cpp)

## 이전 기록

[기준 커밋의 개정 전 원문](https://github.com/29thnight/CreatorEngine/blob/493536e300e274cbf59989cdd62b20310c8ae64f/docs/plans/archive/RhiBoundaryPlan.md)에 당시 단계·세부 앵커·측정 이력이 남아 있다. 그 실행 결과를 현재 커밋의 수용으로 승계하지 않는다.
