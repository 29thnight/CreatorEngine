# MAT-9 2026-10-01 체크포인트의 현재 대응표

> 기준: `493536e300e274cbf59989cdd62b20310c8ae64f`의 코드·호출 연결 정적 검토. 이 문서 갱신에서는 빌드·GPU 실행·성능 측정을 수행하지 않았다.

## 이 문서의 역할

이 체크포인트는 고정 material reference 기반을 확보하면서 남은 품질·성능 문제를 후속 RenderGraph/GPU 구조 작업에 전달하기 위해 만들어졌다. 당시 32/34일, host 10, phase 진행률은 역사적 일정 표현이다. 현재 구현 완료율이나 남은 공수로 복사하지 않는다.

현재 기반은 schema/typed IR/compiler, immutable generation·binding·cook, graph-only GPU-driven Scene, ordered Blend와 Special transport, Editor asset authoring이다. 현행 계약의 기준은 [Material 계획](../plans/BlenderMaterialGraphPlan.md)과 [제품 통합](../design/MaterialGraphProductIntegration.md)이다.

## 그대로 보존할 품질 증거

- [GGX/박막](MAT9GgxClosureComparison.md): 당시 fixed film on/off 16/16씩 수용
- [HDRI](MAT9HdriConvergence.md): 당시 constant material 20/control 6 수용
- [bounded Volume](MAT9SpecialTransportComparison.md): 당시 homogeneous 단일 산란 8/8 수용
- [Special Surface](MAT9SpecialProfileCorrection.md): 7/18 미수용, closed-interface·SSS 잔여 오차
- [근접 성능](MAT9NearCameraPerformance.md): 과거 reference lookup spike 조사, current speedup 증거 아님

이 결과들은 서로 다른 범위/세대의 증거다. 숫자를 합산한 전체 “Material 통과율”을 만들지 않는다. JSON/이미지는 변경하지 않았으며 새 GPU 실행을 수행하지 않았다.

## 현재 달라진 제품 정책

live IBL은 최종 split-sum runtime evaluation이다. 고비용 reference의 BRDF1024/environment4096은 reference 검증 경로에 남고, idle refine을 약속하지 않는다. 현재 host identity34/cooked version12와 graph bindless 계약은 초기 checkpoint의 값보다 우선한다.

GPU-driven visibility·indirect batching은 Scene 제출/geometry 비용을 다루지만 SSS·refraction 오차나 모든 IBL 비용을 자동 해결하지 않는다. 현재 성능은 native-real 완료 frame과 per-frame provenance를 가진 새 capture로 판단해야 한다.

## 후속 작업의 의존 원칙

검증된 material 기반을 후속 RenderGraph·RHI·GPU Scene의 선행으로 사용할 수 있다. MAT-9 전체 수용을 모든 후속 착수의 선행으로 묶으면 “성능을 개선할 구조가 MAT-9 완료를 기다리는” 순환이 생긴다. 대신 알려진 품질/성능 baseline을 고정하고 구조 변경의 정확성·퇴행을 계속 검증한다.

미완료 요구는 texture/factor/normal-map와 route parity, area-light consumer, closed-surface exit refraction/SSS 공간 응답, 실제 모델 moving/near/fullscreen tier/cold-warm 비용이다. Vulkan의 전체 제품 수용은 [Backend parity](../plans/BackendParityPlan.md), RenderGraph·공통 pipeline 기능은 [Scriptable Render Pipeline](../plans/ScriptableRenderPipelinePlan.md)에서 소유한다.

옛 다른 세션의 조사/설계/임시 binary hash는 구현 완료나 현재 실행의 provenance가 아니다. 현재 판단에는 해당 코드와 새 측정 identity를 사용한다.

## 구현 근거

- [`Engine/RenderEngine/MaterialGraphProduct.h`](../../Engine/RenderEngine/MaterialGraphProduct.h)
- [`Engine/RenderEngine/MaterialGraphSceneHost.cpp`](../../Engine/RenderEngine/MaterialGraphSceneHost.cpp)
- [`Engine/RenderEngine/MaterialGraphSceneLookup.cpp`](../../Engine/RenderEngine/MaterialGraphSceneLookup.cpp)
- [`Engine/RenderEngine/Render/Scene/EnhancedSceneRenderer.cpp`](../../Engine/RenderEngine/Render/Scene/EnhancedSceneRenderer.cpp)
- [`Tools/regression/measure-material-scene-performance.ps1`](../../Tools/regression/measure-material-scene-performance.ps1)
- [`docs/analysis/MAT9HdriConvergenceTargets.json`](MAT9HdriConvergenceTargets.json)
- [`docs/analysis/MAT9SpecialSurfaceTargets.json`](MAT9SpecialSurfaceTargets.json)
