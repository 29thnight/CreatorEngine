# Graph-only 정리 이후의 준비·실패·shadow cache 경계

> 기준: `493536e300e274cbf59989cdd62b20310c8ae64f`의 코드·호출 연결 정적 검토. 이 문서 갱신에서는 빌드·GPU 실행·성능 측정을 수행하지 않았다.

## 현재 정리 결과

2026-10-08 cleanup의 유효한 목적은 중복 material 준비와 native fallback 분기를 제거하면서 공유 renderer 준비를 보존하는 것이다. 현재 제품의 graph-only 선택은 [패스 전환](GraphOnlyMaterialPasses20261007.md), 실제 리소스 기록은 [pass recording](../design/MaterialGraphPassRecording.md)을 정본으로 삼는다.

DX12/Vulkan의 공통 `PrepareSceneRecording` 경계는 residency를 확보한 뒤 recording/descriptor boundary, palette, Decal/Sprite 준비와 graph 준비를 연결한다. material bind helper는 texture 상태 전이를 대신 수행하지 않는다. 동일 recording에서 검증된 packet 공유는 가능하지만 device/recording/descriptor version/layout이 달라지면 재검증해야 한다.

## 실패를 성공으로 바꾸지 않는 게시

CPU 입력 봉인, PSO 준비, render graph 기록, native 제출, GPU 완료를 따로 관리한다. 제출 큐가 ticket을 받았다는 이유로 native recording 성공 또는 GPU 완료를 선언하지 않는다. 현재 frame의 모든 단계가 성공한 경계에서만 해당 cache의 제출 상태를 게시한다.

standalone native material helper를 남기는 것은 회귀와 명시적 도구 경로의 호환성이다. live Scene draw에 우회 경로를 복구하거나 unknown material을 기본 shader로 바꾸는 근거로 사용하지 않는다. 실패 시 accepted asset/generation은 보존하지만 새 입력과 섞어 새 프레임을 만들지 않는다.

## CSM cache는 별도 미구현 요구

shadow caster 계산을 줄이려면 cached depth와 그 depth를 소비하는 정확한 light/cascade/caster/material/pose identity를 함께 관리해야 한다. 단순히 writer만 건너뛰고 clear를 남기면 cache를 지운다. clear와 graph writer를 같이 생략하고, resource import/dependency·native 성공·invalidate 조건을 묶어야 한다.

이 cleanup 자체가 완전한 persistent CSM cache를 구현하거나 검증한 것은 아니다. 움직이는 light/skin/alpha-mask와 실패 제출의 stale depth 방지는 후속 gate다.

## 과거 기록과 재검증

기존 보고서의 실행 횟수와 validation 결과는 당시 출력이다. 현재 코드의 존재만으로 같은 결과를 재주장하지 않는다. 새 검증에서는 모든 backend의 recording reset/descriptor 재사용, 실패 뒤 accepted frame 보존, Decal/Sprite와 graph의 공존, CSM clear/writer 쌍을 각각 확인해야 한다.

## 구현 근거

- [`Engine/RenderEngine/Render/Scene/EnhancedSceneRenderer.cpp`](../../Engine/RenderEngine/Render/Scene/EnhancedSceneRenderer.cpp)
- [`Engine/RenderEngine/MaterialGraphSceneHost.cpp`](../../Engine/RenderEngine/MaterialGraphSceneHost.cpp)
- [`Engine/RenderEngine/MaterialGraphRenderBindings.cpp`](../../Engine/RenderEngine/MaterialGraphRenderBindings.cpp)
- [`Engine/RenderEngine/MaterialGraphSceneShadowStage.inl`](../../Engine/RenderEngine/MaterialGraphSceneShadowStage.inl)
