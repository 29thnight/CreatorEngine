# 렌더러 품질 개선의 기능별 완료선

기준: master `493536e300e274cbf59989cdd62b20310c8ae64f`의 소스와 실제 호출 배선. 이번 개정에서는 빌드·테스트·GPU 실행을 하지 않았다. 구현 존재와 실행 수용을 구분한다.

## 구현과 품질 수용을 분리한다

현재 렌더러에는 CSM, SSAO/SSGI/SSR/SSS, Fog, Bloom과 AgX/ACES tone mapping, temporal upscale과 UI 합성 경로가 있다. 파일·node 존재는 모든 재질·장치에서 시각 정확도와 성능이 수용되었다는 뜻이 아니다. 특히 RenderPassSettings에 남은 이름이 현재 삭제된 일반 Deferred pass의 존재를 의미하지 않는다.

## 보존하는 품질 작업

- RND-1: local reflection probe의 자산·capture·선택 우선순위·바인딩과 specular AO. 현재 전역 IBL 구현을 local probe 제품 완료로 계상하지 않는다
- RND-2: 공용 shadow sampling, point/spot atlas, quality tier·masked·normal-offset·far fade. 현재 3-cascade directional 경로와 다른 광원 종류의 지원을 구분한다
- RND-3: display OETF·tone mapping·exposure·bloom의 실제 순서와 품질. AgX/Bloom 구현이 이미 있으므로 전부 미착수라고 쓰지 않으며 자동 노출·출력 장치 정책은 별도 확인·구현 대상이다
- RND-ENV: background와 diffuse/specular IBL의 독립 설정·profile·쿠킹·실제 소비. 상세는 EnvironmentRenderingPlan을 따른다

## 게이트

pre-tone-map material golden과 표시 결과 golden을 분리한다. 효과별 off/on·parameter 경계·해상도·camera 이동·history reset과 성능을 같은 sealed 입력으로 검사한다. native golden은 temporal/spatial post 제외 증거를 확인한다. RGB 오차·누락/과노출·성능과 사용자 표시 결과를 각각 기록한다.

기존 계획의 DX12 Debug/Release 제품 게이트와 Vulkan 별도 parity 범위를 유지한다. 과거 28인일(RND-1~3 18 + ENV 10)은 당시 범위 추정이며 현재 구현을 반영한 잔여 확정치가 아니다. C# 저작 전환이나 material Principled 의미의 수용을 이 품질 작업의 완료로 대신하지 않는다.

## 소스 근거

- [Engine/RenderEngine/Render/Scene/EnhancedSceneRenderer.cpp](../../Engine/RenderEngine/Render/Scene/EnhancedSceneRenderer.cpp)
- [Engine/RenderEngine/Render/Passes/PostProcess/EnhancedPostChainPass.cpp](../../Engine/RenderEngine/Render/Passes/PostProcess/EnhancedPostChainPass.cpp)
- [Engine/RenderEngine/Render/Passes/PostProcess/EnhancedPostChainPass.h](../../Engine/RenderEngine/Render/Passes/PostProcess/EnhancedPostChainPass.h)
- [Engine/RenderEngine/Render/Passes/Geometry/EnhancedShadowPass.cpp](../../Engine/RenderEngine/Render/Passes/Geometry/EnhancedShadowPass.cpp)
- [Engine/RenderEngine/Interfaces/RenderPassSettings.h](../../Engine/RenderEngine/Interfaces/RenderPassSettings.h)
