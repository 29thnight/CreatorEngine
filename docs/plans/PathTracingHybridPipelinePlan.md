# Path Tracing·Hybrid RT 제품 구현 계획

기준: master `493536e300e274cbf59989cdd62b20310c8ae64f`의 소스와 실제 호출 배선. 이번 개정에서는 빌드·테스트·GPU 실행을 하지 않았다. 구현 존재와 실행 수용을 구분한다.

<a id="1-현재-기반과-연결-지점"></a>

<a id="연결-정본"></a>

## 상태와 반드시 만들 두 환경

현재 BuildPipelineDesc는 raster/compute METEOR 파이프라인이다. 검토한 RHI와 실제 SceneHost 배선에서 BLAS/TLAS 생성·ray dispatch·PT 모드 실행을 확인하지 못했다. 아래는 미구현 의도와 수용 계획이며 현행 RT 기능 설명이 아니다.

제품 목표는 독립 Path Tracing 환경과 기존 raster에 RT 효과를 결합하는 Hybrid 환경이다. 두 모드는 같은 renderer owner·불변 scene/asset generation·graph·presentation 계약을 사용한다. support capability와 requested/active mode를 분리하고 unsupported 사유를 보여 준다. 별도 임시 renderer를 제품으로 남기거나 한 모드만 구현해 전체를 완료하지 않는다.

<a id="2-세-모드와-공통-소유권"></a>

## 공통 기반

RTP-0는 현재 raster·asset·material·기능 baseline을 고정한다. RTP-1은 중립 RT 타입과 DX12 AS/pipeline/shader table, instance/primitive remap, upload·scratch·AS의 GPU 수명을 만든다. 현행 object-space posed stream은 가능한 입력이며 AS를 이미 제공하지 않는다. transform-only TLAS와 deform BLAS, static build/compaction, 두 view·두 in-flight frame의 비용과 세대를 분리한다.

RTP-2는 hit에서 실제 재질·texture·UV·normal·선택 LOD를 복원하고 BSDF evaluate/sample/PDF, 직접광·environment sampling과 MIS를 구현한다. raster shader의 임의 함수를 ray-hit 지원으로 간주하지 않는다. material closure·alpha·emission·dielectric 및 지원 제외의 의미를 명시한다.

RTP-3는 accumulation/reprojection/history reset과 denoise의 입력·소유권을 정의한다. camera·scene·material·lighting·resolution·mode 변경 시 reset하며 서로 다른 view/mode의 history를 섞지 않는다. temporal upscaler의 기존 state는 재사용 후보이지 RT denoise 구현 자체가 아니다.

<a id="3-구현연구-슬라이스"></a>

<a id="4-실행-순서와-기존-id-대응"></a>

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

<a id="5-restir-pt-enhanced-적용-계약"></a>

## ReSTIR PT Enhanced의 미구현 연구 요구

RTP-8은 기본 PT·RT 재질/광원·path/reservoir 기록·재투영과 visibility 재검증 뒤에 놓는다. SSGI에 단순 filter를 추가하는 작업으로 축소하지 않는다. reciprocal neighbor selection, footprint 기반 reconnection, duplication map, direct/GI reservoir 통합을 개별 변경으로 구현·비교한다는 요구를 유지한다. shift mapping의 PDF/weight 의미, 움직이는 geometry/light·disocclusion·얇은 표면·specular/transmission·반복 무늬의 실패 fixture가 필요하다.

고정 scene/seed의 고샘플 기본 PT reference에 대해 동일 spp와 동일 GPU budget의 오차를 모두 비교한다. reservoir 메모리, 추가 visibility ray, reuse와 denoise를 포함하고 편향·상관·잔상·수렴을 분리한다. 기존 계획이 지정한 [연구 입력](https://research.nvidia.com/labs/rtr/publication/lin2026restirptenhanced/)은 구현 전 다시 검토할 외부 자료이며 이번 감사에서 논문이나 외부 구현을 재검증하지 않았다. 외부 논문의 배수를 엔진 성능 보장으로 가져오지 않는다.

<a id="6-amd-tetrahedral-cages-적용-계약"></a>

## Tetrahedral cages의 미구현 연구 요구

RTP-9/10은 dense mesh를 tetrahedron별로 전처리하고 정적 mini-BLAS를 공유하며 instance cage 변형과 rest-pose ray 변환을 평가하는 별도 geometry 연구다. 연결성을 유지하는 식생을 첫 fixture로 삼고 Hybrid/PT가 같은 RT geometry route를 소비하도록 설계한다. cage가 raster pose와 일치하는지, 경계 clipping·ray distance·normal·winding·alpha·LOD 전환을 검사한다. degenerate/inverted cage의 거부·fallback도 필요하다.

exact deformation+BLAS update/rebuild, static reuse, cage를 같은 의미·오차 budget에서 비교한다. cook 크기, geometry/cage/AS/scratch 전체 VRAM과 CPU/GPU update·traversal을 기록한다. 변형 근사의 품질 한계와 필요한 DXR/SDK/device 기능은 prototype 전 확인한다. [기존 계획의 AMD 연구 입력](https://gpuopen.com/learn/how-tetrahedral-cages-significantly-reduce-bvh-memory-usage/)의 장면별 수치를 엔진 전체 VRAM 절감이나 현행 지원으로 옮기지 않는다.

<a id="7-공통-수용과-공수-확정"></a>

<a id="8-공수-산정--2026-10-08"></a>

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
