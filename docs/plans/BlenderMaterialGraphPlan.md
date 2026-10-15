# Blender형 Material Graph: 구현 기반과 남은 제품 수용 계획

> 기준: `493536e300e274cbf59989cdd62b20310c8ae64f`의 코드·호출 연결 정적 검토. 이 문서 갱신에서는 빌드·GPU 실행·성능 측정을 수행하지 않았다.

## 0. 현재 판정과 실행 범위

목표는 아티스트가 Blender형 node/socket/default/group 조작으로 재질을 저작하고, 명시된 실시간 비용 안에서 예측 가능한 pre-tone linear HDR 응답을 얻는 것이다. 모든 Blender 노드·Cycles 효과·최종 screenshot의 동일성을 약속하지 않는다. 지원 범위는 [대응표](../design/LatticeMaterialBlenderParity.md), 현재 제품 흐름은 [통합 계약](../design/MaterialGraphProductIntegration.md)을 따른다.

schema/typed IR/Slang 생성, 공통 ShaderGeneration/Instance와 PSO owner, cooked 복구, 모델 material graph 저작, Editor/Inspector/HTTP 연결은 구현되어 있다. 현재 live Scene은 graph-only GPU-driven host이고 IBL은 최종 split-sum runtime evaluation이다. 이를 새로 만들 작업으로 중복 산정하지 않는다.

다만 코드 존재와 독립 probe, 역사적 native fixture, 현재 제품 수용은 다른 증거다. MAT-9의 전체 품질·실제 모델 성능과 LX 제품 편집 마감은 열려 있다. 옛 32/34일·40일+미산정·잔여8일은 당시 산정이며 현재 일정/완료율로 재사용하지 않는다. 이 문서 작업에서 새 공수나 실행 통과를 만들지 않았다.

## 1. 고정한 비교 범위

Blender 5.1.1의 고정 입력·socket 의미·reference fixture를 출발점으로 한다. Core의 base/MR/IOR/specular/normal/alpha/emission, Layered의 coat/sheen/aniso/film, Special의 transmission/SSS/Volume을 각각 판정한다. reference renderer가 평가하지 않는 feature를 다른 renderer의 같은 이름만 보고 동일하다고 가정하지 않는다.

같은 mesh/UV/tangent/texture/HDRI/light/camera의 tone mapping 전 linear HDR을 비교한다. AgX·노출·bloom·AA·UI·compositor, 일반 shadow/AO/probe 품질, 좌표계 전환은 이 material 오차를 덮는 보정값으로 쓰지 않는다. 임의 topology를 매 runtime frame 편집하는 기능도 범위 밖이다.

현재 활성 graph domain은 material이다. pass/fullscreen/compute 저작 domain은 향후 별도 기능이며 material Output/closure 계약에 섞지 않는다.

## 2. 현재 구조와 소유권

`.shadergraph` → typed IR/feature extraction → generated Slang·ShaderMeta → verified product → immutable generation/instance → Scene input seal → ready program 선택 → render graph 기록·제출 순서다. Graph 모드에서는 생성 Meta/Slang을 별도 저작 원본으로 편집하지 않는다.

logical `MaterialInputs`/`PrincipledSurface`는 cbuffer/GBuffer/native struct layout이 아니다. Volume은 별도 closure다. source ID·색 공간 의도는 physical register와 분리되고 reflection은 실제 binding을 검증한다. runtime 값/texture override와 cook-time topology/feature permutation도 분리한다.

공통 runtime에 CodeProgram 호환·직렬화·명시적 helper가 있어도 live Scene이 arbitrary Code material을 수용하는 것은 아니다. 옛 mixed Code/Graph 목표는 현재 graph-only admission에 맞추어 읽는다. [Code AssetSet](../design/CodeMaterialAssetSet.md)의 exact variant 복구는 독립 계약이다.

오류는 지원하지 않는 feature/enum·budget·stale input·missing resource를 지목하고 후보를 거부한다. accepted asset/generation은 유지하되 이전 재질을 새 프레임의 fallback으로 섞지 않는다. package에는 현재 host34/cooked12 계약과 양 backend target이 필요하다.

## 3. 아트 팀용 비용 계약

Standard/Layered/Special tier는 feature 집합에서 선택하고 route는 backend/host capability와 GBuffer 표현 가능성까지 검사한다. coverage Opaque/Mask/Blend와 물리 transmission은 독립이다. Special surface는 Forward transport, volume-only는 Volume 경로가 필요하다.

Inspector는 route/feature·texture sample/variant 비용·지원 거부 이유·투명 overlap 위험을 이해 가능한 상태로 보여야 한다. artist가 register/descriptor/PSO/RenderGraph pass를 직접 선택하게 하지 않는다. budget 초과를 조용히 기능 제거로 처리하지 않는다.

live IBL의 split-sum은 최종 근사다. idle refine을 약속하지 않으며 reference 적분의 오차 목표를 live 통과 판정으로 이전하지 않는다. 같은 고정 grid의 재질/조명별 차이를 %로 기록한다. [차이 보고](../analysis/MAT9SplitSumLiveLookupDifference.md)가 역사적 baseline이다.

## 4. 단계별 현재 대응

| ID | 현재 재사용할 기반 | 남는 수용/확장 |
|---|---|---|
| MAT-0 | 고정 Blender input/reference와 비교 도구 | 입력·seed·source/binary identity를 유지한 새 비교 |
| MAT-1 | 논리 surface/volume·feature ABI | 새 feature의 semantics/layout 구분과 compatibility |
| MAT-2 | schema1·typed IR·전체 문서 transaction | annotation 등 미구현 schema 확장과 migration gate |
| MAT-3 | Core Principled 평가·공통 GGX | texture/route/rendered 교차 수용 |
| MAT-4 | Layered lobe·film/energy 데이터 | anisotropy/grazing·live 근사 차이와 비용 |
| MAT-5 | bounded SSS/refraction/Volume | closed-interface·spatial SSS·복잡한 합성 품질 |
| MAT-6 | deterministic codegen·source diagnostics | 새 node/enum을 registry부터 cook까지 일관 확장 |
| MAT-7 | generation/binding/PSO/cook·graph-only Scene | 현재 제품 실패 복구·package/backend 전체 재검증 |
| MAT-8 | model graph SoT·Inspector·Editor/preview | LX 전용 panel·전체 gesture/수명 마감 |
| MAT-9 | 고정 image/성능 도구·역사적 baseline | 아래 품질/성능 gate |

이 표는 실행 통과를 새로 선언하는 일정표가 아니다. LX-3/3H와 MAT-2/6/8의 같은 산출물을 두 번 완료 처리하지 않는다.

## 5. 남은 품질·성능 작업

### reference 품질

- RMS≤1%, normalized p95≤1%, max≤5%, 독립 seed RMS≤0.25%의 기존 공학 목표를 reference에 유지한다. noise·mask·input 변경으로 오차를 숨기지 않는다
- fixed film on/off와 두 HDRI constant suite의 역사적 수용을 유지하되 texture-only/factor×texture/normal-map·같은 feature의 Deferred/Forward를 새로 비교한다
- Special Surface의 7/18 미수용을 닫기 위해 closed-surface exit refraction·내부 재방문, SSS 공간 응답과 grazing을 분리한다. 임의 tint 중복으로 오차를 맞추지 않는다
- 실제 alpha+transmission, interpenetration/self-overlap, 강한 Volume의 T≥1e-4 역보정 오차를 검사한다. ordered draw를 OIT/triangle sorting으로 설명하지 않는다
- 원래 area-light grid의 light ABI와 실제 consumer를 연결한 뒤 비교한다. unsupported lighting을 matched reference처럼 사용하지 않는다

### 실시간 성능

Standard가 기존 Standard PBR의 GPU 시간/GBuffer 대역폭 회귀 상한을 넘지 않는지 실제 입력으로 확인한다. Layered/Special은 추가 lobe·화면 점유·overlap·메모리 비용을 함께 보고한다. 사용하지 않는 lobe/sample/permutation 제거와 compile/cache/variant 상한도 확인한다.

실제 모델의 near/fullscreen·moving/stopped/cut/disocclusion·animation·material/HDRI 변경을 Debug/Release, cold/warm PSO로 측정한다. CPU/GPU median/p95/max, 완료 native render FPS, input lag, VRAM/upload/register 비용을 기록한다. GBV 정확성 실행을 timing 실행으로 쓰지 않으며 생성 frame 출력률을 native 처리량으로 세지 않는다. [성능 runner](../analysis/MAT9MaterialScenePerformance.md)의 per-frame provenance를 따른다.

GPU-driven/RenderGraph 개선이 이 비용을 자동 해결한 것으로 간주하지 않는다. 검증된 material 기반은 후속 구조 작업이 소비할 수 있고 MAT-9 전체 완료를 그 착수의 선행으로 묶어 순환 의존을 만들지 않는다.

## 6. 완료 기준

저장→닫기→재개방에서 node/pin/link/layout/Blackboard/default/color-space/group이 보존되고, unknown/migration/compile 오류에서 원본과 accepted generation을 잃지 않아야 한다. Apply/Save/Assign·자동 저장·strict reload·stale async/HTTP 충돌을 실제 UI에서 확인한다.

현재 package의 source-free Player 복구, ready generation 선택, in-flight owner와 failure preservation, backend별 binding/PSO·실제 frame을 확인한다. preview가 지원하는 범위에서 동일 generated 의미를 소비해야 하며 Blended preview 제한을 숨기지 않는다. 독립 fixture 성공만으로 Preview/Scene/Game/Player 전체 성공을 추정하지 않는다.

reference gate와 live 차이 보고, route 비교, 실제 모델 성능이 각각 자기 증거를 갖고 남은 한계를 명시해야 한다. 미래 기능은 compiler에 없는 상태에서 완료로 표기하지 않는다.

## 7. 후속 pipeline 인계

후속 Pipeline Asset/Custom Pass는 compiled generation, logical ABI, 선택 route, resource/cost metadata를 읽기 전용 계약으로 소비한다. Principled 의미·schema·artist tier를 다시 정의하지 않는다. post/shadow/probe를 끈 독립 material golden도 재현 가능해야 한다.

UI 후속은 [Lattice 도입](LatticeAdoptionPlan.md), renderer 배선은 [PBR 안정화](PBRWiringStabilizationPlan.md), backend 전체 수용은 [Backend parity](BackendParityPlan.md), 일반 pipeline 저작은 [Scriptable Render Pipeline](ScriptableRenderPipelinePlan.md)이 소유한다.

<a id="implementation-history"></a>
## 구현 이력의 보존 위치

날짜별 build/check 숫자를 이 계획에 누적하지 않는다. [MAT-7 어댑터](../analysis/MAT7ShaderMetaAdapter.md)부터 [제품 복구](../analysis/MAT7ProductEditingRecovery.md), [MAT-9 체크포인트](../analysis/MAT9IntegrationCheckpoint20261001.md)와 연결된 원본 JSON/이미지가 각 단계의 범위·한계를 보존한다. 현재 구현의 정본은 design 문서다.

## 구현 근거

- [`Lattice/Material/LXMaterialGraph.cpp`](../../Lattice/Material/LXMaterialGraph.cpp)
- [`Lattice/Material/LXMaterialCompiler.cpp`](../../Lattice/Material/LXMaterialCompiler.cpp)
- [`Engine/RenderEngine/MaterialGraphProduct.h`](../../Engine/RenderEngine/MaterialGraphProduct.h)
- [`Engine/RenderEngine/MaterialGraphProduct.cpp`](../../Engine/RenderEngine/MaterialGraphProduct.cpp)
- [`Engine/RenderEngine/MaterialGraphSceneHost.cpp`](../../Engine/RenderEngine/MaterialGraphSceneHost.cpp)
- [`Engine/RenderEngine/MaterialGraphSceneLookup.cpp`](../../Engine/RenderEngine/MaterialGraphSceneLookup.cpp)
- [`Editor/EngineGUIWindow/MaterialGraphWindow.cpp`](../../Editor/EngineGUIWindow/MaterialGraphWindow.cpp)
- [`Tools/regression/assess-hdri-material-images.py`](../../Tools/regression/assess-hdri-material-images.py)
- [`Tools/regression/assess-special-material-images.py`](../../Tools/regression/assess-special-material-images.py)
- [`Tools/regression/measure-material-scene-performance.ps1`](../../Tools/regression/measure-material-scene-performance.ps1)
