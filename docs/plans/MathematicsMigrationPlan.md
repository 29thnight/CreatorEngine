# Mathematics 이주 계획

> 2026-10-20 정리 · master `27de3f5a02aadde7456aab680db3184c36ee735e` · 잔여 구현·수용 계획

판정 기준은 위 master의 커밋된 소스다. 작업 중인 로컬 코드 변경은 포함하지 않는다. 완료된 구현 단계·날짜별 실행 일지는 제거했다. 대시보드는 SRP 저작 전환 취소에 해당하는 항목만 제거하고, 나머지 상태·공수·링크를 보존한다.

[전체 문서 대조·충돌 판정](MasterPlanAudit20261020.html) · [정리 전 원문](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/docs/plans/MathematicsMigrationPlan.md)

## 남은 작업과 완료 판정

- 이미 이관된 math::* 저장 타입·연산과 DirectX 수학 별칭 제거는 활성 계획에서 제외한다. 잔여는 동일 camera/projection/bounds 결과의 제품 픽셀과 실제 Physics simulation 수용이다.
- ABI·YAML·GPU·PhysX 경계의 필드 변환/레이아웃과 D/R 제품 회귀를 확인한다. 과거 빌드 통과를 현재 master의 실행 결과로 재사용하지 않는다.

## 적용할 구조와 경계

`math value types → Transform/Camera snapshots → render and physics boundary conversion`

위 경로는 소스에서 확인한 구현 기반이다. 아래 요구의 수용 결과를 뜻하지 않는다. 구현을 다시 만드는 항목과 제품 검증을 혼합하지 않는다.

## master 소스 근거

모든 행의 파일·행 번호·Git blob은 고정 master에서 대조했다. 소스 존재·배선 확인과 실행 성공을 구분한다.

- [Engine/SceneRuntime/Transform.h:90](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/SceneRuntime/Transform.h#L90) — `math::vector3 GetPosition() const;`
- [Engine/RenderEngine/Camera.h:25](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RenderEngine/Camera.h#L25) — `math::matrix4x4 CalculateProjection() const;`
- [Engine/Utility_Framework/Mathematics.Intersect.h:18](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/Utility_Framework/Mathematics.Intersect.h#L18) — `#include <mathematics/intersect.hpp>`

## 검증 경계

이번 정리는 master 소스·문서 전체의 소유권/충돌/링크/HTML 검증이다. 엔진 빌드·GPU 픽셀·SDK/장치·성능·실제 GUI 수용을 새로 실행한 결과가 아니다. 과거 통과는 해당 소스/바이너리 신원에만 유효하다. 남은 제품 수용은 위 요구를 실제 산출물로 닫아야 한다.

<details><summary>이전 절 링크 호환</summary>

이전 절의 완료 이력·구안은 제거했다. 이 링크로 들어온 경우 위의 남은 작업과 단일 소유 계획을 따른다.

<span id="-기본값-규약이-둘-달랐다--조용히-넘어가면-안-되는-종류다"></span>
<span id="0-결론"></span>
<span id="01-남은-작업-목록--2026-08-27-재작성"></span>
<span id="1-현재-배선"></span>
<span id="11-진입점"></span>
<span id="12-2026-08-27-현재-수치"></span>
<span id="13-값이-흐르는-주요-경계"></span>
<span id="2-mathematics-적합성"></span>
<span id="21-그대로-맞는-규약"></span>
<span id="22-직접-매핑"></span>
<span id="23-단순-치환이-안-되는-지점"></span>
<span id="3-의존성-배선"></span>
<span id="31-권장-방식-고정-벤더링"></span>
<span id="32-컴파일-설정-계약"></span>
<span id="33-전환용-interop-규칙"></span>
<span id="4-구현-슬라이스"></span>
<span id="5-s0-구현-패치의-실제-범위"></span>
<span id="6-판정표"></span>
<span id="s0-dependency--contract-probe-소비자-미배선--완료-2026-08-25"></span>
<span id="s1-coremathfh-해체--구현빌드-게이트-완료-2026-08-25"></span>
<span id="s2-camera---framecamerasnapshot---render-첫-수직-슬라이스"></span>
<span id="s2-구현-결과-2026-08-25"></span>
<span id="s3-transform--transformstore--yamlreflection--구현-완료-physics-runtime-gate-보류-2026-08-25"></span>
<span id="s4-a-proxycommand-world-transform-payload-2026-08-25"></span>
<span id="s4-b-renderproxy-world-transform-storage-2026-08-25"></span>
<span id="s4-c-renderproxy-world-aabb-storage-2026-08-25"></span>
<span id="s4-d-mesh-assetcomponent-bounds--cache-rebuild-2026-08-25"></span>
<span id="s4-e-animator-bone-palette-storage--gpu-staging-2026-08-25"></span>
<span id="s4-experiment-구간-구현-결과-2026-08-25"></span>
<span id="s4-f-enhanceddrawitem-world-matrix--instance-staging-2026-08-25"></span>
<span id="s4-g-decalsprite-world-payload--billboard-assembly-2026-08-25"></span>
<span id="s4-h-foliage-derived-world-matrix--asset-schema-gate-2026-08-25"></span>
<span id="s4-i-decalsprite-frame-constant-matrices-2026-08-26"></span>
<span id="s4-j-gbuffer-frame-matrix--skinning-fallback-2026-08-26"></span>
<span id="s4-k1-camera-storageapi--editorruntime-consumers-2026-08-26"></span>
<span id="s4-k2-render-lightshadowfoggizmo-values--dead-scene-light-dto-removal-2026-08-26"></span>
<span id="s4-l1-animationskeletonsocket-storage--evaluation-2026-08-26"></span>
<span id="s4-l2a-material-typed-value-boundary--flow-dto-2026-08-26"></span>
<span id="s4-l2b1-meshimport-vertex--model-node-abi-2026-08-26"></span>
<span id="s4-l2b2-navigationspritesheetterrain-dto--dead-bridge-cleanup-2026-08-26"></span>
<span id="s4-render-payload--meshimportanimation"></span>
<span id="s5-a-physics-query-dto--physx-adapter-2026-08-26"></span>
<span id="s5-b-physics-actorcollider--mesh-cooking-경계-2026-08-26"></span>
<span id="s5-c-character-controller--movementforced-move-경계-2026-08-26"></span>
<span id="s5-d-ragdollarticulation--physics-root-cleanup-2026-08-26"></span>
<span id="s5-physics-독립-섬--구조-전환-완료-live-simulation-gate-없음"></span>
<span id="s6-a-color-저장직렬화gpu-전달-2026-08-26"></span>
<span id="s6-b-rect-저장ui-hitbox-2026-08-26"></span>
<span id="s6-c-native-vector234와-helper-teardown-2026-08-26"></span>
<span id="s6-d-easingtween-upstream-계약-및-legacy-teardown-2026-08-26"></span>
<span id="s6-e-consumer-owned-tweenmanager-기반-2026-08-26"></span>
<span id="s6-uieditor나머지-값-타입"></span>
<span id="s7-a1-product-frustum-chain-2026-08-27"></span>
<span id="s7-a2-product-bounds--editor-ray-picking-2026-08-27"></span>
<span id="s7-b1-simplemath--mathematicsinterop-root-teardown-2026-08-27"></span>
<span id="s7-b2-product-raw-directxmath-teardown-2026-08-27"></span>
<span id="s7-b3-contract-oracle--direct-dependency-root-teardown-2026-08-27"></span>
<span id="s7-boundsfrustum--directx-수학-의존-제거"></span>
<span id="sec-0-결론"></span>
<span id="sec-1-현재-배선"></span>
<span id="sec-2-mathematics-적합성"></span>
<span id="sec-3-의존성-배선"></span>
<span id="sec-4-구현-슬라이스"></span>
<span id="sec-5-s0-구현-패치의-실제-범위"></span>
<span id="sec-6-판정표"></span>
<span id="검증-완료-2026-08-25"></span>
<span id="배선"></span>
<span id="별칭을-두지-않았다"></span>
<span id="회전은-quaternion-으로-갈라-넣었다"></span>

</details>
