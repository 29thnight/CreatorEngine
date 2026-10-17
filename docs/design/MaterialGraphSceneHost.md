# METEOR Scene의 Material 실행 호스트

> 기준: `493536e300e274cbf59989cdd62b20310c8ae64f`의 코드·호출 연결 정적 검토. 이 문서 갱신에서는 빌드·GPU 실행·성능 측정을 수행하지 않았다.

## 제품 진입점

`EnhancedSceneRenderer::BuildPipelineDesc`가 live pipeline의 stage와 blackboard 배선을 조립한다. `material_graph::SceneHost`는 준비된 graph instance, geometry, GPU visibility, material reconstruction, 조명과 특수 transport를 연결한다. LX는 노드 언어 이름이고 제품 pass/resource는 `METEOR.Scene.*` 이름을 사용한다.

producer가 만든 immutable `SceneViewInput`을 `SelectReadyInput`으로 검증하고, texture copy/residency를 병렬 recording prefix 전에 준비한 뒤 `Prepare`에서 현재 recording의 geometry·binding·효과 자원을 만든다. worker는 mutable Material/Camera/Animator를 다시 읽지 않는다.

## 실제 패스 순서와 데이터 흐름

핵심 geometry 의존성은 `Geometry → Cull.First → Occluder → Cull.Final → Surface`다. Geometry는 pose를 적용한 object-space stream과 frame table을 게시한다. 첫 cull 목록은 occluder hybrid HW/SW raster로 들어가며 최종 cull은 회복된 opaque 목록과 ordered blended 목록을 게시한다. Surface는 최종 visibility에서 재질을 재구성해 GBuffer를 채운다. cull stage가 raster까지 수행하는 구조로 설명하지 않는다.

`Geometry → Shadow`는 별도 가지다. Shadow가 자신의 caster 목록, culling과 indirect 명령을 소유한다. `LayerWork`는 blended list를 받아 capture/color용 work를 만든다. 각 consumer는 producer가 반환한 정확한 RGHandle을 받아야 하며 누락·다른 epoch·다른 target을 거부한다.

Surface 이후 Decal은 diffuse/normal/ORM을 수정하고 pre-Decal baseline을 별도로 남긴다. Color는 재질과 조명/AO/그림자 입력을 읽어 HDR을 만든다. Volume composite와 뒤에서 앞으로 정렬된 Blended stream은 같은 frame의 medium/light tile을 사용한다. Temporal은 현재 visibility와 material/layer work의 계약을 사용하며 예전 별도 CPU draw 우회를 가정하지 않는다.

## 준비·기록·제출

`RequestProgram`과 `PollPrograms`는 shader 준비 및 native PSO 요청을 관리한다. 현재 view의 material slot은 exact instance와 coverage 하나를 식별해야 한다. 모든 requested generation이 준비되어야 selection이 완성된다. pending이면 해당 프레임을 보류하고 실패면 오류를 반환한다. 임의의 구세대/native material을 섞어 렌더하지 않는다.

Prepare/Declare/record 성공은 GPU 제출 성공이 아니다. `PublishSubmittedCache`는 owning graph의 submission ticket/completion을 확인해 lookup 등을 게시한다. upload prefix의 접수만으로 전체 graph 성공을 선언할 수 없다. graph callback과 submitted frame은 exact program, geometry, instance pin, texture/resource owner를 완료까지 보유한다. 해제는 device idle 이후 `ShutdownAfterIdle`로 수행한다.

## live 품질과 참조 경로

live renderer는 `lookupRuntimeEvaluation=true`로 fragment split-sum을 선택한다. 이 경로는 프레임별 full-screen reference 적분 cache와 다르다. `SceneHostBudget::lookupApproximate`는 changed pixel의 최종 split-sum 근사 선택이며 시간이 지나면 reference로 정제되는 옵션이 아니다.

기본 host 예산은 4096×4096 pixel, draw 4096, lookup 2 GiB, SSS/refraction/volume 각 512 MiB다. 이는 승인된 realtime 성능 보장이 아니라 거부 가능한 용량 상한이다. 실제 사용할 기능과 viewport에 맞는 비용 검증이 필요하다.

## 지원 경계

제품은 graph-only material 소비를 기준으로 한다. CodeProgram 보관과 독립 helper 지원은 임의 Code draw의 Scene fallback을 허용하지 않는다. Blend 정렬은 object/draw 범위이며 교차 삼각형, 한 draw 내부 self-overlap, OIT는 별도 문제다. SSS는 screen-space diffusion, refraction은 화면 배경/환경 근사, Volume은 bounded homogeneous transport다. 각 효과의 수용 범위는 해당 설계 문서에서 확인한다.

## 구현 근거

- [`Engine/RenderEngine/MaterialGraphSceneHost.h`](../../Engine/RenderEngine/MaterialGraphSceneHost.h)
- [`Engine/RenderEngine/MaterialGraphSceneHost.cpp`](../../Engine/RenderEngine/MaterialGraphSceneHost.cpp)
- [`Engine/RenderEngine/Render/Scene/EnhancedSceneRenderer.cpp`](../../Engine/RenderEngine/Render/Scene/EnhancedSceneRenderer.cpp)
- [`Engine/RenderEngine/MaterialGraphSceneEffectStage.inl`](../../Engine/RenderEngine/MaterialGraphSceneEffectStage.inl)
- [`Engine/RenderEngine/MaterialGraphSceneAccess.h`](../../Engine/RenderEngine/MaterialGraphSceneAccess.h)
