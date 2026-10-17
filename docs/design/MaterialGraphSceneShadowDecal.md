# Graph shadow caster와 Decal의 surface 계약

> 기준: `493536e300e274cbf59989cdd62b20310c8ae64f`의 코드·호출 연결 정적 검토. 이 문서 갱신에서는 빌드·GPU 실행·성능 측정을 수행하지 않았다.

## 제품 연결

현재 shadow는 `METEOR.Scene.Shadow` stage가 Geometry output에서 caster 목록/culling/indirect 명령을 만들고 cascade target에 기록한다. `SceneCompileTargets`는 surface material에 shadow VS/MS/PS를 포함한다. 별도 legacy material pass를 숨겨 사용하는 설명은 현 graph-only Scene과 맞지 않는다.

shadow camera와 main camera는 같은 immutable material/geometry owner를 유지하지만 자기 가시성 목록을 가진다. main-view visible draw 수를 그대로 shadow caster 수로 사용할 수 없다. CPU prepared/logical mesh commands, indirect submission count, GPU survivor count도 구분한다.

## alpha와 pose

caster는 같은 generated material 의미와 coverage를 사용한다. Masked alpha와 double-sided/fixed state, 현재 pose/world/geometry identity가 색 pass와 일관되어야 한다. blend/transmission을 무조건 opaque shadow로 바꾸는 지원 약속은 하지 않는다. 구체 caster 선택은 stage의 queue/feature 분기를 따른다.

## Decal과 재평가

Surface가 GBuffer를 만든 뒤 Decal은 diffuse/normal/ORM을 수정한다. Color가 generated graph를 다시 평가할 때 decal 전 값을 잃지 않도록 pre-Decal baseline 3장을 별도 snapshot으로 전달한다. `EffectStage::DeclareColor`는 baseline이 0개 또는 완전한 3개인지, 실제 `Decal.Snapshot` writer가 있는지, 수정 target과 다른지 검사한다.

이 baseline은 물리 material 입력과 decal delta를 일관되게 합성하기 위한 것이다. 일부 channel만 연결하거나 수정 후 target을 원본 baseline으로 재사용하면 이중 적용/유실이 생긴다. refraction의 capture 단계도 같은 decal 계약을 사용한다.

## 자원과 검증

stage는 exact RGHandle/epoch와 depth/target을 확인하고 texture read·shadow write 의존성을 선언한다. 현재 frame의 generation과 geometry pin은 GPU 완료까지 유지한다. cache hit 또는 CPU caster count만으로 그림자가 맞다고 판정하지 않는다.

권장 회귀는 masked texture/parameter 변경, skin pose, view 밖 caster, double-sided, decal off/on/neutral channel, SSS/transmission과의 합성이다. 예전 2026-09-29 실행 기록은 현재 hybrid shadow target의 최신 통과가 아니다. 이번 문서에서는 실제 배선과 검증 분기만 확인했다.

## 구현 근거

- [`Engine/RenderEngine/MaterialGraphSceneShadowStage.inl`](../../Engine/RenderEngine/MaterialGraphSceneShadowStage.inl)
- [`Engine/RenderEngine/MaterialGraphSceneCompiler.cpp`](../../Engine/RenderEngine/MaterialGraphSceneCompiler.cpp)
- [`Engine/RenderEngine/MaterialGraphSceneEffectStage.inl`](../../Engine/RenderEngine/MaterialGraphSceneEffectStage.inl)
- [`Engine/RenderEngine/Render/Scene/EnhancedSceneRenderer.cpp`](../../Engine/RenderEngine/Render/Scene/EnhancedSceneRenderer.cpp)
