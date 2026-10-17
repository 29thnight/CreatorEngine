# Material 제품 연결의 확인 지도

> 기준: `493536e300e274cbf59989cdd62b20310c8ae64f`의 코드·호출 연결 정적 검토. 이 문서 갱신에서는 빌드·GPU 실행·성능 측정을 수행하지 않았다.

## 실제 Editor 경로

Editor material session은 `.asset`/`.shadergraph`를 원본으로 불변 draft snapshot을 compile한다. `CompileMaterialGraphAuthoring`은 generated Slang, Scene host, reflection과 generated ShaderMeta를 검증한 generation을 준비한다. Apply는 expected accepted asset을 확인해 게시하고 Save는 그 accepted material과 같은 graph를 저장한다. Assign과 Scene 저장은 별도다.

Material typed property 수정은 common `LX::Runtime::Instance`와 graph stable parameter ID를 연결한다. model-derived 기본 graph는 `Assets/ModelMaterialGraph`와 DataSystem adapter를 통과한다. imported graph, user copy, inline override를 같은 identity로 덮지 않는다.

## Scene와 backend 경로

producer의 `SceneMaterialSource::Capture`에서 graph instance/coverage를 밀봉하고 SceneViewInput은 model/material owner와 현재 view/pose를 pin한다. SceneHost는 exact requested generation의 complete backend program을 준비하며 pending이면 프레임을 보류한다.

METEOR stage가 geometry/cull/hybrid visibility/material reconstruction/shadow/color/Volume/ordered Blend를 연결한다. live는 runtime split-sum이고 reference bake는 회귀·offline 경로다. DXIL/SPIR-V bytecode 생성 가능성과 실제 양 backend native PSO/그림/제출 수명 검증은 다르다.

## package 경로

AssetCooker의 자동 Scene cook은 양 backend stage를 요구한다. packaged Player는 catalog/bytes에서 generated contract와 bytecode를 복구하고 source compiler를 호출하지 않는다. Editor cache의 한 backend만 있는 product를 그대로 package할 수 없다.

## 검증을 단계별로 읽는 법

- 문서: ID/type/color-space/group 왕복, stale revision, 손상 primary 복구 정책
- 생성: exact semantic identity, source-map 진단, reflection/layout, unsupported feature 거부
- runtime: immutable instance/texture owner, recording/descriptor epoch, PSO 후보 실패 보존
- Scene: current pose/visibility/coverage, shadow/Decal, Blend/특수 transport 순서
- 배포: 양 backend complete stages, source-free 재개방, 실제 Player render와 정상 종료

이 단계의 증거를 서로 대체하지 않는다. 이전 날짜의 Debug/Release PASS나 helper probe count를 최신 commit의 제품 전체 수용으로 표시하지 않는다.

## 미완료 요구

제품 group/interface·Blackboard UI 마감, annotation/reroute gesture, full Blender node/enum 대응, SSS/closed-surface refraction 품질, texture/normal map/route/area-light 교차, moving-camera 실제 성능은 각각 독립 게이트다. 기존 구현을 다시 만드는 대신 [Material 계획](../plans/BlenderMaterialGraphPlan.md)과 [LX 계획](../plans/LatticeAdoptionPlan.md)의 잔여만 이어간다.

## 구현 근거

- [`Editor/EngineGUIWindow/MaterialGraphWindow.cpp`](../../Editor/EngineGUIWindow/MaterialGraphWindow.cpp)
- [`Engine/RenderEngine/DataSystem.cpp`](../../Engine/RenderEngine/DataSystem.cpp)
- [`Engine/RenderEngine/Assets/ModelMaterialGraph.cpp`](../../Engine/RenderEngine/Assets/ModelMaterialGraph.cpp)
- [`Engine/RenderEngine/MaterialGraphSceneHost.cpp`](../../Engine/RenderEngine/MaterialGraphSceneHost.cpp)
- [`Engine/RenderEngine/MaterialGraphSceneCompiler.cpp`](../../Engine/RenderEngine/MaterialGraphSceneCompiler.cpp)
- [`Tools/AssetCooker/AssetCooker.cpp`](../../Tools/AssetCooker/AssetCooker.cpp)
