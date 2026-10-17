# 2026-10-07 병합 감사 이후의 소비 계약

> 기준: master `493536e300e274cbf59989cdd62b20310c8ae64f`의 소스 정적 조사. 이 문서의 재작성에서 빌드·실행·성능 측정은 하지 않았다.

## 역사 보고의 범위

과거 4일간 PR 목록·병합 시각·merge SHA는 당시 감사의 증거였다. 이 문서는 현재 branch의 조상 관계나 GitHub 상태를 새로 검증하지 않고, 그 변화가 남긴 실행 계약을 현행 소스로 다시 설명한다. PR 개수와 완료 checklist를 현재 제품 수용률로 바꾸지 않는다.

## 지금 이어지는 호출 경로

Profiler는 producer stream→collector→bounded writer로 이어지고 별도 ProfilerViewerProcess가 인증된 IPC를 통해 관찰한다. 현재 snapshot/continuous/IPC format은 각각 독립 버전이다. 과거 “CEPROF v3”라는 단일 표기로 현재 파일 형식을 설명할 수 없다.

Scene open은 owner가 pending load를 만들고 scheduler에서 문서·asset을 준비한 뒤 owner completion 경계에서 적용한다. 취소·stale generation·epoch 변경은 GPU 완료와 별개의 판단이다. Player는 자체 native presentation을 소유하고 consumer lease를 최종 GPU 사용까지 보존한다. Vulkan presentation은 현재 CPU bridge이므로 native DX12 transport와 같다고 하지 않는다.

모델 렌더링은 METEOR의 명시적인 Geometry/Cull/Occluder/Surface/Shadow 연결로 이어진다. 옛 CEMC 버전과 CPU LOD 설명을 현재 codec·graph 계약에 재사용하지 않는다. 오디오의 현재 실행 계층은 SceneRuntime의 MiniaudioBackend이며 과거 FMOD 전환 계획은 더 이상 prerequisites가 아니다.

## 수용 기록의 사용

현재 정본은 [ProfilerContinuousRecording](../design/ProfilerContinuousRecording.md), [AsyncAssetPreparation](../design/AsyncAssetPreparation.md), [PlayerNativePresentation](../design/PlayerNativePresentation.md), [GpuDrivenMeshletDxrWiring](../design/GpuDrivenMeshletDxrWiring.md)이다. 각 문서의 구현/미지원/미검증 경계를 우선한다.

병합은 배포 closure, 장치별 품질, shutdown, 성능 측정의 대체 증거가 아니다. 남아 있는 과거 JSON·이미지·capture는 그때의 revision과 입력을 가진 독립 증거이며 이 재작성에서 결과를 변경하거나 재실행하지 않았다.

## 구현 근거

- [Engine/EngineDiagnostics/ProfilerViewerProcess.cpp](../../Engine/EngineDiagnostics/ProfilerViewerProcess.cpp)
- [Engine/SceneRuntime/SceneManager.cpp](../../Engine/SceneRuntime/SceneManager.cpp)
- [Player/PlayerMain.cpp](../../Player/PlayerMain.cpp)
- [Player/PlayerVulkanPresentation.cpp](../../Player/PlayerVulkanPresentation.cpp)
- [Engine/RenderEngine/Render/Scene/EnhancedSceneRenderer.cpp](../../Engine/RenderEngine/Render/Scene/EnhancedSceneRenderer.cpp)
- [Engine/SceneRuntime/Audio/MiniaudioBackend.cpp](../../Engine/SceneRuntime/Audio/MiniaudioBackend.cpp)
