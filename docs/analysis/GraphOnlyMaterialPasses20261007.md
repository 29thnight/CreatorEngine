# Graph-only Scene 패스: 현재 선택 경계와 과거 전환 기록

> 기준: `493536e300e274cbf59989cdd62b20310c8ae64f`의 코드·호출 연결 정적 검토. 이 문서 갱신에서는 빌드·GPU 실행·성능 측정을 수행하지 않았다.

## 전환의 의미

2026-10-07의 목표는 재질 준비 실패를 native fallback으로 숨기지 않고 Scene 전체의 정확한 graph generation을 소비하는 것이었다. 현재 `EnhancedSceneRenderer::BuildPipelineDesc`와 Scene host stage 연결이 이 정책을 구현한다. 이전 단계의 패스 목록이나 host identity를 현재 ABI로 재사용하지 않는다.

## 실제 선택과 기록

`SelectReadyInput`은 frame/view/extent, material slot과 instance identity, coverage, geometry와 필요한 program을 검증한다. 하나라도 Pending이면 그 프레임을 defer하고, Failed이면 거부한다. 준비된 일부 재질만 새 프레임에 섞거나 이전 generation/native material로 대신 기록하지 않는다. 이미 완료된 화면이 남아 있을 수 있지만 그것은 새 프레임의 fallback draw가 아니다.

Geometry → First Cull → Occluder → Final Cull → Surface의 visibility 흐름과 별도 Shadow/LayerWork/Temporal 의존은 render graph resource handle과 epoch로 연결된다. 같은 이름의 texture나 이전 frame handle만으로 의존을 대체하지 않는다. 자세한 흐름은 [Scene host](../design/MaterialGraphSceneHost.md)에 모았다.

## 남는 native 경계

fullscreen 효과, Decal/Sprite, 공통 RHI/descriptor/residency 작업은 재질 compiler와 다른 계층이다. 이 코드가 남아 있다고 graph-only 정책이 깨진 것은 아니다. 반대로 standalone native pass/Code 회귀가 성공해도 live Scene material fallback이 허용되지는 않는다.

지원하지 않는 material feature는 compiler/route/input 단계에서 진단하고 거부한다. CPU asset owner의 생존, GPU residency, ready PSO, render graph 제출 성공은 각각 다른 조건이다.

## 역사적 검증의 사용

과거 DX12 검사 수·Vulkan 비동기 준비 실패 기록은 당시 코드를 설명하는 이력이다. 현재 revision의 backend 통과나 새 성능 측정으로 올리지 않는다. 재검증 시 모든 requested program ready, stale input 거부, 중간 실패 시 새 화면 미게시, 순차/병렬 기록의 동일 입력을 확인해야 한다.

이 보고서는 현재의 정적 연결만 확인했다. 제품 오류 복구와 재쿠킹은 [generation](../design/MaterialGraphSceneGeneration.md), [cook](../design/MaterialGraphSceneCook.md)을 따른다.

## 구현 근거

- [`Engine/RenderEngine/Render/Scene/EnhancedSceneRenderer.cpp`](../../Engine/RenderEngine/Render/Scene/EnhancedSceneRenderer.cpp)
- [`Engine/RenderEngine/MaterialGraphSceneHost.cpp`](../../Engine/RenderEngine/MaterialGraphSceneHost.cpp)
- [`Engine/RenderEngine/MaterialGraphSceneInput.cpp`](../../Engine/RenderEngine/MaterialGraphSceneInput.cpp)
- [`Engine/RenderEngine/MaterialGraphSceneStages.inl`](../../Engine/RenderEngine/MaterialGraphSceneStages.inl)
