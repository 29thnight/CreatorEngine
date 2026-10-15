# Path Tracing·Hybrid RT 제품 구현 계획

기준: master `493536e300e274cbf59989cdd62b20310c8ae64f`의 소스와 실제 호출 배선. 이번 개정에서는 빌드·테스트·GPU 실행을 하지 않았다. 구현 존재와 실행 수용을 구분한다.

## 상태와 반드시 만들 두 환경

현재 BuildPipelineDesc는 raster/compute METEOR 파이프라인이다. 검토한 RHI와 실제 SceneHost 배선에서 BLAS/TLAS 생성·ray dispatch·PT 모드 실행을 확인하지 못했다. 아래는 미구현 의도와 수용 계획이며 현행 RT 기능 설명이 아니다.

제품 목표는 독립 Path Tracing 환경과 기존 raster에 RT 효과를 결합하는 Hybrid 환경이다. 두 모드는 같은 renderer owner·불변 scene/asset generation·graph·presentation 계약을 사용한다. support capability와 requested/active mode를 분리하고 unsupported 사유를 보여 준다. 별도 임시 renderer를 제품으로 남기거나 한 모드만 구현해 전체를 완료하지 않는다.

## 공통 기반

RTP-0는 현재 raster·asset·material·기능 baseline을 고정한다. RTP-1은 중립 RT 타입과 DX12 AS/pipeline/shader table, instance/primitive remap, upload·scratch·AS의 GPU 수명을 만든다. 현행 object-space posed stream은 가능한 입력이며 AS를 이미 제공하지 않는다. transform-only TLAS와 deform BLAS, static build/compaction, 두 view·두 in-flight frame의 비용과 세대를 분리한다.

RTP-2는 hit에서 실제 재질·texture·UV·normal·선택 LOD를 복원하고 BSDF evaluate/sample/PDF, 직접광·environment sampling과 MIS를 구현한다. raster shader의 임의 함수를 ray-hit 지원으로 간주하지 않는다. material closure·alpha·emission·dielectric 및 지원 제외의 의미를 명시한다.

RTP-3는 accumulation/reprojection/history reset과 denoise의 입력·소유권을 정의한다. camera·scene·material·lighting·resolution·mode 변경 시 reset하며 서로 다른 view/mode의 history를 섞지 않는다. temporal upscaler의 기존 state는 재사용 후보이지 RT denoise 구현 자체가 아니다.

## 모드별 수직 슬라이스

- RTP-4: 첫 Hybrid directional hard shadow. raster 결과·ray visibility·독립 oracle을 비교한다
- RTP-5: reflection/GI/refraction/volume/fog를 효과별 실험으로 추가한다. 전체 AS 비용을 포함해 채택·조건부·보류·기각을 결정한다
- RTP-6: PT camera ray·multi-bounce·NEE/MIS·Russian roulette로 progressive/reference 경로를 만든다
- RTP-7: interactive PT의 denoise·post·display와 mode 전환을 연결한다. reference와 실시간 품질 preset을 혼동하지 않는다
- RTP-8: ReSTIR PT Enhanced의 구현·평가·채택을 독립 연구 gate로 수행한다. 기본 unbiased/reference PT를 대체하는 선행이 아니다
- RTP-9/10: tetrahedral cages의 cook·remap·LOD와 runtime deformation을 구현·평가하고 deform BLAS와 비교한다. topology-changing geometry까지 자동 지원하지 않는다
- RTP-11: profile·설정·UI와 저장/재로드, active mode·fallback 표시를 연결한다
- RTP-12/13: 통합·수명·장시간·오류·품질·성능 증거와 최종 채택 결정을 닫는다

첫 범위는 DX12 single queue, opaque/masked/emissive/dielectric, progressive/reference와 interactive 사용, 연결성이 유지되는 변형이다. 모든 SSS·participating media·spectral·bidirectional·topology-changing 재질/기하를 약속하지 않는다. Vulkan RT 실제 구현은 별도 예산·지원 결정이며 단순 parity 검사만으로 추가되지 않는다.

## 비용과 검증

기존 RTP-0~13 추정은 순서대로 5/18/20/16/6/28/12/10/28/18/18/6/18/5인일, 총 208인일이었다. 기본 환경 116과 확장/연구 92의 2026-10-08 계획이며 실적·현재 잔여·납기 보장이 아니다. 130~290 범위도 통계적 신뢰구간이 아니다. RTP-0와 첫 prototype의 실제 입력·지원 범위로 다시 산정한다.

정확한 외부 버전·라이선스·상업 사용 허가 확인 전 Spartan 등 외부 구현의 도입을 전제하지 않는다. 알고리즘의 이름만으로 품질이나 속도를 주장하지 않는다. RT 미지원·build 실패·overflow·cancel·resize·device 전환은 명시적 결과를 가져야 한다. 총 CPU/GPU/VRAM과 cold/warm·AS 공유 전후를 함께 기록하고 DX12 correctness와 Release 비용, Vulkan 후속 수용을 분리한다.

## 소스 근거

- [Engine/RenderEngine/Render/Scene/EnhancedSceneRenderer.cpp](../../Engine/RenderEngine/Render/Scene/EnhancedSceneRenderer.cpp)
- [Engine/RenderEngine/MaterialGraphSceneHost.cpp](../../Engine/RenderEngine/MaterialGraphSceneHost.cpp)
- [Engine/RenderEngine/MaterialGraphSceneGeometryStage.inl](../../Engine/RenderEngine/MaterialGraphSceneGeometryStage.inl)
- [Engine/RenderEngine/RHI/IRenderDeviceServices.h](../../Engine/RenderEngine/RHI/IRenderDeviceServices.h)
- [Engine/RenderEngine/RHI/RHIEncoder.h](../../Engine/RenderEngine/RHI/RHIEncoder.h)
- [Engine/RenderEngine/Render/Scene/EnhancedTemporalViewState.h](../../Engine/RenderEngine/Render/Scene/EnhancedTemporalViewState.h)
