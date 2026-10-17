# Material 제품 generation, 바인딩, 쿠킹 계약

> 기준: `493536e300e274cbf59989cdd62b20310c8ae64f`의 코드·호출 연결 정적 검토. 이 문서 갱신에서는 빌드·GPU 실행·성능 측정을 수행하지 않았다.

## 현재 제품 경계

Lattice는 저작 문서·typed IR·Slang 생성 계층이다. `material_graph`는 이를 엔진 shader, binding layout, 불변 generation으로 바꾸고 `LX::Runtime`은 Graph와 authored Code 입력의 공통 instance/pipeline 계약을 소유한다. 실제 Scene 소비자는 `SceneHost`의 METEOR stage들이다. 테스트용 `SceneMaterialSlot`을 제품 host로 취급하지 않는다.

흐름은 `.shadergraph → GenerateMaterialSlang → CompileSceneProduct → VerifiedProduct → CookedProgram/Generation → Instance → SceneViewInput → SceneHost`다. Code 입력은 별도 검증된 CodeProgram에서 공통 LX runtime으로 들어오며 Graph로 자동 변환되는 것이 아니다. 제품 Scene의 graph-only 제약과 Code 자산을 보관·복구할 수 있다는 사실을 구분한다.

## 자동 선택과 미지원 거부

`SelectRoute`는 feature/output 일관성과 비용을 검사하고 `Tier::{Standard,Layered,Special}`, `Route::{Deferred,Forward,Volume}`를 선택한다. nonlocal transmission/SSS/volume은 Special이며 surface가 있으면 Forward, volume만 있으면 Volume이다. 나머지는 host GBuffer feature 집합에 모두 들어갈 때 Deferred, 그렇지 않으면 Forward다. capability는 품질 옵션이 아니라 실제 transport 설치 여부다.

기본 Budget은 texture sample 32, texture/sampler 각각 64, uniform 65536 bytes, variant 1, compiled bytes 32 MiB다. Stage별 reflection 불일치, 잘못된 feature, 필요한 Forward/lookup/refraction/SSS/volume 부재, descriptor·bytecode 예산 초과는 오류다. 지원하지 않는 feature를 단순한 PBR로 조용히 낮추지 않는다.

`Tier`와 Opaque/Masked/Blended coverage는 다른 축이다. 투명도 queue는 [Scene coverage](MaterialGraphSceneInput.md) 계약과 고정 함수 상태를 함께 만족해야 한다.

## 바인딩과 색 공간

`BuildBoundSource`는 논리 resource를 제품 공간으로 투영한다. 현재 graph texture table은 `t16384`의 4096개, sampler table은 `s3`의 36개, parameter uniform은 `b2`, descriptor index block은 `b15`다. 이 수치는 authored Code helper의 `t16..t127` 규칙과 다르다.

`ResolveBindings`와 stage reflection 병합은 parameter별 ID/type/offset/bytes, texture/sampler slot, texture encoding uniform을 검증한다. texture encoding은 RGBA 또는 BC5 normal XY이며 편집 가능한 graph parameter가 아니다. `PrepareUniforms`는 reflected layout을 기준으로 값을 pack하고 `PrepareResourcesWithUniforms`는 이미 pack된 instance bytes를 다시 해석하지 않고 descriptor를 만든다.

이미지 sRGB는 보간 이전 decode를 보장해야 한다. `ResourcePacket`은 CPU Texture representation owner를 중복 제거해 유지한다. GPU residency 및 native retirement는 texture cache가 따로 소유한다. CPU owner 하나로 GPU entry 생존을 보장할 수 없다.

`RenderBindingCache::Prepare`는 recording ID와 descriptor version에 맞는 packet을 만든다. 다른 recording에 upload slice/table을 재사용하면 안 된다. `Bind`는 올바른 pipeline/layout 이후 draw/dispatch 직전에 호출하며 resource transition은 RenderGraph 선언의 책임이다.

## 후보 전체 교체와 수명

`Generation`은 asset ID, process-wide generation 번호, cooked product, content digest, surface blend mode, 선택적으로 AssetDepot artifact/default texture origin을 소유한다. `GenerationStore::BeginPreparation → Prepare → Publish`에서 loader/serialization/hash는 store mutex 밖에서 수행된다. 동일 cold ticket은 loader를 한 번만 실행한다.

reload는 accepted generation을 즉시 제거하지 않고 preparation ticket을 갱신한다. Remove/Clear/새 reload는 오래된 ticket 게시를 거부한다. 같은 digest의 성공 결과는 기존 owner를 유지할 수 있다. 격리 preview는 store 상태를 바꾸지 않으며 expected accepted owner를 비교하는 별도 Publish로만 적용한다.

retained cache의 기본 예산은 128 MiB다. eviction은 cache reference만 놓고, instance/worker/frame이 가진 exact generation을 파괴하지 않는다. generation 번호는 cache clear로 다시 시작하지 않는다. 실제 Scene은 pending generation일 때 프레임 선택을 보류하므로 이 CPU의 last-valid 보존을 화면의 자동 구세대 fallback으로 해석하면 안 된다.

## 쿠킹과 runtime 재개방

현재 `CookedProgramVersion`은 12, `SceneHostIdentity`는 `|lx-scene-host:34`다. 변경 시 이전 payload는 현재 host에 재바인딩하지 않고 recook해야 한다. `WriteCookedProgram`/`ReadCookedProgram`은 bounded versioned envelope, checksum, layout, targets, metadata/source contract를 검사한다.

`SceneCompileTargets`는 backend별 complete stage 집합을 정의한다. Editor는 실행 backend만 준비할 수 있고 AssetCooker는 DXIL/SPIR-V 모두를 준비한다. `LoadSceneShaders`는 host identity, backend 포함 여부, 정확한 stage/profile 집합과 nonempty bytecode를 검사하며 source·reflection·Slang compiler를 다시 열지 않는다. artifact dependency 목록 중 메모리 전용 필드는 cooked payload에 자동 포함된다고 가정하지 않는다.

## 점검 순서

1. 문서/IR 오류인지 compiler 진단인지 분리한다
2. feature/capability/coverage와 reflected layout을 확인한다
3. exact generation, backend, complete stages와 host version을 확인한다
4. recording·descriptor epoch, Texture residency, 제출 성공과 완료 수명을 확인한다
5. [Scene host](MaterialGraphSceneHost.md), [쿠킹](MaterialGraphSceneCook.md), [자산 저작](MaterialAssetAuthoring.md)의 호출 경계를 따른다

기존 날짜별 PASS/성능 숫자는 해당 시점의 회귀 기록이며 현재 commit의 실행 결과로 승격하지 않는다.

## 구현 근거

- [`Engine/RenderEngine/MaterialGraphProduct.h`](../../Engine/RenderEngine/MaterialGraphProduct.h)
- [`Engine/RenderEngine/MaterialGraphProduct.cpp`](../../Engine/RenderEngine/MaterialGraphProduct.cpp)
- [`Engine/RenderEngine/MaterialGraphRuntime.h`](../../Engine/RenderEngine/MaterialGraphRuntime.h)
- [`Engine/RenderEngine/MaterialGraphRuntime.cpp`](../../Engine/RenderEngine/MaterialGraphRuntime.cpp)
- [`Engine/RenderEngine/MaterialGraphRenderBindings.h`](../../Engine/RenderEngine/MaterialGraphRenderBindings.h)
- [`Engine/RenderEngine/MaterialGraphSceneCompiler.cpp`](../../Engine/RenderEngine/MaterialGraphSceneCompiler.cpp)
- [`Engine/RenderEngine/LXMaterialRuntime.h`](../../Engine/RenderEngine/LXMaterialRuntime.h)
