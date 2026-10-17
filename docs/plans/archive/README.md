# 보관 계획의 사용 범위

> 기준: master `493536e300e274cbf59989cdd62b20310c8ae64f`의 소스 정적 조사. 이 문서의 재작성에서 빌드·실행·성능 측정은 하지 않았다.

## 이 디렉터리를 읽는 방법

여기 있는 문서는 과거 전환의 결정을 현재 코드와 연결하는 호환 설명이다. 날짜가 붙은 완료율·공수·벤치마크를 최신 master의 상태로 읽지 않는다. historical JSON·TSV·이미지는 당시 입력과 revision을 가진 증거이며 새 실행으로 덮어쓰지 않는다.

## 모델 전환

[ModelImportPipelinePlan](ModelImportPipelinePlan.md)은 병렬 experiment 시대와 현재 제품 소비를 구분한다. [ModelAssetBigBangCutoverPlan](ModelAssetBigBangCutoverPlan.md)은 AuthorModelAsset의 UUIDv8/v2 sidecar/immutable generation 계약을 설명한다. [기준선](ModelAssetBigBangCutoverBaseline.md)은 고정 corpus를 새 결과와 비교할 때의 주의사항이다. runtime 요청은 [AssetDepot](../AssetDepotImplementationPlan.md), cache 재사용은 [AssetSetImportReceipts](../../design/AssetSetImportReceipts.md)가 정본이다.

## 자원과 구조

[AssetResidencyPlan](AssetResidencyPlan.md)은 논리 퇴출과 GPU completion 후 해제를 분리한다. [Phase5CouplingPlan](Phase5CouplingPlan.md)은 예전 include 절단을 현재 SceneRuntime·host·sealed frame 경계로 연결한다. Scene 객체 수명은 현재 GCCE이므로 [NativeObjectMemoryPolicyPlan](../NativeObjectMemoryPolicyPlan.md)을 우선한다.

## 재질

[Phase425ImplementationHistory](Phase425ImplementationHistory.md)는 저작·shader 생성·program 소비·최종 품질의 서로 다른 완료선을 설명한다. 실제 pipeline 연결은 [GpuDrivenMeshletDxrWiring](../../design/GpuDrivenMeshletDxrWiring.md), 일정과 상태는 [RenderPhaseRoadmap](../RenderPhaseRoadmap.md)에 있다.

새 기능을 추가할 때 보관 문서의 폐기된 fallback·A/B·폴더 구조를 구현 요구로 복원하지 않는다. 여전히 유효한 의도는 active owner의 계약과 실패 gate로 옮기고, 실행 증거가 없으면 미검증으로 남긴다.

## 구현 근거

- [Engine/RenderEngine/Assets/ModelAssetAuthoringTransaction.h](../../../Engine/RenderEngine/Assets/ModelAssetAuthoringTransaction.h)
- [Engine/RenderEngine/RHI/RHICompletionRetireQueue.h](../../../Engine/RenderEngine/RHI/RHICompletionRetireQueue.h)
- [Engine/SceneRuntime/SceneManager.h](../../../Engine/SceneRuntime/SceneManager.h)
- [Engine/RuntimeHost/EngineLaunchConfig.h](../../../Engine/RuntimeHost/EngineLaunchConfig.h)
