# Sprite 색·깊이·motion 소비

기준: master `493536e300e274cbf59989cdd62b20310c8ae64f`의 소스와 실제 호출 배선. 이번 개정에서는 빌드·테스트·GPU 실행을 하지 않았다. 구현 존재와 실행 수용을 구분한다.

## 이 문서의 역할

Sprite 색·깊이·motion 소비에 관한 과거 단계별 기록을 현행 계약으로 다시 정리한다. 공통 용어·수명·실행 모델은 [RenderGraph 정본](../plans/RenderGraphDependencySchedulingPlan.md)을 따른다. 날짜가 붙은 파일명은 링크 호환을 위해 유지한다.

## 현재 구현과 호출 경로

EnhancedSpritePass는 입력 HDR이 있으면 Modify, 자체 output이면 Write한다. 원본 texture는 shader-read로 import하고 depth를 읽는다. signedDistance는 SDF text의 별도 샘플링 표식이다. Temporal.SpriteMotion은 기존 temporal targets를 수정해 sprite motion/mask를 보탠다.

## 변경·검증 기준

빈 sprite 목록이 color를 끊지 않아야 한다. alpha·깊이 가림·Canvas 순서·SDF/이미지 혼합과 이전 sprite incarnation/texture/UV 변화 시 history를 확인한다. 후보 개수와 실제 기록된 temporal item 수를 구분한다.

과거 로컬 Build/Artifacts 경로의 수치와 완료·기성 표시는 이번 소스 감사에서 재검증하지 않았으므로 현재 통과 결과로 옮기지 않는다. 제품 기본값, 특정 fixture 성공, 여러 backend의 제품 수용은 각각 판정한다.

## 소스 근거

- [Engine/RenderEngine/Render/Passes/Geometry/EnhancedSpritePass.cpp](../../Engine/RenderEngine/Render/Passes/Geometry/EnhancedSpritePass.cpp)
- [Engine/RenderEngine/Render/Scene/EnhancedTemporalViewState.h](../../Engine/RenderEngine/Render/Scene/EnhancedTemporalViewState.h)
- [Engine/RenderEngine/Render/Scene/EnhancedSceneRenderer.cpp](../../Engine/RenderEngine/Render/Scene/EnhancedSceneRenderer.cpp)
