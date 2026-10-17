# 보관: 자산 GPU 상주 계획의 현재 후계

> 기준: master `493536e300e274cbf59989cdd62b20310c8ae64f`의 소스 정적 조사. 이 문서의 재작성에서 빌드·실행·성능 측정은 하지 않았다.

## 유지할 결정

자산 identity, CPU descriptor 소유권, GPU residency와 native allocation은 다른 층이다. 주소 재사용을 identity로 삼지 않고 exact generation을 잡는다는 옛 계획의 동기는 여전히 유효하다. 현재 CPU 준비는 AssetDepot, GPU cache의 퇴출 정책은 RHIAssetEvictionPolicy, 완료 대기는 RHICompletionRetireQueue가 소유한다.

## 현재 정책을 읽는 방법

기본 normal retire threshold는 120 frames, pressure threshold는 3 frames이고 최소 pressure target은 64MiB다. 정책은 정상 미사용 후보를 고르고 pressure에서는 최근 사용을 보호하며 last-used 순으로 추가 후보를 고른다. 이는 wall-clock 시간이나 즉시 VRAM 회수량을 보장하지 않는다.

retiredBytes는 cache에서 completion 대기 영역으로 이동한 논리 바이트다. 실제 native budget 감소는 GPU 완료 뒤 persistent segment trim 등에 달려 있다. retire completion 값 0은 일반 Collect가 회수하지 않는 quarantine이다. producer/consumer가 모두 멈춘 teardown에서만 Drain할 수 있다.

## 옛 완료 문구의 제한

2026-08 보고의 “모든 cache가 종료 때만 해제”와 raw pointer key 설명은 당시 문제이며 현행 전체 구현 설명으로 사용할 수 없다. 반대로 당시 완료 표기가 모든 texture·mesh·display lease·device-loss 경로를 검증했다는 뜻도 아니다.

현재 상세 계약은 [AssetDepot](../AssetDepotImplementationPlan.md), [OwnedBoundedPresentation](../../design/OwnedBoundedPresentation.md), [MemoryProfiler](../MemoryProfilerPlan.md)에 있다. 메모리 계측 수치를 더할 때 CPU live, retired logical, OS/device usage를 구분한다.

## 구현 근거

- [Engine/RenderEngine/RHI/RHIAssetEvictionPolicy.h](../../../Engine/RenderEngine/RHI/RHIAssetEvictionPolicy.h)
- [Engine/RenderEngine/RHI/RHIAssetEvictionPolicy.cpp](../../../Engine/RenderEngine/RHI/RHIAssetEvictionPolicy.cpp)
- [Engine/RenderEngine/RHI/RHICompletionRetireQueue.h](../../../Engine/RenderEngine/RHI/RHICompletionRetireQueue.h)
- [Engine/RenderEngine/AssetDepot/AssetDepot.cpp](../../../Engine/RenderEngine/AssetDepot/AssetDepot.cpp)
