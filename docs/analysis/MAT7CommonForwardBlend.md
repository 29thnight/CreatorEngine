# MAT-7 alpha Blend의 현재 순서와 호환 범위

> 기준: `493536e300e274cbf59989cdd62b20310c8ae64f`의 코드·호출 연결 정적 검토. 이 문서 갱신에서는 빌드·GPU 실행·성능 측정을 수행하지 않았다.

## alpha와 route를 분리한다

현재 material coverage와 shading tier/route는 독립된 계약이다. 일반 alpha Blend는 opaque depth를 읽으며 색을 back-to-front 순서로 합성한다. opaque depth/visibility owner를 덮지 않고 alpha 0은 색을 남기지 않는다. Special transmission은 alpha 자체가 아니라 별도의 transport이므로 Blend의 성공만으로 유리/SSS/Volume 정확성을 주장하지 않는다.

## 현재 Scene 연결

Scene host의 LayerWork/LayerStage는 각 draw가 필요한 lookup·transport 준비와 순서 있는 색 합성을 연결한다. 현재 live 재질은 graph-only다. 과거 문서의 Code/Core/Layered/Code 혼합 순서 fixture는 명시적 helper 회귀이며 현재 제품 admission 목록이 아니다.

공통 light/cascade shadow/opaque depth는 renderer가 준비한 입력을 사용한다. Blend surface에 opaque SSAO/Decal/opaque caster 의미를 무조건 적용하지 않는다. scratch lookup과 opaque temporal cache는 역할·owner·수명이 다르며 frame과 completion 경계를 넘겨 무단 공유하지 않는다.

## 버전과 binding 주의

초기 host 11의 t16..127 설명은 현재 Graph binding ABI가 아니다. 현행 Graph texture는 t16384부터의 bindless 배열, descriptor index와 uniform의 별도 계약을 사용한다. Code helper resource 범위와 Graph product layout을 혼동하면 안 된다. 정확한 register/용량은 [제품 계약](../design/MaterialGraphProduct.md)을 따른다.

## 검증을 읽는 방법

`material_forward_blend_tests.inl`의 격리 색·순서 반전·카메라 방향·opaque occlusion·alpha 0·light overflow 사례는 합성 불변식을 확인하기 위한 것이다. 독립 alpha recurrence와 비교하는 GPU 성분 수는 Blender BSDF 일치나 FPS를 측정하지 않는다.

초기 DX12/Vulkan 실행과 Vulkan 준비 timeout 조정은 역사적 결과다. timeout을 늘려 correctness fixture가 완료된 사실을 냉간 PSO 성능 개선으로 해석하지 않는다. 새 검증은 실제 제품 graph-only draw, 순차/병렬 기록, layered transport와 함께 수행해야 한다.

## 한계와 남은 요구

정렬된 draw 합성은 OIT나 triangle-level 정렬이 아니다. self-overlap/interpenetration·복잡한 투과 내부 경로는 별도 문제다. alpha+transmission·강한 Volume이 있는 장면의 이미지 오차와 camera-motion 안정성은 [Forward transport](MAT7ForwardTransportComposition.md) 및 [품질 계획](../plans/BlenderMaterialGraphPlan.md)에 남는다.

## 구현 근거

- [`Engine/RenderEngine/MaterialGraphSceneLayerStage.inl`](../../Engine/RenderEngine/MaterialGraphSceneLayerStage.inl)
- [`Engine/RenderEngine/MaterialGraphSceneHost.cpp`](../../Engine/RenderEngine/MaterialGraphSceneHost.cpp)
- [`Engine/RenderEngine/MaterialGraphProduct.h`](../../Engine/RenderEngine/MaterialGraphProduct.h)
- [`Engine/RenderEngine/Render/Graph/EnhancedForwardLighting.h`](../../Engine/RenderEngine/Render/Graph/EnhancedForwardLighting.h)
- [`Tools/regression/material_forward_blend_tests.inl`](../../Tools/regression/material_forward_blend_tests.inl)
