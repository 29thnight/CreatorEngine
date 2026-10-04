# Blender형 PBR·Material Graph 계획 (PHASE 4.25)

**2026-10-02 목표 재평가·통합 완료:** MAT-0~6·MAT-8 및 기존 MAT-7 기반의 32인일은 구현 이력으로 보존한다. 재개방한 공통 재질 소비 MAT-7은 혼합 transport·제품 회귀까지 완료했고, 미산정 작업 시간을 역산하여 기성에 더하지 않는다. MAT-9 잔여 8인일과 LX 제품 편집 마감은 별도다. 산정 범위는 **40인일 + 미산정**이며 전체 완료율/납기를 뜻하지 않는다. Vulkan 비교는 PHASE 4.9 소유다. [공수 근거](RenderPhaseEffortEstimate.md).


**신설 2026-09-03 · 구현 기반 32인일 · MAT-7 공통 통합 완료 · LX 마감 미산정 · MAT-9 잔여 8인일 · PHASE 4.25 진행 중**

공통 노드 저작 계층·UI의 설계 초안은
[`LatticeNodeSystem.md`](../design/LatticeNodeSystem.md), Material 우선 순서와
이후 창의 재작성 시점은 [`LatticeAdoptionPlan.md`](LatticeAdoptionPlan.md)가
소유한다. LX 캔버스의 독립 ImGui 예제 빌드·조작 게이트 `LX-2`는 통과했고,
Editor 기본 연결은 구현됐고 `LX-3`의 그룹/Blackboard·전체 조작 마감은 남는다. 산정된 40인일은 Material 의미와
`MAT-2/MAT-6`의 기본 graph 산출물 산정이다. Blender Shader Editor에 가까운
노드·소켓·그룹·편집 동작의 전 범위와 노드별 지원 확대는 `LX-0` 대응표를 만든 뒤 별도 산정한다.
기존 BT·Animator 자산 변환 비용은 계획하지 않는다.

아트 팀이 Blender에서 만든 재질 의도를 CreatorEngine에서 같은 방식으로 이해하고 예측할 수
있게 만드는 계획이다. **노드 저작 구조와 조작은 Blender Shader Editor에 거의 1:1로 대응**하도록
LX를 설계한다. 렌더 결과는 **Blender 5.1.1 Principled BSDF/OpenPBR 계열의 재질 구성과
pre-tone linear HDR 결과**를 게임 엔진 비용 모델 안에서 검증한다.

PHASE 4가 현재 glTF PBR 배선을 안정화한 뒤 시작한다. RenderGraph·그림자·reflection probe·
후처리와 generic Custom Pass authoring은 PHASE 4.75가 소유한다.

---

## 0. 현재 판정과 실행 범위 (2026-10-02)

목표 재평가는 현행 소스·기존 실행 증거와 사용자 목표를 대조한 계획 정리다. 이후 생성 어댑터의 새 빌드·검증 결과는 아래 첫 구현 단계와 별도 검증 기록에서 구분한다.

| 구분 | 현재 판정 | 소유 |
|---|---|---|
| Principled Core/Layered/Special 의미·ABI, typed Graph IR·왕복, Slang 생성/진단 | 구현 기반 완료. 기존 지원 범위를 재작성하지 않음 | MAT-0~6 |
| reflection binding·generation/instance·PSO 수명·기존 쿠킹/캐시·특수 transport | 기존 LX 경로에서 검증된 기반. 기존 3인일만 보존 | MAT-7-BASE |
| 모델 기본 graph·Inspector override·preview·비용/실패 안내 | 구현 기반 완료. 공통 소비에 재연결할 대상 | MAT-8 |
| Graph→`.shadermeta`+`.slang`→공통 재질 소비 | **MAT-7 완료. 생성·공통 값/바인딩·LXMC v4·graphics/compute owner·Code/Graph alpha/SSS/transmission/Volume 공통 Forward+ 정렬·합성 및 DX12 Debug/Release 제품 편집/복구·cooked Player 회귀 완료. 지원 한계와 품질/비용 수용은 별도** | MAT-7 |
| annotation/Reroute 삽입 문서 계약·그룹/Blackboard 제품 패널·전체 조작·HTTP host 수명 | 이미 구현된 Core·Editor/HTTP 연결과 분리해 잔여만 마감 | LX-1/3/3H |
| 전체 Blender 품질·route parity·실제 모델 성능 | 진행 중. 2026-10-04부터 살아 있는 화면은 split-sum 근사이며, Blender 대비 차이를 %로 표기하고 넘어간다(아래 10-04 결정) | MAT-9 |

### MAT-7 통합 순서와 완료 게이트

1. **재질 원본 단일화:** Blender형 재질 하나의 node tree/Blackboard/Material Output이 저작 원본이다. Graph 모드의 ShaderMeta·Slang은 읽기 전용 생성 산출물이고 인스턴스는 노출 값·텍스처만 override한다.
2. **공통 산출물 생성:** 지원 typed property·기본값·자원·feature/coverage·entry 정보를 LX 실행 계약으로 내린다. ShaderMeta는 생성 계약의 표현으로 재사용하며, 물리 binding/offset은 Slang reflection으로 검증한다. 재질의 feature/coverage와 렌더러의 패스 실행 정책을 구분한다. Graph/compiler/include identity가 같은 계약·소스·bytecode를 함께 쿠킹·게시하고 실패 시 마지막 정상 재질을 유지한다.
3. **LX 공통 재질 소비:** LX generation/instance·자원 owner·바인딩·PSO 교체가 공통 실행 환경을 소유한다. 기존 ShaderMeta 로더/packer/쿠킹 구현은 이 환경에서 재사용하며, 코드로 작성한 ShaderMeta/Slang 입력도 LX 실행 계약으로 변환한다. Graph를 기존 ShaderMeta runtime/cache에 삽입하는 구조로 고정하지 않는다. Preview·Scene·Game·cooked Player가 같은 계약을 소비하며 렌더 경로는 재질 특성으로 결정한다.
4. **공통 Forward+:** light list·shadow·불투명 depth/HDR·정렬 계약을 공유한다. Code/Core/Layered/SSS의 일반 alpha와 physical transmission을 같은 순서에서 검사한다. 굴절은 현재까지 합성한 뒤쪽 alpha를 배경에 포함하고 불투명 depth/owner를 보존한다. Volume은 배경과 각 표면 깊이의 카메라 transport를 구별한다. SSS·굴절·Volume 보조 처리는 그 순서 안에서 실행한다.
5. **제품 회귀:** 같은 그래프의 생성 결정성·Meta/reflection 일치·override·저장/재개방·실패 복구·재import·쿠킹/재실행을 DX12 Debug/Release에서 검사한다. 그래프 UI/위젯 마감은 LX, 색/route/성능 수용은 MAT-9가 소유한다.

MAT-7-BASE와 MAT-6의 구현을 다시 만드는 항목은 활성 작업에서 제외한다. `.materialprogram.json`·기존 LX generation은 재사용할 구현 증거이며 공통 ShaderMeta 소비 완료의 증거가 아니다. BT/Animator 창 재작성과 imgui-node-editor 제거는 LX 횡단 후속으로 관리하고 4.25 종료 조건에서 제외한다.

### MAT-7 첫 구현 단계 — 생성 어댑터 (2026-10-02)

- `MaterialGraphShaderMeta`가 기존 generated Slang을 실제 ShaderMeta schema로 내린다. 숫자/Color/Normal/Texture의 기본값·stable parameter ID·노출·색공간과 독립 Sampler를 보존한다. 물리 이름/offset/register는 실제 DXIL/SPIR-V reflection으로 확인하고 기존 공통 property packer를 사용한다.
- Editor authoring 준비와 AssetCooker의 자동 Scene compiler가 같은 어댑터를 호출한다. `.generated/<generation>/material.shadermeta`와 `material.slang`은 후보 폴더에서 함께 검증한 뒤 디렉터리 rename으로 게시한다. 공통 로더는 source SHA-256 불일치를 거부하며 생성 실패는 정상 쌍을 덮어쓰지 않는다.
- 이 단계의 pass entry는 **현재 Scene host의 실제 entry**다. 공통 Forward+ 설치나 일반 alpha Blend 합성 완료를 뜻하지 않는다. 저작 원본은 `.shadergraph`이며 생성 쌍은 캐시 산출물이다.
- 당시 후속은 common Material/override·binding·쿠킹 복구였다. 아래 두 번째 단계에서 값/바인딩·LXMC v3 복구를 연결했다. coverage·host compile 옵션/의존 계약과 공통 PSO/Forward+는 계속 남는다.
- 게이트와 증거는 [생성 어댑터 검증 기록](../analysis/MAT7ShaderMetaAdapter.md)을 따른다. MAT-7은 `progress`, 잔여 공수는 미산정을 유지한다.

### MAT-7 두 번째 구현 단계 — LX 소유의 공통 값/바인딩·쿠킹 복구 (2026-10-02)

- 생성 메타·전체 host Slang·공통 reflection layout을 immutable `VerifiedProduct`/generation에 보존한다. 제품 Graph Material은 생성 계약 없는 세대를 거부한다.
- `Material`의 typed 조회/수정, property block, texture owner 조회가 그래프의 공통 schema/값을 소비한다. stable parameter ID의 숫자/텍스처 override는 기존 공통 `MaterialPropertyPacker`로 패킹한다. 렌더 바인딩은 accepted uniform을 재사용하며 다시 LX 숫자 패킹을 하지 않는다.
- LXMC/artifact v3에 실제 generated metadata/source와 검증된 backend bytecode를 함께 담는다. authoring warm cache와 cooked reader가 같은 세대를 복구한다. 기존 v2는 재쿠킹 대상으로 처리한다. 기존 CEMF schema는 유지한다.
- 실제 DXIL/SPIR-V·실패 보존·복구 68항목, Debug/Release의 실제 DataSystem/Material·source-free package·warm cache 각각 25항목 및 반복 cook을 통과했다. 전체 Debug 에디터와 두 구성의 엔진/도구 빌드도 통과했다. GPU 렌더 이미지·PSO 통합·전체 모델 성능 수용으로 확대하지 않는다.
- 증거와 실행 게이트는 [공통 Material 소비 검증 기록](../analysis/MAT7CommonMaterialConsumption.md)이 소유한다. 여기서 공통 PSO/renderer 전환이나 일반 alpha Blend 완료를 선언하지 않는다.
- 당시 후속은 코드 입력의 LX 적응과 공통 PSO였다. 아래 세 번째 단계에서 코드 값/자원·frame snapshot을 연결했다. coverage·host compile 옵션/의존과 LX shader/PSO 소비, 공통 Forward+·일반 alpha Blend·제품 회귀는 계속 남는다. MAT-7 `progress`/미산정을 유지한다.

### MAT-7 세 번째 구현 단계 — 코드 입력의 LX instance·frame snapshot 연결 (2026-10-02)

- `LX::Runtime::ShaderGeneration/Instance`가 공통 계약·값·uniform·texture owner·keyword selection을 소유한다. generated Graph와 기존 코드 Material이 이 타입을 사용한다. Float2/Float4x4 등 generic property를 유지하며 Graph 전용 Principled/BSDF ABI를 강요하지 않는다.
- 코드 Material의 typed 편집/조회와 실제 GBuffer/Forward `SealCore`를 연결한다. draw snapshot이 LX owner를 보존하며 값/자원 검증 실패는 accepted owner와 caller output을 유지한다. 같은 handle의 동일 계약/layout은 weak cache로 재사용한다.
- 검증 범위와 실행 증거는 [코드 입력의 LX 연결 검증 기록](../analysis/MAT7CodeToLXRuntime.md)이 소유한다. generic shader compile/reflection·CPU frame sealing과 GPU 이미지/전체 모델 성능을 구분한다.
- Debug/Release 코드 재질 각각 35항목·그래프 소비 25항목·texture DataSystem 31항목 및 반복 cook을 통과했다. 기존 adapter 68항목과 product/runtime 회귀, 전체 Debug 에디터 빌드도 통과했다.
- 당시 코드 compatibility handle/native pass cache와 graph Scene host PSO는 유지됐다. 아래 네 번째 단계에서 graphics compile/PSO owner를 LX로 연결한다. 공통 Forward+·일반 alpha Blend·제품 회귀는 계속 남는다.

### MAT-7 공통 graphics shader·PSO 소유권 연결 (네 번째 구현 단계 · 2026-10-02)

- Code GBuffer/Forward와 Graph Scene host·packet·product slot이 `LX::Runtime::GraphicsGeneration`을 사용한다. 계약 owner·실제 effective compile/permutation/dependency/backend identity와 소유 바이트코드·고정 상태·PSO 요청을 함께 유지한다.
- Code의 draw snapshot이 모델 mask·Forward Reference 변형까지 accepted owner를 보관한다. Graph cooked target/bytecode는 sealed identity로 복구한다. CPU 소유권과 native RHI 객체 수명/캐시 역할을 구분한다.
- 새 후보가 성공해도 이전 프레임의 세대는 전체 sealing 성공 뒤의 commit까지 남긴다. 비동기 Pending 요청의 저장소 덮어쓰기를 거부하며, 공유 PSO는 마지막 cache holder가 없어질 때 fence 기준으로 retire한다.
- 정확한 빌드·실행 범위는 [공통 graphics 소유권 검증 기록](../analysis/MAT7GraphicsPipelineOwnership.md)이 소유한다. API 실패 주입을 전체 제품 hot reload·GPU 이미지/성능 수용으로 확대하지 않는다.
- Debug/Release 각각 소유권·실패 검사 186항목과 source-free identity 4항목, 실제 DX12 packet 169항목·GPU 16성분 및 Vulkan Scene host 768픽셀·Ready 요청 18개/validation 0을 통과했다. 기존 product/runtime 회귀와 전체 Debug 에디터 빌드도 통과했다.
- 당시 후속인 공통 Forward+ light list·일반 Blend 혼합 정렬·depth/HDR 합성은 아래 다섯 번째 단계에서 연결했다. 네 번째 단계의 Ready 요청 18개는 당시 opaque PSO 집합의 실행 이력이며 새 alpha 집합의 결과와 구분한다.

### MAT-7 공통 Forward+·일반 alpha Blend (다섯 번째 구현 단계 · 2026-10-02)

- Code와 Core/Layered Graph가 한 번 업로드한 light buffer와 같은 Forward+ tile count/index를 소비한다. 타일당 32개 초과 시 전체 light list로 평가하여 광원을 누락하지 않는다. 같은 cascade shadow·읽기 전용 opaque depth·HDR target을 공유한다.
- 카메라 방향 기준의 공통 back-to-front 순서에 Code 배치 구간과 Graph draw를 배치한다. Graph의 lookup capture/bake는 이 순서 안의 재질 준비이며 별도 transparency 합성 계층이 아니다. 알파는 SRC_ALPHA/INV_SRC_ALPHA, depth/opaque owner는 보존한다.
- Graph의 생성 ShaderMeta에 실제 Forward entry/state를 추가했다. Core/Layered는 opaque·shadow와 함께 alpha color/lookup까지 15개 PSO 요청이 모두 Ready여야 선택한다. 기존 SSS/refraction·Volume 처리는 유지한다. 이 기능들과 일반 alpha의 동시 혼합·굴절 배경 순서는 제품 잔여 게이트다.
- shader host ABI와 파생물이 변경되어 **SceneHost identity 11**로 올린다. 기존 identity 10 cook은 재생성한다. BRDF 1024/environment 4096과 이미지 품질 상한은 유지하며, 10을 사용한 MAT-9 과거 측정은 새 ABI의 재검증 결과로 세지 않는다.
- 실제 native GPU 검증과 잔여 범위는 [공통 Forward Blend 검증 기록](../analysis/MAT7CommonForwardBlend.md)에 기록한다. 작가 UI·Preview/Scene/Game·encrypted cooked Player의 전체 통합이나 모델 이동 성능 완료로 확대하지 않는다.
- DX12 Debug/Release 및 Vulkan Debug의 혼합 GPU 12조건·4,608 RGB 성분, Vulkan Debug/Release의 generation별 Ready 요청 30개·768픽셀을 validation 0으로 통과했다. 기존 Debug SSS/refraction/Volume·shadow/Decal, Scene/lookup/교체 55프레임과 최종 Debug 혼합 재실행 및 전체 Debug 에디터 빌드를 확인했다. Vulkan 냉간 PSO 준비는 첫 90초 제한을 넘었으며 600초 정확성 대기로 재검증했고, 로딩 성능 통과로 세지 않는다.
- MAT-7은 `progress`/미산정, 4.25는 열린 상태다. 당시 후속인 보조 compute/RT compatibility 조회 정리는 아래 여섯 번째 단계에서 처리했다. 제품 저장·reload·재import·쿠킹 재실행 게이트는 남는다. object 정렬의 교차/자체 겹침 한계는 제품 지원 범위에 명시하고, 특수 transport 혼합의 순서·검증은 기존 alpha/transmission 게이트에서 회수한다. 품질·전체 CPU/GPU 비용은 MAT-9가 소유한다.

### MAT-7 보조 compute·프레임 조회 분기 정리 (여섯 번째 구현 단계 · 2026-10-02)

- `LX::Runtime::ComputeGeneration`이 CS bytecode·compile/cooked identity·layout/PSO를 보관한다. Scene transform/lookup/SSS/refraction/Volume과 공통 Forward+ culling, 독립 bake/evaluate/resolve의 frame/result도 소유자를 유지한다. 후보 실패 보존과 공유 native handle retirement 경계를 graphics와 맞춘다.
- GBuffer/Forward RT draw는 프레임의 accepted graphics owner만 소비한다. 소유권 없이 최신 registry를 다시 조회하던 분기를 제거하고 렌더 fixture를 같은 준비 계약으로 전환했다. Code 입력용 ShaderMeta registry·variant preparation/진단 cache와 generic property는 유지한다.
- ABI·SceneHost 11·BRDF 1024/environment 4096을 유지한다. 외부 precompiled helper의 계약 없는 입력과 동기 compute PSO API는 별도 경계로 남는다. 정확한 빌드·실행 범위와 한계는 [compute 소유권 검증 기록](../analysis/MAT7ComputePipelineOwnership.md)을 따른다.
- 후속 DX12 회귀에서 MeshSurface 테스트의 Core/Layered product index와 IBL model 지정 혼동을 수정했다. 제품 수학·tolerance 변경 없이 Debug/Release 각각 1,117,767 checks·454,560 GPU 성분이 통과했고, Debug SurfaceBatch 19,387 checks·ScenePacket 169 checks도 통과했다. sampler 즉시 결과가 deferred transform으로 오인되지 않는 소유자 분리 검사도 포함한다. 최종 빌드 결과와 로그는 위 검증 기록에 둔다.
- MAT-7 `progress`/미산정, PHASE 4.25 열린 상태를 유지한다. 당시 후속인 제품 편집 저장·reload·실패 보존·재import·재쿠킹 회귀는 아래 일곱 번째 단계에서 회수한다. alpha/특수 transport 혼합 순서와 MAT-9 품질·전체 비용 수용도 기존 게이트에 남긴다.

### MAT-7 제품 편집·복구·쿠킹 회귀 (일곱 번째 구현 단계 · 2026-10-02)

- 명시적 Reload가 손상된 primary 대신 오래된 `.bak`을 성공으로 읽던 경로를 수정했다. 거절 시 문서/적용 재질을 보존하고 정상 외부 수정은 재import→Save→Apply의 실제 Scene 픽셀로 확인했다.
- **LXMC/artifact v4**는 생성 ShaderMeta 계약의 CEDO tree를 보관한다. cooked 복구뿐 아니라 generation 재검증·재쿠킹도 바이너리 계약을 사용한다. schema/typed defaults·자원·source digest·backend stage/reflection 검증을 유지하며 Player의 text parser 2회를 0회로 수정했다. v3는 재쿠킹, CEMF schema·SceneHost 11·1024/4096은 유지한다.
- 생성/복구 72항목, Debug/Release 반복 cook·공통 consumer 26항목·Code runtime 35항목·pipeline runtime 212항목·source-free graphics identity 4항목 및 실제 encrypted PAK/Player를 검증했다. graph 준비 완료/Scene compile 0/parser 0과 패키지 bytes 보존을 요구한다.
- 임시 fixture의 0.8 배율이 현재 1.5를 덮어 적용하던 검증 경로를 수정했다. 작업 프로젝트의 배율을 상속하고 에디터 시작·재시작의 실제 적용을 대조한다. 전체 LX 제스처/DPI 수용과 분리한다.
- UI·Scene/Game·실제 Player의 실행 범위와 최종 결과는 [제품 편집·복구 검증 기록](../analysis/MAT7ProductEditingRecovery.md)을 따른다. MAT-7 `progress`/미산정과 4.25 열린 상태를 유지한다. 다음 MAT-7 게이트는 일반 alpha와 SSS/refraction/Volume의 혼합 순서·굴절 배경 수용이며 LX 편집 마감/MAT-9 품질·전체 비용은 별도다.

### MAT-7 특수 transport·일반 alpha 혼합 (여덟 번째 구현 단계 · 2026-10-02)

- Code/Core/Layered/SSS의 alpha와 physical transmission을 공통 Forward+의 카메라 깊이 순서에 넣었다. SSS의 overflow 광원도 같은 light list를 소비한다.
- SSS/굴절만 임시 surface depth/owner를 만들고 Scene의 불투명 depth/owner를 보존한다. 굴절 직전 현재 HDR을 복사하여 뒤쪽 alpha를 배경에 포함한다. Volume은 배경과 각 Forward 표면 깊이까지의 transport를 구별한다.
- **SceneHost identity 12**로 재쿠킹한다. LXMC/artifact v4·1024/4096은 유지한다. 혼합 24조건·Volume 33프레임을 각각 DX12 Debug/Release에서 통과했다. 기존 굴절/SSS와 실제 Editor/Player 회귀도 통과했다. 카메라 Volume ray가 far projection 때문에 뒤집히는 결함을 수정하고 배경/굴절 결과를 각각 독립 적분과 대조했다. [혼합 transport 검증 기록](../analysis/MAT7ForwardTransportComposition.md).
- 정렬은 object/draw 단위다. 굴절 배경은 합성 컬러 한 장과 불투명 깊이를 사용한다. 교차 표면 OIT·층별 투명 ray 추적을 이번 지원 범위로 세지 않는다. 매질 역변환의 강한 흡수 오차, 활성/비활성 Volume 및 scratch/최초 준비의 전체 비용은 MAT-9가 수용한다.
- 이 단계로 **MAT-7을 done**으로 판정한다. `days:null`/기성 추가 0을 유지한다. PHASE 4.25는 LX-1/3/3H 마감과 MAT-9 수용 때문에 계속 열린 상태다. 위 첫~일곱 번째 단계의 `progress`/identity 11은 당시 기록이다.

**LX 공통 runtime 전환의 수용 조건:** 기존 코드 재질의 Float2/Float4x4 등 generic property를 지원하고 Graph 전용 Principled/BSDF ABI를 강요하지 않는다. authored ShaderMeta의 name 기반 값과 graph의 stable ID override를 입력 어댑터에서 해석한 뒤 동일 generation/instance·resource/PSO owner로 소비한다. reload 실패 보존, texture alias/색공간, mixed Graph/Code draw와 cooked 재실행의 완료 증거는 위 단계별 기록에 둔다. 그래프 UI는 LX-3, 품질/성능 수용은 MAT-9가 소유한다.

현재 제품 기준선 **BRDF 1024 / environment 4096**과 품질 상한은 유지한다. BASE-0/후속 구조 작업은 현재 구현 기반을 사용할 수 있으며 MAT-7 재개방이나 MAT-9 전체 수용을 일괄 착수 선행으로 추가하지 않는다. 공통 통합 뒤 같은 입력의 회귀를 회수한다.

압축 전 노트는 [구현·검증 이력](archive/Phase425ImplementationHistory.md), 날짜별 증거는 아래 접힌 기록에 보존한다.

---

## 1. 고정한 비교 범위

### 포함

- Blender 5.1.1 Material Preview/EEVEE의 Principled 재질 의미.
- Base Color, Metallic, Roughness, IOR/Specular IOR Level, Normal, Alpha,
  Emission Color/Strength.
- Coat, Sheen, Anisotropy, Iridescence, Transmission, Subsurface와 Volume 입력.
- EEVEE 5.1.1이 평가하지 않는 anisotropy/thin film은 같은 버전 Cycles의 재질 의미를 기준으로 삼고, 게임 엔진 근사와 spectral 렌더 오차를 별도로 판정한다.
- 같은 mesh·UV·texture·HDRI·light·camera에서 tone mapping 전 linear HDR 비교.
- Material Graph 저장/재개방, typed node/pin, deterministic Slang codegen.
- Blender 5.1.1 Shader Editor 대비 node tree·socket·link·group/interface·reroute·frame·값 위젯·편집 동작 대응표와 노드별 지원 판정.
- 게임 엔진용 Standard/Layered/Special 품질 tier와 자동 Deferred/Forward routing.

### 제외

- 좌표계·축 변환 비교. UV/tangent 제품 결함은 PHASE 4에서 먼저 닫는다.
- AgX, auto exposure, bloom, color grading, AA, UI 합성 등 후처리.
- Blender compositor, Cycles 전용 path-tracing 효과, viewport overlay.
- 그림자 품질, local reflection probe, AO처럼 장면/렌더러가 주도하는 항목.
- 런타임에서 임의 graph topology를 매 프레임 바꾸는 기능.

따라서 “Blender와 같은 색”의 판정은 최종 스크린샷의 우연한 유사성이 아니라 **같은 입력의
pre-tone linear HDR 재질 응답**이다. 후처리 차이로 재질 오차를 덮지 않는다.

---

## 2. 현재 구조에서 막히는 지점

| 현재 계약 | 문제 | 목표 계약 |
|---|---|---|
| `StandardMaterialProperty`의 flat 숫자·texture 이름 | lobe, closure, 색/벡터 의미와 연결 구조를 표현하지 못함 | typed `MaterialInputs`와 `PrincipledSurface` |
| `ShaderPropertyType`의 Float/Vector/Texture 중심 타입 | Color, Normal, BSDF/closure, enum/domain 검증 불가 | Color/Vector/Normal/Texture/Sampler/Surface/Closure typed pin |
| GBuffer/Forward의 고정 4 texture table | graph가 입력을 늘리면 register 수동 확장과 제품 코드 수정 반복 | reflection 기반 material resource table |
| GBuffer/Deferred/Forward의 개별 평가 | route가 바뀌면 외형이 바뀔 수 있음 | 공용 Slang `EvaluateMaterial`/BRDF/IBL module |
| Opaque/Transparent 중심 route | alpha와 물리 transmission을 구분하지 못함 | Opaque/Mask/Blend + Transmission/Volume route |
| 단일 Standard 재질 비용 모델 | 저가 재질도 고급 lobe 비용을 부담하거나 artist가 내부 pass를 알아야 함 | `MaterialFeatureMask`와 자동 tier/routing |
| generic `.shadergraph` 구상 | Material output과 Fullscreen/Compute output 책임이 섞임 | 같은 graph 기반, 명시적 `domain=material|pass`와 서로 다른 output 계약 |

현재 활성 구현은 `domain=material`뿐이다. `pass`는 LX 공통 기반에서 나중에
열 수 있는 별도 domain이며, 현재 [C# Pipeline 저작 계획](CSharpRenderPipelinePlan.md)의
완료 조건이나 이 문서의 40일에 포함하지 않는다.

---

## 3. 최종 구조

```text
.shadergraph (domain=material)
    -> typed Graph IR
    -> constant fold / dead-lobe elimination / feature extraction
    -> generated .shadermeta + .slang (같은 generation)
    -> common Shader/Material compile·reflection·cook·binding/PSO
    -> shared Slang EvaluateMaterial (MaterialInputs + MaterialFeatureMask)
    -> PrincipledSurface
         ├─ Standard : Deferred
         ├─ Layered  : specialized Deferred 또는 공통 Forward+
         ├─ Alpha Blend : 공통 Forward+
         └─ Special : 공통 재질 + 필요한 SSS/refraction/volume 보조 처리
```

핵심 규칙:

1. `PrincipledSurface`는 surface 평가 결과의 논리 ABI다. MAT-5의 Volume은 별도
   `VolumeInputs`→`PrincipledVolume` Output closure이며 surface density 필드로 저장하지 않는다.
2. `MaterialFeatureMask`는 graph를 훑어 얻는 정적 feature 집합이다. artist가 pass/register를
   직접 고르지 않는다.
3. Deferred와 Forward+는 같은 `EvaluateMaterial`·BRDF·IBL 모듈을 소비한다. route 변경이
   색 변화가 되어서는 안 된다.
4. graph topology와 feature permutation은 cook 때 고정한다. runtime instance는 값과 texture만
   override한다.
5. Graph 원본과 ShaderMeta를 두 개의 독립 저작 입력으로 유지하지 않는다. Graph 모드에서는 Meta/Slang을 함께 재생성한다. 직접 작성한 Code 모드도 같은 ShaderMeta/Material 소비 계약에 합류한다.
6. 무한 uber shader 하나를 만들지 않는다. coarse tier permutation + specialization + cook-time
   constant folding으로 variant 폭과 분기 비용을 제한한다.

---

## 4. 아트 팀용 비용 계약

| Tier | 기본 route | 재질 기능 | 저작 경험 |
|---|---|---|---|
| Standard | Deferred | base/MR/normal/IOR/specular/emission/mask | 기본값. 가장 싼 green 배지 |
| Layered | specialized Deferred 또는 공통 Forward+ | coat/sheen/aniso/iridescence | yellow 배지와 비용 증가 이유 표시 |
| Special | 공통 재질 + 필요한 transport 처리 | transmission/subsurface/volume | red 배지, 겹침/화면 점유 비용 경고 |

- artist에게 descriptor register, MRT, PSO 키, Slang specialization을 노출하지 않는다.
- Material Inspector는 예상 route, feature mask, texture sample 수, variant 수, 투명 overlap 위험을
  읽기 쉬운 배지와 문장으로 보여 준다.
- 품질 preset은 의미를 삭제하지 않고 샘플 수·근사 수준을 조절한다. 기능이 지원되지 않으면
  조용히 다른 재질로 바꾸지 않고 cook/import 단계에서 이유와 대체 경로를 표시한다.
- preview와 Scene/Game 결과는 같은 generated Slang과 같은 material generation을 사용한다.

---

## 5. 실행 순서와 공수

| ID | 내용 | 상태 | 선행 | 일 |
|---|---|---|---|---:|
| `MAT-0` | Blender 5.1.1 reference scene·pre-tone HDR golden 고정 | ✓ | PHASE 4 W9 | 2 |
| `MAT-1` | `PrincipledSurface`·`MaterialFeatureMask` 공용 Slang ABI | ✓ | MAT-0 | 4 |
| `MAT-2` | typed Graph IR·domain·schema migration·round-trip | ✓ | MAT-1, LX-2 독립 예제 게이트 | 4 |
| `MAT-3` | core Principled 의미·기본값·IOR/specular/emission/alpha | ✓ | MAT-1 | 4 |
| `MAT-4` | coat·sheen·anisotropy·iridescence layered lobe | ✓ | MAT-3 | 4 |
| `MAT-5` | transmission·subsurface·volume와 Special route | ✓ | MAT-4 공용 레이어 | 4 |
| `MAT-6` | material-domain graph→deterministic Slang codegen·diagnostic | ✓ | MAT-2, MAT-5 공용 의미 | 4 |
| `MAT-7-BASE` | 기존 LX binding·generation/PSO·쿠킹 구현 기반 보존 | ✓ 이력 | MAT-4~MAT-6 | 3 |
| `MAT-7` | Graph→ShaderMeta/Slang·LX 공통 Material/Forward+·alpha/특수 transport 통합 | 완료 | MAT-6, MAT-7-BASE | 미산정·기성 추가 없음 |
| `MAT-8` | artist Inspector·preview·cost badge·unsupported 설명 | 완료 | MAT-2, MAT-7 | 3 |
| `MAT-9` | Blender material grid/furnace·route parity·성능 gate | 진행 | MAT-7, MAT-8 | 8 |
| **합계** | 완료 기반 32 + MAT-9 잔여 8 | 4.25 진행 중 | 미산정 MAT-7 완료 공수 역산 없음·LX 별도 | **40 + 미산정** |

구 PHASE 4의 `SRP-3` 공용 graph 기반과 단일 PBR 레인의 material 의미·lobe·codegen 몫을
이 열 개 슬라이스가 대체한다. 옛 PBR ID는 새 작업과 병행하지 않는다.
`MAT-2`는 Lattice의 공통 node/pin/connection·layout/serialization 계약을
Material domain의 typed IR에 적용하고, `MAT-6`은 그 결과를 Slang·resource table과 진단으로
내린다. `LX-3`과 동일 산출물을 중복 완료 처리하지 않는다. Lattice 기반의 다른
창 재작성과 `imgui-node-editor` 최종 제거는 LX 계획에서 별도로 판정한다.
`MAT-2`의 4일을 Blender 노드 전체 목록 구현 완료로 간주하지 않는다. LX-0 대응표에서
정한 지원 노드/소켓과 편집 기능을 별도 범위·공수·제품 게이트로 관리한다.

<details id="implementation-history">
<summary>날짜별 구현·측정 이력 — 당시 상태와 공수 표기 보존</summary>

아래는 각 작업 시점의 기록이다. 현재 완료/잔여 판정은 §0과 실행 표가 소유한다. 과거의 “MAT-7 완료”, “MAT-0~8 완료”, “32/34”는 공통 재질 통합 완료나 현재 공수로 사용하지 않는다.

### MAT-0 고정 결과 (2026-09-28)

- [`Tools/blender/fixtures/material-reference-5.1.1/README.md`](../../Tools/blender/fixtures/material-reference-5.1.1/README.md)의 Blender 5.1.1 장면, 15개 core/layered/special 구체 사례, 입력·환경·해시 manifest를 비교 기준으로 고정했다.
- `material-grid-linear.exr`은 Raw view transform에서 저장한 scene-linear RGBA32F다. AgX PNG는 표시 전용이다. 최대 선형 red 값 13.71875로 1.0 초과 HDR 값 보존을 확인했다.
- 별도 출력 디렉터리에서 동일 입력으로 다시 렌더하고 EXR 전체 RGBA 픽셀의 최대 절대 차이 `0.0`을 확인했다. 제품 결과와 Blender 결과의 허용 오차 판정은 `MAT-9`에서 한다.
- EEVEE 볼륨 경계가 보이는 `special_volume`은 특수 재질의 독립 진단 사례다. shadow/AO/probe/post의 비교를 이 golden으로 대체하지 않는다.

### MAT-1 공용 ABI 결과 (2026-09-28)

- [`PrincipledMaterialAbi.md`](../design/PrincipledMaterialAbi.md)에 `MaterialInputs` → `EvaluateMaterial` → `PrincipledSurface`의 논리 값·색/normal 공간과 정적 `MaterialFeatureMask` 비트를 명시했다. 이 구조를 cbuffer나 자산 byte layout으로 직렬화하지 않는다.
- GBuffer·Forward가 같은 평가 함수를 호출하고, Deferred는 기존 MRT에서 결과를 복원해 공용 `BuildPbrSurface`·`EvaluatePrincipledIbl`을 소비한다. b2·texture reflection·MRT 형식과 현행 Standard factor×texture 출력은 유지했다.
- 저장소의 Slang 2026.14·고정 DXC로 DXIL/SPIR-V 70개 엔트리 컴파일, 미지원/unknown feature mask의 지정 진단 거부 4건을 통과했다. 기존 Debug host에 변경 Slang을 지정한 `dx12.gbuffer`·`dx12.forwardshade`·`dx12.iblshade` GPU readback도 통과했다.
- MAT-1의 고정 Standard 지원 mask는 `0x003F`였다. 아래 MAT-3에서 공용 core 의미를 `0x007F`까지 구현했다. 고정 pass의 비기본 IOR binding과 자동 route는 MAT-7, 고급 lobe는 MAT-4/MAT-5, graph feature 추출·codegen은 MAT-6, Blender golden·제품 route parity와 성능은 MAT-9에 남는다.

---

### MAT-2 문서·IR 결과 (2026-09-28)

- `Lattice/Material`에 `.shadergraph` schema 1, typed Blackboard·socket metadata·공유/중첩 group과 결정적 Material IR을 구현했다. 위치·접힘·Frame·view는 IR에서 제외한다.
- Blender 기준선의 초기 6개 정의(Principled 31입력 포함), typed Parameter/Reroute, Sampler·Closure와 domain/schema/enum/parameter 검증을 연결했다. Blender 전체 노드나 Slang 평가 구현의 완료는 아니다.
- exact 저장 왕복, schema 0→1 승격, 미등록 node payload의 읽기 전용 보존, 손상 backup 복구, migration/교체 실패의 원본 유지, 마지막 정상 IR 유지, Material metadata를 포함한 그룹 Undo/Redo를 검사했다.
- 독립 예제 Debug/Release에서 신규 95개 검사와 기존 `LX_SELF_TEST_OK`를 통과했다. 계약·증거는 [MaterialGraphSchema.md](../design/MaterialGraphSchema.md), 고정 문서는 `Tools/LatticeExample/fixtures/material/`에 있다. Editor Canvas adapter·HTTP는 LX-3/LX-3H, Slang과 GPU generation은 MAT-6/MAT-7에 남는다.

### MAT-3 core 평가 결과 (2026-09-28)

- [PrincipledCoreSemantics.md](../design/PrincipledCoreSemantics.md)에 Blender core 기본값, clamp, IOR/Specular Level의 F0·유효 IOR, dielectric tint·금속 F82 Tint, geometric normal, HDR emission과 별도 opacity를 고정했다.
- 공용 직접광이 동일 Fresnel을 소비하고 IOR-aware IBL의 적분·소비를 분리했다. 적분은 bake/probe 함수이며 제품 pixel shader에 표본 루프를 넣지 않았다. 기존 glTF 수치·MRT·Schlick/DFG는 명시적인 Standard 어댑터로 보존한다.
- RTX 4070 Ti D3D12에서 23개 사례×9개 각도, CPU double 기준 8,280개 대조를 통과했다. 최대 정규화 오차 0.000011920798. 9개 Blender core 소켓 기본값도 대조했다. core DXIL/SPIR-V 2개, 기존 pass 70개 컴파일과 미지원/unknown/미배선 route 10개 거부를 통과했다.
- 변경 Slang을 현행 Debug host에 지정한 GBuffer·Forward/reference 16,384픽셀 불일치 0·Water/Wind·IBL/AO를 통과했다. C++ Editor 전체 신규 빌드나 Blender EXR 교차 판정은 아니다. 비기본 IOR의 제품 lookup/packing·graph binding은 MAT-6/MAT-7, golden·route·성능은 MAT-9에 남는다.

---

### MAT-4 Layered 평가 결과 (2026-09-28)

- [PrincipledLayeredSemantics.md](../design/PrincipledLayeredSemantics.md)에 Sheen→Coat→base 순서, LTC sheen·coat tint/emission 감쇠·별도 coat normal, anisotropic GGX/tangent 회전과 thin film을 구현·고정했다.
- Coat의 물리 Fresnel과 base의 IOR/F82 반사를 분리했다. 직접광과 IBL bake/소비가 같은 lobe를 사용하며, white furnace의 multiple scatter·하위 에너지 감쇠를 포함한다. 제품 pixel shader에 적분 루프를 넣지 않는다.
- 13개 Blender 입력 기본값, 고정 Apache-2.0 Sheen 계수 1,024개, 35사례×8시선×7조합의 GPU/CPU **216,776개** 검사, 에너지 범위 **5,712개**, numeric golden **1,960행/7,840성분**을 통과했다. 최대 정규화 오차 0.0000011920929.
- Layered DXIL/SPIR-V 14개 컴파일·의존 mask 거부 8개, MAT-3 core 8,280개 검사·기존 pass 70개 컴파일·미지원/unknown/미배선 route 거부 34개, 현행 Debug host GPU 회귀 3종을 통과했다. permutation별 compile byte 수와 raw 수치 artifacts를 남겼다.
- Thin film은 650/550/450nm RGB 근사이며 EEVEE 전체 parity나 Cycles spectral 일치 판정이 아니다. 기능별 numeric golden은 독립 closure 기준이다. 그래프 생성·제품 lookup/environment convolution/packing·자동 route는 MAT-6/MAT-7, rendered golden·route parity·성능과 근사 수용은 MAT-9에 남는다.

---

### MAT-5 Special 평가 결과 (2026-09-28)

- [PrincipledSpecialSemantics.md](../design/PrincipledSpecialSemantics.md)에 Alpha와 별도 Transmission,
  authored IOR 기반 isotropic glass·front/back TIR·GGX BTDF, RGB Radius/Scale/IOR/anisotropy 확산 profile,
  별도 Volume closure의 흡수·산란·발광·HG phase·Beer–Lambert segment 합성을 구현·고정했다.
- Metal/Transmission/SSS의 에너지 비중을 분리하고 coat/sheen 하위 감쇠를 적용했다.
  SSS의 입사 source와 destination 색/weight를 분리해 후속 확산 pass의 중복 곱셈을 막았다.
  Prepared integral/transport를 소비하는 Special Forward API와 Deferred/필수 자원 누락 거부 계약을 구현했다.
- Blender 12개 기본값과 Volume exporter exact 재생성, 35사례×6시선×7마스크의 GPU/CPU·독립성
  **202,049개** 검사, energy bound **4,410개**, numeric golden **4,410행/17,640성분**을 통과했다.
  최대 정규화 오차 0.0000011920929. Profile/HG 정규화·작은 optical depth·segment 분할 합성도 검사했다.
- Special DXIL/SPIR-V CS/PS 28개 컴파일·route/의존성 거부 22개, core/Layered GPU 및 pinned golden,
  기존 pass 70개 컴파일·unknown/미배선 route 거부 58개, 현재 Debug host GPU 회귀 3종을 통과했다.
- SSS는 normalized diffusion 근사이며 Cycles Random Walk가 아니다. Transmission은 단일 경계의
  에너지 정규화 모델, Volume은 homogeneous single scattering이다. Blackbody/attribute/heterogeneous
  기능과 닫힌 solid의 경로 추적은 지원 범위에 포함하지 않는다.
- 공용 평가·명시적인 Special Forward 요구 조건의 완료다. Graph operator/codegen과 제품 자원 배선·
  material별 SSS/refraction/Volume path·자동 cook route는 MAT-6/MAT-7, rendered parity·근사 수용·성능은 MAT-9에 남는다.

---

### MAT-6 생성·진단 결과 (2026-09-28)

- [MaterialSlangCodegen.md](../design/MaterialSlangCodegen.md)의 초기 지원 노드를
  결정적 Slang·typed resource/Blackboard metadata·source map으로 낮췄다. 공유/중첩 그룹과
  인스턴스 입력을 해석하며 전체 Principled 그룹의 disabled/implicit UV 입력 노출을 수정했다.
- active output에서 live 입력만 생성하고 상수 float32 접기, dead lobe/resource/parameter 제거,
  Color/Alpha 공유 sample과 Special Forward 요구 조건을 추출한다. scope/node/pin/property와
  group invocation을 진단하며 실제 Slang 오류를 줄 위치로 매핑한다.
- 같은 의미·배치 변경·저장 순서·재개방의 identity, 공용 dependency 변경 무효화와 실패 시
  마지막 정상 program/전체 compiled artifact/generation 유지 경계를 검증했다.
- 20개 graph × 5개 입력의 RTX 4070 Ti 실제 texture/sampler GPU **3,600성분** 대조,
  DXIL/SPIR-V CS/PS **93개** 성공과 오류/route/publication 거부 **31건**을 통과했다.
  source/graph 4쌍과 생성 metadata JSON을 고정/검사했다. 기존 pass 70개·거부 58건도 재검사했다.
- 독립 ImGui 예제 Debug/Release의 기존 Material 95개 및 신규 생성기 21개 검사를 통과했다.
  raw 증거는 `Build/Obj/MaterialCodegenProbe/`다. CPU program publication과 독립 GPU 검증이다.
  제품 `.shadermeta` pass/reflection adapter·실제 texture view·lookup/SSS/transport 자원과
  PSO/Scene 교체·자동 cook route는 MAT-7, Editor UI/HTTP는 LX-3/LX-3H에 남는다.

### MAT-7 구현·검증 기록 (2026-09-28~29)

아래 진행 상태는 각 개발 단계 당시의 기록이다. 현재 종료 상태와 실행 증거는
마지막 제품 통합 종료 결과 및 [MaterialGraphProductIntegration.md](../design/MaterialGraphProductIntegration.md)가 소유한다.

- [MaterialGraphProduct.md](../design/MaterialGraphProduct.md)에 제품 register/reflection,
  capability 기반 tier/route·미지원 거부, 전체 generation/PSO 교체와 texture owner fence,
  typed LXMC/CEMF·AssetCooker·loose/encrypted PAK 계약을 구현·기록했다.
- 실제 DXIL/SPIR-V reflection과 D3D12 SRGB/override readback, native PSO 거부·마지막
  정상 generation 보존을 검사했다. AssetCooker Debug/Release 결과를 compiler 호출 없이
  재개방하는 게이트를 추가했다. raw 로그와 판정은 `Build/Obj/MaterialProductProbe/`다.
- DataSystem/Material의 GUID 기반 불변 LX generation·공개 numeric/Texture GUID instance
  override·typed YAML/CEMT/CEDO 저장 재개방과 실패 복구를 연결했다. 독립 runtime 회귀
  43개 및 제품 DataSystem/Texture/Material 직접 호출 Debug/Release 각 26개 검사를
  통과했다. clone/finalize 보존과 stale asset-change reload/removal도 검사했다.
- 제품 RHI render binding adapter로 b2·t16+·s3+ 독립 root/table·texture 업로드를 연결했다.
  실제 엔진 DX12 서비스 오프스크린 draw에서 Debug/Release 각 168개 검사·64개 GPU 성분,
  네 sampler의 filter/address/SRGB·숫자 변경·캐시 재사용과 실패 packet 보존을 통과했다.
  Scene 적용과 물리 lookup/convolution, 동시 인플라이트 수명 판정은 포함하지 않는다.
- Core/Layered evaluated-point GPU bake와 별도 base/coat/sheen 환경 convolution,
  lookup 소비 draw를 실제 엔진 RHI로 검증했다(Debug/Release 각 19,351개 검사·18,240개 GPU 성분).
  [PrincipledIblBake.md](../design/PrincipledIblBake.md)가 입력/환경 세대·수명과 제한된 범위를 소유한다.
  Scene lookup 배치/보간/재사용, texture-varying 입력·환경 MIS/수렴·실시간 예산은 후속이다.
- [MaterialGraphScenePacket.md](../design/MaterialGraphScenePacket.md)의 render owner 경계를 추가했다.
  graph instance·coverage/queue·owning PSO·IBL·recording binding을 함께 준비하고, 제출 성공 확인 뒤
  정확한 recording/fence로 게시한다. callback만으로 정상 packet을 바꾸지 않는다.
  실제 두 GPU 제출을 in-flight로 고정해 교체/완료 해제·실패/abort·중간 제출·환경 세대와
  Opaque/Masked/Blended 분리를 검사했다(Debug/Release 각 165개 검사·GPU 16성분,
  DXIL/SPIR-V 10개, GPU validation 0건). 제품 Scene pass 설치는 별도 후속이다.
- 당시 Scene의 graph pass 소비와 실제 route 분류는 미연결이었다. 아래 후속 Scene host에서
  Core/Layered Opaque/Masked 소비를 연결했다. refraction·재질별 SSS·Volume 합성, cooker의 현재 host/compiler
  검증과 자동 graph 수집은 남아 있다. 현재 cooker는 먼저 검증된 specialization을 받는다.
  MAT-7 전체를 완료로 세지 않으며 완료 공수 26일과 잔여 8일 표시는 유지한다.
- [MaterialGraphSurfaceBatch.md](../design/MaterialGraphSurfaceBatch.md)에 공간·시선별 GPU 평가
  배치를 추가했다. texture/SRGB·명시적 LOD·월드 frame·eye·typed override를 graph CS에서
  평가하고 GPU point buffer를 물리 bake와 bounded graphics consumer에 직접 연결한다.
  완료 readback acceptance와 기록을 구분하며 잘못된 GPU 입력은 진단 표식으로 거부한다.
  실제 Scene mesh/skin 입력 생성·lookup 보간/오차/예산·RenderGraph 설치·async 게시는 후속이다.
  이 단계의 수치 증거는 연결 문서가 소유하며 MAT-7 완료 공수는 추가하지 않는다.
- [MaterialGraphMeshSurface.md](../design/MaterialGraphMeshSurface.md)에 제품 Model 정점 ABI의
  immutable 입력, GPU skin/world frame → graph → IBL 경로와 GPU Scene packet 게시 경계를
  연결했다. 8개 layout·3포즈의 CPU double 대조, 실제 indexed geometry 진단과 bounded lookup
  draw, 완료 검증 뒤 게시·실패 보존·취소/할당 중 구간 전환 거부를 검사했다.
  큰 메시 triangle 분할·원본 remap/예산 거부와 재질 평가 전 UV/world frame 보간,
  명시적 footprint LOD를 추가했다. 실제 Scene visible sample 수집·texture별 footprint,
  lookup 해상도/재사용/오차/실시간 예산·RenderGraph/async polling 설치는 남는다.
  수치 증거는 연결 문서가 소유하며 MAT-7 완료 공수는 추가하지 않는다.

- [MaterialGraphRasterSurface.md](../design/MaterialGraphRasterSurface.md)에 불투명 depth/MRT의
  가시 픽셀·원근 보간·fine UV derivative → GPU graph → IBL 경로를 추가했다.
  단일 재질 chunk 묶음·최대 4096픽셀의 독립 RHI RenderGraph 패스다. 같은 graph의 재질 평가·IBL 준비/선언/순차·병렬 기록을 [MaterialGraphPassRecording.md](../design/MaterialGraphPassRecording.md)에 연결했다.
  수치 증거는 연결 문서가 소유한다. 실제 Scene draw pool·공유 depth·컬러/직접광·
  Scene pass/async 게시와 자동 host 쿠킹은 남으며 MAT-7 완료 공수는 추가하지 않는다.

- [MaterialGraphSharedDepth.md](../design/MaterialGraphSharedDepth.md)에 current-pose mesh/skin 생산을
  같은 graph에 포함하고 Core/Layered 재질이 read-only depth와 정확한 opaque draw winner를
  공유하는 경로를 추가했다. coplanar tie·clipping·그리기 순서·스키닝 입력 복사·실패 복구를
  독립 RHI에서 검증한다. 실제 Scene draw pool/per-view·legacy/Masked depth ownership·컬러/조명
  설치는 남으며 MAT-7 전체 상태와 완료 공수를 변경하지 않는다.

실제 Scene 입력 밀봉은 [MaterialGraphSceneInput.md](../design/MaterialGraphSceneInput.md)에
연결했다. producer의 Mesh/Foliage proxy/delta에서 typed instance·coverage를 복사하고,
실제 draw pool/per-view가 선택한 geometry·world·pose·camera를 owning plan으로 밀봉한다.
같은 입력의 native shared-depth graph 24개와 입력 실패 24종을 포함해 Debug/Release 각
773,635개 검사·GPU 649,168성분·GPU validation 0건을 통과했다.
후속 [MaterialGraphSceneHost.md](../design/MaterialGraphSceneHost.md)는 실제 Scene의
GBuffer·공유 D32·legacy/LX Masked 가림·Core/Layered 직접광/HDR 합성에 연결했다.
초기 설치 검증은 정확도 기준용 IBL 적분을 쓰는 4,096픽셀·64 draw 이하의 host였다.
Debug/Release 각 830,436개 검사·GPU 659,914성분, 실제 합성 graph 24개·가시 3,582픽셀·
실패 거부 120개·GPU validation 0건을 통과했다. 업로드 취소 시 삭제된 iterator를
재사용하던 어설션도 수정하고 새 texture 3개의 취소·재시도를 검증했다.
2026-09-29 [MaterialGraphSceneLookup.md](../design/MaterialGraphSceneLookup.md)에 일반 viewport의
정확한 평가 입력 수집·변경 픽셀 compute bake·재사용·실제 제출 뒤 게시를 추가했다.
host 기본 한도는 4,096²픽셀·4,096 draw·2 GiB lookup payload다.
Debug/Release 각 20,762,086개 검사·GPU 3,623,244성분, 실제 합성 36 graph·가시 42,292픽셀,
실패 거부 192개·새 적분 20,491/재사용 21,801픽셀·GPU validation 0건을 확인했다.
1920×1080 cold/warm은 희소 coverage 2프레임이며 dense Scene 성능 수용으로 세지 않는다.
일반 Scene의 cold/카메라 이동 성능·전체 메모리 수용, 실제 Editor Live Tick·Vulkan native 실행,
Special/투명·LX shadow caster·Decal lobe 수정·자동 Scene host 쿠킹은 남는다.
이번 연결 검증과 일반 해상도의 제품 완료를 구분하며 MAT-7은 진행 중이다.

같은 날 [MaterialGraphTextureFootprints.md](../design/MaterialGraphTextureFootprints.md)에
실제 Scene의 샘플별 최종 Vector fine derivative·이미지 크기/mip 수로 독립 LOD를
구하는 경로를 추가했다. 첫 텍스처 공통 LOD와 단일 텍스처 제한을 제거했다.
생성기 v2·golden 20 graph 대조·DXIL/SPIR-V 93개·GPU 3,600성분을 통과했다.
크기/mip 수가 다른 이미지 2개, 같은 이미지의 독립 Vector/sampler, fractional/마지막 mip,
parameter·이미지 크기 교체의 부분 캐시 갱신을 native D3D12 8프레임에서 확인했다.
Debug/Release 전체 회귀는 각 20,892,205개 검사·GPU 3,735,460성분·합성 44 graph/
43,644픽셀·거부 240개·GPU validation 0건이다. compute host는 명시적 LOD를 유지하며
named UV set·Blender 전체 노드 확대는 별도다. compiler v1 LXMC는 재쿠킹해야 한다.
일반 Scene 비용·전체 초기화/캐시 예산·자동 Scene host 쿠킹과 Editor/Vulkan 실행 검증은
계속 남으며 MAT-7 완료 공수는 추가하지 않는다.

### MAT-7 Scene generation 비동기 준비·교체 (2026-09-29)

[MaterialGraphSceneGeneration.md](../design/MaterialGraphSceneGeneration.md)에 generation/backend별
Slang worker 검증·DX12 native PSO worker 준비·8개 PSO 전체 ready 조건을 추가했다.
실제 DX12/Vulkan Scene consumer는 texture residency/parallel prefix 이전에 준비 상태를 고른다.
pending·실패·미설치 Blended 요청은 같은 epoch/view/Material 슬롯의 마지막 정상
instance·coverage와 현재 geometry를 사용한다. cold 슬롯은 준비 완료까지 해당 draw만 생략한다.
graph batch의 정확한 completion·티켓 성공 뒤 게시하고 stale 요청·abort·다른 epoch를 거부한다.

108개 source SHA-256이 유지된 Debug/Release 각 21,079,762개 검사·GPU 3,899,966성분,
합성 56 graph/45,626픽셀·실패 거부 312개·GPU validation 0건을 통과했다.
신규 12프레임에서 이전 재질 유지 7회·abort/pending ticket/stale 게시 거부 각 1회,
generation 준비/worker 6회·실패 memo 2개·native PSO worker 24회를 확인했다.
같은 ready generation의 numeric 교체는 재컴파일하지 않으며 다른 슬롯의 lookup을 재사용한다.

후속 Vulkan native PSO worker 준비·기본 draw 검증은 아래에 추가했다.
mesh/lookup 초기 CS의 동기 설치 제거·전체 native cache eviction/retirement 예산,
자동 Scene host/compiler 쿠킹·일반 Scene cold/카메라 이동 성능과 Editor/Vulkan 전체 Scene 실행이
남는다. Special/투명 transport·LX shadow caster·Decal lobe 수정·환경 MIS/수렴도 별도다.
MAT-7은 진행 중이며 완료 공수를 추가하지 않는다.

### MAT-7 Vulkan native PSO 비동기 준비 (2026-09-29)

[MaterialGraphSceneGeneration.md §6](../design/MaterialGraphSceneGeneration.md)에 Vulkan의 owned
descriptor worker 생성·owner 완료 게시·실패 memo·64개 admission·무효화한 작업 회수·
shutdown join을 추가했다. legacy synchronous 소비자는 accepted job을 공유한다.
graphics/compute shader module은 PSO 생성 뒤 성공·실패 모두 해제한다.
native Vulkan 실행에서 생성 Scene shader의 실제 entry 이름과 기존 고정값의 불일치를
발견해 SPIR-V의 해당 stage 이름으로 설치하도록 수정했다.

독립 Vulkan 검증은 Core/Layered Scene의 16 Ready 응답·14 native worker(동일 GBuffer 2개 공유),
stale 작업 65개 회수·pending/중복/실패 memo·원본 데이터 교체·admission·종료/재시작,
실제 worker PSO draw/readback을 포함한다. 554개 source SHA-256 고정·Debug/Release 빌드·
RTX 4070 Ti native 실행에서 각각 768픽셀/3,072성분·주 회귀 83 accepted/worker·
기존/생성 compute entry 설치와 잘못된 stage 거부·device 종료까지 validation 0건을 확인했다.
Vulkan 전체 Scene 합성·Editor Live Tick·자동 Scene host 쿠킹은 MAT-7 잔여다.
초기 CS 전체 비동기화·전역 cache 최적화는 별도 개선이며,
일반 Scene의 최종 시간/메모리 수용은 MAT-9가 소유한다.
MAT-7 status는 progress, 완료 공수는 추가하지 않는다.

### MAT-7 실제 Scene SSS transport (2026-09-29)

[MaterialGraphSceneSubsurface.md](../design/MaterialGraphSceneSubsurface.md)에 SSS surface의
Special Forward route·generation당 10개 PSO·source/profile 7 MRT·독립 금속/유전체 반사,
draw owner mask와 world-distance dipole gather·HDR 합성을 연결했다.
입사 source와 destination 색/weight를 분리하고 0 Radius 채널·0 Scale은 local source로 처리한다.
legacy/배경·다른 draw·가림 경계를 섞지 않고 공유 depth를 읽기 전용으로 유지한다.
SSS payload는 픽셀당 224 B이며 기본 프레임 예산은 512 MiB다.
562개 source SHA-256 변경 0개로 VS18/v145 Debug/Release 빌드·native D3D12를 검증했다.
각 구성 SSS 12 graph·가시 4,503픽셀·Masked 구멍 648개·119,414개 검사·GPU 108,072성분,
기존 전체 21,079,762개 검사·GPU 3,899,966성분·Scene 합성 56 graph·generation 12 frame을
통과했다. 모든 실행의 WARNING 이상 GPU validation은 0건이다. raw 로그는 연결 문서가 소유한다.

이는 9×9 가시 표면의 normalized dipole 근사다. 숨겨진/화면 밖 표면·solid 내부 Random Walk,
Blender pixel parity·dense Scene 성능의 수용을 완료로 표시하지 않는다.

### MAT-7 실제 Scene transmission/refraction (2026-09-29)

[MaterialGraphSceneRefraction.md](../design/MaterialGraphSceneRefraction.md)에 단일 투과 표면의
실제 Scene 연결을 추가했다. 최초 GBuffer에서 투과 draw를 제외해 불투명 뒤쪽 장면을
완성한 뒤 HDR/depth를 복사하고, 가장 가까운 투과 표면의 GBuffer·lookup·굴절 compute·
Special HDR을 합성한다. 최종 owner/depth를 후속 pass에 전달한다.
매끈한 Snell 방향과 32개 GGX rough 방향, 현재 환경 fallback, 뒤쪽 면 IOR 역수·전반사,
금속/SSS 혼합과 Tint/Thin Film을 처리한다. 배경 SSAO는 투과 표면에 적용하지 않는다.
같은 이전 lookup 자원의 중복 import를 막고 실제 최종 상태를 다음 frame에 전달한다.

capture·glass/transport sample·HDR/D32 복사 payload는 124 B/픽셀, refraction 기본 프레임
예산은 512 MiB다. 기존 224 B/픽셀 Special 자원을 공유한다. 순차·1/4 worker의 native D3D12,
DXIL/SPIR-V artifact, 독립 적분·평면 ray·rough convolution·HDR와 소유권 검증은
연결 문서의 재현 gate와 실행 증거가 소유한다.

화면에 보이는 불투명 배경과 단일 closest transmitting surface의 유한한 근사다.
닫힌 유리의 두 번째 경계·다중 투과 layer·숨은 geometry·caustics·Blender rendered parity나
dense Scene 성능 수용을 완료로 세지 않는다. MAT-7은 계속 progress이며 완료 공수는 추가하지 않는다.

**MAT-7 종료 범위는 세 묶음으로 고정한다.**

1. 제품 render path: LX shadow caster/Decal 연결 완료. 아래의 shadow/Decal·균질 Volume 검증 범위를 사용한다.
2. 자동 cook/package·Player: graph 폐포 수집·Scene host/compiler artifact의 자동 검증과 배포 실행.
3. 제품 통합 검증: 실제 Editor Scene/Game·교체/재개방·수명과 native Vulkan 전체 Scene 합성.

초기 CS 전체 비동기화·전역 cache 최적화·환경 MIS/수렴·named UV 확대를 MAT-7 필수 잔여로
추가하지 않는다. 최종 성능/근사/Blender parity는 MAT-9, artist 표시·preview는 MAT-8,
LX canvas/HTTP는 LX-3/LX-3H가 소유한다. MAT-7은 계속 progress이며 완료 공수는 추가하지 않는다.

### MAT-7 실제 Scene Volume transport와 렌더 설정 명칭 (2026-09-29)

[MaterialGraphSceneVolume.md](../design/MaterialGraphSceneVolume.md)에 `Output.Volume`의
실제 Scene 연결을 추가했다. 정적 닫힌 메시의 경계와 균질 매질 계수를 준비하고,
near plane부터 최종 Scene 깊이까지 흡수·발광·직접광/환경광 단일 산란을 HDR에 합성한다.
Volume-only는 GBuffer·불투명 깊이를 기록하지 않는다. Surface+Volume은 굴절 ray의 내부
구간을 적분하며 앞면 반사를 유지하고 카메라 내부 매질의 중복 적분을 막는다.
UV seam·음수/비균일 world transform·카메라 내부·중첩 매질을 처리한다.
context에 의존하는 Volume과 열린/비다양체/퇴화/skin 경계, 16 객체·128 triangle 및
프레임 예산 초과는 잘라서 렌더하지 않고 accepted frame을 보존하며 거부한다.

stage별 reflection은 재질 binding의 합집합을 사용한다. 미사용 resource 제거는 허용하지만
같은 binding의 layout 충돌과 원래 typed 계약의 누락은 거부한다. 실제 실행에서 발견한
환경맵의 다음 프레임 상태 불일치는 Volume pass 뒤 원래 PixelShaderResource 상태로 복원했다.
574개 source SHA-256 변경 0개로 VS18/v145 Debug/Release 빌드·native D3D12 Volume
각 33 graph·8,448픽셀·35,026개 검사·57,840 GPU 성분·budget/graph reset 거부 66개를 통과했다.
굴절 각 185,066개·SSS 119,413개·전체 Scene 21,079,762개 검사도 통과했고 모든 실행의
WARNING 이상 GPU validation은 0건이다. 자세한 실행 증거는 연결 문서가 소유한다.

기존 Scene 렌더 설정은 `SceneRenderProfile`·`SceneRenderProfileComponent`·`.renderprofile`로
변경했다. 적용 요청은 `RequestRenderProfileApply()`다. 이전 `.volume`·직렬화된 컴포넌트
이름·필드의 호환 읽기를 제공하지 않는다. 기존 7개 자산의 내용, GUID와 컴포넌트 UUID는 유지했다.

이종 매질/VDB·skinned 경계·다중 산란·여러 굴절 경계 및 legacy alpha transparency의
깊이 순서를 구현한 것으로 세지 않는다. 기존 전역 fog는 별도 후처리 계약이다.
최종 시간/메모리·구적법 수렴·Blender rendered parity는 MAT-9다. MAT-7의 위 세 묶음과
progress 상태를 유지하며 완료 공수를 추가하지 않는다.

### MAT-7 실제 Scene shadow caster·Decal (2026-09-29)

[MaterialGraphSceneShadowDecal.md](../design/MaterialGraphSceneShadowDecal.md)에 실행 계약을 고정했다.
기존 shadow pass는 graph instance를 제외하며 SceneHost가 선택한 ready/마지막 정상 재질의
Alpha를 전용 VS/PS에서 평가한다. 현재 world·skin pose·chunk geometry를 GBuffer와 공유하고
세 캐스케이드의 기존 깊이를 보존한다. 순수 Volume을 불투명 caster로 처리하지 않는다.

Decal의 기존 snapshot을 공유하여 추가 baseline 복사 없이 변경된 RGB·AO·roughness·metallic·
world normal만 raw MaterialInputs에 반영한다. derived Principled lobe와 lookup이 같은 입력을
사용하며 untouched 채널의 full precision·graph Alpha·독립 layer 입력을 보존한다.
Decal의 owner bitmask로 LX AO/R/M과 legacy M/R/AO를 구분한다. 기존 diffuse alpha 제곱과
normal alpha 0 블렌드 의미는 유지한다. opaque Decal을 transmission의 별도 단계에 적용하지 않는다.

576개 source SHA-256 변경 0개로 VS18/v145 Debug/Release 빌드와 native D3D12 회귀가 통과했다.
각 Shadow 42 frame·Decal 114 frame의 누적 295,799개 검사·290,898 GPU 성분과
잘못된 Decal 선언 거부 72회를 확인했다. Volume 33·굴절 24·SSS 12 frame과 기존 전체
21,079,762개 검사·3,899,966 GPU 성분·Scene 합성 56 graph·generation 12 frame도 유지됐고
WARNING 이상 GPU validation은 0건이다. 상세 검증 결과는 연결 문서가 소유한다.
native Vulkan도 source 569개 변경 0개·그림자를 포함한 Ready 응답 18개/native worker 15회·
768픽셀 baseline draw·validation 0건으로 Debug/Release 준비 회귀를 통과했다.
CreatorEditor x64 Debug 전체 빌드도 통과했다. 실제 Editor 조작·native Vulkan 전체 Scene은 남아 있다.
종료 범위 첫 묶음의 native 렌더 경로를 연결했으며,
**당시 남은 MAT-7 필수 작업은 자동 Scene host cook/package·Player와 실제 Editor Scene/Game·
교체/재개방·수명 및 native Vulkan 전체 Scene 통합 검증 두 묶음이다.** status는 progress,
완료 공수는 추가하지 않는다. 전체 성능·근사·Blender parity는 기존 MAT-9 범위다.

### MAT-7 자동 Scene cook/package·Player (2026-09-29)

[MaterialGraphSceneCook.md](../design/MaterialGraphSceneCook.md)에 공용 Scene compiler와
complete DXIL/SPIR-V stage 집합, typed instance 의존성·자동 수집·패키징 계약을 고정했다.
Editor 준비와 AssetCooker가 같은 host/include/permutation/compiler 검증을 사용한다.
Player는 cooked bytecode로 native PSO를 준비하며 Scene 재질 Slang job을 제출하지 않는다.
MeshRenderer의 inline LX 문서 post-load 누락과 BuildTool의 Library 캐시 패키지 유입도 수정했다.

588 source 변경 0개로 Debug/Release 자동 쿠킹·동일 경로 반복 바이트 일치·6종 실패 거부와
accepted 해시 보존을 검증했다. encrypted PAK의 native D3D12 cooked Scene 각 24 frame·
57,185개 검사·10,746 GPU 성분·Scene compile 0회·GPU validation 0건을 확인했다.
기존 authored 전체 Scene·Shadow/Decal·Volume·굴절·SSS 회귀와 BuildTool 46개 검사도 통과했다.

Player·CreatorEditor Debug/Release 전체 빌드 후 독립 프로젝트를 실제 패키징했다.
각 program 2개·재질 1개·cooked Scene 1개와 CEMF 16 entry/110 identity를 Player가 읽었고,
실제 inline LX 재질 ready·화면 게시 24회·Scene compile 0회·텍스트 파서 호출 0회를 확인했다.
각 실행에서 615 source 변경 0개와 runtime entry/PAK 해시 보존을 검사했다.
Release 최초 시도의 긴 검증 경로 error 206은 짧은 격리 경로로 재실행했다.
정확한 source snapshot과 실행 증거 및 경로 제한은 연결 문서가 소유한다.

### MAT-7 제품 통합 종료 결과 (2026-09-29)

[MaterialGraphProductIntegration.md](../design/MaterialGraphProductIntegration.md)에
마지막 실제 Editor·native Vulkan 검증을 연결했다. full CreatorEditor Debug/Release에서
각 1,430개 검사·11개 최종 capture로 재질 변경·Undo/Redo·거부/reload 실패 복구·저장 후
재개방·Play/Stop·삭제/Undo와 owner·픽셀 복구를 확인했다. GPU validation 오류 0개,
1,225개 source 변경 0개, 정상 종료 코드 0이다. HTTP `material.graph`와 live capture/fence를 사용했다.

Vulkan Debug/Release의 6개 실행 경로가 모두 통과했다. Core/Layered Scene 56 frame
(generation 12 포함), Shadow 42·Decal 114·SSS 12·Refraction 24·Volume 33·encrypted
cooked Scene 24 frame을 실제 GPU readback으로 검사했다. 573개 source 변경 0개,
device 종료까지 validation·encoder drop 0개, cooked Scene compile 0회다.
cube/array upload, register별 descriptor 선언, 큰 viewport descriptor pool paging,
GBuffer의 Decal 이전 평가 경로를 수정했다. 마지막 shader 수정 후 DX12 자동 cook 반복
바이트 일치·6종 거부/accepted 해시 보존도 Debug/Release에서 재확인했다(588 source 변경 0개).

**MAT-7의 고정한 세 종료 묶음을 모두 완료하여 status는 done, MAT 완료 공수는 29/34일이다.**
MAT-8 artist preview·cost badge·fallback 설명과 MAT-9 rendered parity·성능 수용은 남는다.
artist canvas의 마우스 편집을 이번 HTTP 재질 검증의 완료로 세지 않는다.

### MAT-8 모델 graph SoT·Inspector (2026-09-29)

모델 PBR 데이터로 최초 `.shadergraph`를 만들고 배치와 Scene 재개방부터 graph 재질을 사용한다.
Base/Normal/ORM/AO/Emission, factor×texture, alpha 정책, sampler와 UV0 transform을 연결한다.
재import는 편집한 source graph를 보존하고 모델 저작 실패 시 신규 graph/meta 게시를 회수한다.
모델 material·mesh의 cook 의존성은 graph와 embedded texture로 이어진다.

MeshRenderer Inspector는 graph source와 연결 texture thumbnail·노출 parameter를 표시하며,
색상·수치 편집은 instance override로 Undo/Redo 및 Scene 저장에 포함한다. 기존 C# Material API도
같은 identifier를 사용한다. source 기본값은 parameter output socket으로 편집할 수 있고 group 안에서도 유지한다.
상세 계약과 증거는 [MaterialNodeEditor.md](../design/MaterialNodeEditor.md)를 따른다.

Debug native Editor에서 실제 CreatorRobot 배치, Inspector/canvas frame, graph 저장 왕복,
named property 변경과 Undo, 재import 보존, Scene 재개방·실제 렌더 capture를 확인했다.
최초 모델 회귀의 실제 렌더 대상은 Plane이었다. 2026-09-30 Robot 렌더 정지를 재현하여
미사용 본 인덱스 `255`와 탄젠트 없는 정점의 geometry 거부를 수정했다. 후속 native Debug
검증은 Robot의 4개 graph draw·배치 후 프레임 증가·저장한 Scene의 새 Editor 재개방을
실제 capture로 확인했으며 GPU validation·nonfinite 0, 정상 종료였다.
모델 2개·material graph program 5개·embedded texture 13개·Scene 1개를 자동 cook하여
게시 전 재읽기와 의존성 closure를 통과했다(`unproducedGuidRefs=0`).
MAT-8은 done이다. 독립 sphere preview와 Apply/import 실패 안내를 연결하고 Debug 제품 회귀 및 실제 UI를 확인했다.
MAT-9 rendered parity·성능 수용은 남는다.
현재 자동 모델 graph 생성은 UV0만 지원한다.

### MAT-8 artist 상태 표시 완료 (2026-09-30)

MeshRenderer Inspector는 실제 적용된 graph generation의 tier·route·texture sample 수·texture 수와
투명 중첩 비용을 표시한다. 노드 창은 현재 초안의 지원 여부와 예상 route를 진단하고,
미지원 노드·소켓·named UV 등에 수정 방향을 표시한다. pan/zoom도 document revision을 바꾸므로
초안 분석은 조작 종료 후 갱신하여 매 프레임 Slang 생성을 피한다. 저작 오류가 발생해도
Scene의 마지막 적용 재질은 유지한다.

Material Preview는 Scene/Game과 별도 live view의 내장 UV sphere를 사용한다. 적용된 Material의
immutable instance·generation을 Scene과 동일한 compiler/binding 경로로 전달한다. 독립 key light와
Scene 환경 조명을 사용하며 Scene object·decal·UI를 수집하지 않는다. GPU 완료 및 요청 instance와
일치하는 결과만 표시하고, 적용 재질 변경/Refresh/크기 변경 시 다시 그린다. 변경이 없으면
완료된 이미지를 유지한다. 초안은 Save/Apply 전까지 preview를 바꾸지 않는다. Transparent coverage는
아직 Scene graph replacement가 없으므로 opaque로 바꾸어 보여 주지 않고 지원 한계를 표시한다.
Apply/import 실패는 마지막 적용 재질과 열린 문서를 유지하고 수정 안내를 표시한다.
Debug 전체 빌드와 native DX12 제품 회귀 1,337개 검사·최종 capture 5회·source drift 0·정상 종료를 확인했다. 독립 구체의 실제 draw/픽셀 변경·완료 이미지 재사용·숨김 후 재게시·거부된 Apply의 정상 generation 보존을 포함한다. 실제 Windows 조작으로 File Load Scene→Ground Inspector→Material Node Editor 진입과 preview 열기/Refresh를 확인했다. 메뉴의 presentation-thread 직접 씬 로드 크래시는 소유 스레드 명령 큐로 수정하고 재검증했다. 작은 창의 설명 줄바꿈도 확인했다. MAT-8은 done, MAT 완료 공수는 32/34일이며 MAT-9 rendered parity·성능 수용은 남는다. ImGui scale 원인 분석은 사용자 요청에 따라 후속으로 미뤘다.

### MAT-8 편집 UI·캔버스 성능 후속 통합 — 2026-09-30

별도 세션의 노드 캔버스 캐시·Inspector 재질 검색/선택/에셋 추가·기본 수치 편집과
Inspector 기본 접힘 구체/격자 미리보기를 포함한다. 노드 창은 선택 노드 설명/소켓 정보를
표시하며 닫힌 뒤에도 Inspector 미리보기가 유지된다. Debug/Release 실제 실행과 각
1,341/1,339개 제품 검사·5 capture·GPU validation 0건을 확인한 소스 8개의 최종 SHA-256이
통합 전 작업 트리와 일치한다. 1302×614 Scene의 Debug 약 29.1→41.7~43.1 FPS,
canvas 26.34→9.27~9.70ms, Release 기록 중지 342.7 FPS는 당시 조건의 실측이다.
다른 해상도·창 배치와 수치를 직접 비교하지 않으며 MAT-9 최종 성능 수용을 대체하지 않는다.
별도 창 등록 감사의 MaterialGraph bodyless 1건도 기록에 남긴다.
상세 조건·원시 집계는 [MAT9NodeEditorPerformance](../analysis/MAT9NodeEditorPerformance.md)에 고정한다.

### MAT-9 모델 배치 성능 회귀 조사 — 2026-09-30

Debug 작은 Scene에서 빈 씬 7.738ms→Robot 21.345ms(약 49 rendered FPS)를 재현했다.
Raster에 IBL compute 청크 제한을 적용해 4개 메시가 16개 draw로 분할되던 경로,
매 프레임 zero LOD 배열 생성/검사와 청크별 본 행렬 복사, graph 컬링 전체 탐색,
Debug STL 컨테이너 이동과 그림자 재질 binding 중복 준비를 수정했다.
IBL compute 제한과 GPU recording/descriptor/pose owner 검증은 유지한다.
최종 Debug 같은 해상도 Robot 평균은 14.949ms, 녹화 중 실제 rendered FPS는 70.13이었다.
1612×796·ImGui 1.5에서는 Debug 5.934→13.094ms, Release 0.653→1.119ms를 측정했다.
Release도 모델 비용이 있으므로 프레젠테이션 제한에 가려진다고 수용 처리하지 않는다.
Debug/Release Editor 빌드와 GPU validation을 켠 mesh 1,117,765개·binding 179개·
full raster 21,067,374개 검사, 실제 Robot product capture와 정상 종료를 확인했다.
MAT-9는 progress다. Blender 재질별 pixel golden 수용, route parity, 이동 카메라와
tier별 반복 측정·최종 회귀 상한 판정은 남는다. 완료 공수는 32/34일을 유지한다.
실측 조건·근거·재현 도구는 [MAT9MaterialScenePerformance](../analysis/MAT9MaterialScenePerformance.md)에 기록한다.

### MAT-9 Core/Layered 동일 입력 이미지 대조 — 2026-09-30

Blender 5.1.1 Cycles 1,024 sample과 native DX12 Debug/Release의 실제 SceneHost 경로로
Core/Layered 상수 재질 10종·제어 재질 2종을 평행광/white furnace에서 대조했다(24장).
공유 triangle·normal·UV tangent·카메라·light를 고정하고 scene-linear HDR을 비교한다.
BOX reconstruction 폭과 기본 anisotropic tangent 차이를 분리했으며,
emission-only 제어 차이는 0, 박막 외 재질 relative RMS는 최대 1.941%였다.
각 native 400,495개 검사·GPU validation 0건·정상 종료와 구성 간 393,216 RGBA 성분 바이트 일치를 확인했다.
당시 RGB 3파장 박막은 평행광 18.348% / furnace 15.646%로 차이가 남았다.
독립 시드 0/11 재렌더의 박막 변동은 0.00636% / 0.22359%로 훨씬 작았다.
Core rough 평행광의 1.941% 차이도 시드 변동 0.00132%만으로 설명되지 않는다.
기존 650/550/450nm 근사를 rendered parity 통과로 처리하지 않는다.
추가 기준은 `Tools/blender/fixtures/material-matched-5.1.1`에 고정하고,
원본 area-light/HDRI fixture는 보존했다. 입력·geometry·pixel identity 검사와
재현 runner를 추가했다. Special·texture/normal-map·original grid 대조,
재질별 근사 수용·route/성능 gate는 동일 MAT-9 잔여에 유지한다.
MAT-9 progress·완료 공수 32/34일은 유지한다.
상세 조건·한계·수치는 [MAT9BlenderImageComparison](../analysis/MAT9BlenderImageComparison.md)이 소유한다.

### MAT-9 가시광 박막·환경 쿠킹 후속 — 2026-09-30

사용자 요청에 따라 RGB 3파장을 linear Rec.709 Fourier LUT 512×6·3차 Airy·금속 F82로
교체했다. dielectric diffuse attenuation과 substrate Fss 보상을 함께 수정했다.
동일 고정 이미지의 박막 RMS는 평행광 18.348→2.835%, furnace 15.646→1.352%다.
Debug/Release 각 24장·400,495개 검사·GPU validation 0건과 이미지 바이트 일치를 확인했다.
Layered 216,776개·Special 202,049개 검사 및 역사적 non-film 수치 보존,
최신 IBL bake/소비 Debug/Release 각 19,351개 검사를 통과했다.
직접 파장 적분 2,646성분에서는 LUT/3차 최대 절대 차이 0.002072,
grazing 조건의 3차/infinite Airy 차이 0.124655를 측정하여 한계로 남긴다.
Release 64×64 GPU probe에서 박막 warm median은 평행광 +0.054864ms,
furnace +0.102288ms였고 최초 lookup 준비 비용도 늘었다. 이 값으로 실제 Scene FPS를 수용하지 않는다.

사용자가 선택한 기본 forest 환경은 CC0 원본의 쿠킹 네 맵·모든 mip를 Resources에 보관하고
bootstrap에서 CPU preload 후 첫 device frame에 올린다. EXR는 runtime에 배포하지 않는다.
기존 HDRI 생성도 원본/recipe SHA cache key와 fence 완료 readback·비동기 원자적 저장을
사용하도록 연결했다. Editor cache 경로는 Saved/Editor/Cache/Environment다.
DX12 Debug/Release·Vulkan Release 실제 isolated Editor에서 기본 load·HDR cold 저장·warm hit·
손상 거부와 이전 환경 유지·정상 종료를 확인했다. 네 맵 GPU roundtrip은 exact였다.
Editor Debug/Release 및 C# BuildTool 빌드를 통과했고 Editor/Player resource 배치와 game
packaging 복사를 연결했다. 새 Player 패키지 실행은 이번 묶음에서 검증하지 않았다.

MAT-9 progress·32/34일을 유지한다. 잔여는 재질별 rendered 오차 수용, Special·texture/normal-map·
원래 grid 대조, Deferred/Forward parity와 이동 카메라·tier별 성능 상한 판정이다.
상세 수치·재현·라이선스는 [MAT9ThinFilmAndEnvironment](../analysis/MAT9ThinFilmAndEnvironment.md)가 소유한다.

### MAT-9 박막 확대 수용 판정 — 2026-10-01

고정 film/off 8쌍을 방향광/white furnace로 대조한 36장과 Blender seed 0/11을 추가했다.
수정 전/후 native Release DX12 각각 597,673개 검사·GPU validation 0건·정상 종료다.
0.1nm cutoff 및 1nm 미만의 IOR/반사색 이중 보간을 수정하고 SceneHost identity 3으로
이전 artifact를 무효화했다. Layered 216,776개 검사와 기존 non-film 수치, 경계 no-film
224성분을 검증했다. 이전 spectral golden을 보존하고 경계 변경 112행만 별도 고정했다.
전체 박막 수용은 미달이다. 사전에 정한 scene-linear RMS 1%·p95 1%·max 5%·reference
noise 0.25% 목표에 박막 16조건 중 4조건만 통과했다. 거친 혼합 금속의 film/off 차이는
박막 밖의 공통 레이어 감쇠와 GGX 보상도 후속 판정 대상임을 보여준다.
다음은 같은 MAT-9 정확도 범위의 공통 metal/dielectric 감쇠·GGX 보상 분리/수정과 재대조다.
Special/texture·route·실제 Scene 성능으로 앞서 진행하지 않는다. MAT-9 progress·32/34일을
유지하고 새 완료/공수 행은 추가하지 않는다.
[MAT9ThinFilmAcceptance](../analysis/MAT9ThinFilmAcceptance.md)가 수치·한계·판정을 소유한다.

### MAT-9 공통 GGX와 Core Principled 수정 — 2026-10-01

앞의 4/16은 공통 GGX 수정 전 결과다. 금속·유전체 Fss/compensation을 분리하고
Lambertian 보상을 GGX lobe의 곱셈 보상으로 교체했다. Blender의 E/Eavg·dielectric
layering LUT 5,152 float를 라이선스·SHA와 함께 고정했다. Scene Core Principled 그래프도
같은 base를 사용해 박막-off 시 기존 Core 근사로 되돌아가는 경로를 제거했다.
CPU SurfaceEvaluator·Scene packet의 계산 모델 tag를 GPU host와 맞추고 host identity 4로
이전 artifact를 무효화했다. 176/144 byte IBL ABI는 유지한다.

고정 36장 최종 Release DX12 597,673개 검사·GPU validation 0건·정상 종료다.
사전에 정한 RMS 1%·p95 1%·max 5%·reference noise 0.25% 목표에서 박막 on **16/16**과
off **16/16**이 통과했다. 박막 RMS 최대 0.2050%, 거친 혼합 금속 off sun/furnace는
35.5239/19.6764%에서 0.0427/0.1638%로 줄었다. 변경 전 광학 golden은 보존하고
GGX 버전 4 수치 baseline을 별도 추가했다. 강한 anisotropy 에너지 상한을 완화하지 않았다.

MAT-9 progress·32/34일을 유지한다. 다음은 원래 grid의 area-light/HDRI 및
Special·texture/normal-map 대조, route parity·실제 Scene 성능 수용이다.
[MAT9GgxClosureComparison](../analysis/MAT9GgxClosureComparison.md)이 상세 판정을 소유한다.

### MAT-9 HDRI 대조와 Scene 필터 수정 — 2026-10-01

고정 상수 재질 10종 × forest/autumn 및 발광·흰색 확산·거울 제어를 추가했다.
기존 MAT-0 EEVEE 텍스처/면광원 grid는 보존하고 별도 Cycles scene-linear fixture로 측정했다.
Blender 4,096 samples의 seed 0/11, source/cook SHA·좌표·밝기·geometry를 고정했다.
Native 수정 전/후 Release DX12 각각 26장·433,367개 검사·validation 0건·정상 종료다.
Scene field용 point sampler로 HDR cube도 읽던 부분을 linear IBL sampler로 수정하고
SceneHost identity 5로 이전 artifact를 무효화했다. 같은 target에서 재질 1/20·제어 3/6이며
HDRI 전체 수용은 미달이다. 거울 RMS는 forest 8.2410→2.6332%, autumn 0.7838→0.5034%다.

기준 노이즈 목표를 넘는 4,096-sample 조건은 수렴 확인이 필요하다. 가장 큰 autumn 박막은
131,072-sample seed 0/11의 차이 0.1587%에 비해 제품 RMS 84.1182%다. 별도 shader snapshot에서
환경 적분만 1,024→32,768로 늘리면 13.7322%로 줄어 샘플링의 기여를 실측했다.
이는 진단이며 product 기본 샘플 수는 유지한다. 큰 차이를 reference noise로 처리하지 않는다.
다음은 같은 MAT-9 내 environment/BRDF MIS와 diffuse/source/cube 오차 개선·재수용이다.
Product 기본 샘플 수를 올리는 방법을 성능 판정 없이 채택하지 않는다.
면광원은 현재 EnhancedLight/Scene consumer의 directional/point/spot 계약 밖이다.
원래 disk area를 point light로 대체해 grid 통과로 세지 않으며 extent/단위/방향/PDF와
직접광 lobe 소비·수렴 기준 구현을 미충족 조명 gate로 명시한다. 새 행/공수는 추가하지 않는다.
MAT-9 progress·32/34일은 유지한다. 상세 수치·재현·남은 수용 범위는
[MAT9HdriImageComparison](../analysis/MAT9HdriImageComparison.md)을 따른다.

### 2026-10-01 HDRI / BRDF MIS 연결

Scene GGX base/coat와 Sheen에 환경 1,024 + BRDF 1,024 balance MIS를 적용했다.
원본 cube mip 0의 선형 radiance를 소비하고, 기존 prepared albedo의 정규화 계약과
거칠기 0 조회를 보존한다. SceneHost identity 6 및 중요도 맵 owner/generation·상태 전이를 연결했다.
정확히 0인 반사 에너지는 convolution을 건너뛰며 제어 이미지의 변경은 없다.

CEIBL002의 7-map 쿠킹·캐시·bootstrap을 연결하고 forest 배포 cook을 갱신했다.
forest/autumn GPU 전체 roundtrip와 기존 4-map byte equality, CDF/PDF 독립 검증을 통과했다.
전체 26장 native 검증 오류 0; 같은 target은 재질 3/20·제어 3/6으로 아직 미달이다.
최종 v2 cook dense 박막 RMS는 84.1182→3.6083%로 줄었으나 1% 목표를 넘는다.
반사 재계산 비용도 남으므로 실제 모델 FPS 수용으로 세지 않는다.

다음은 source/cube·diffuse irradiance/forest 거울 오차 개선 후 수렴한 HDRI 재수용,
기존 special·texture/normal-map·route parity·성능 순서다. MAT-9 progress·32/34일 유지.
상세 측정·쿠킹 identity·비용·검증 범위는 [MAT9HdriMis](../analysis/MAT9HdriMis.md)을 따른다.

### 2026-10-01 HDR 에너지 보존 / source-cube 대조

autumn HDR 태양(최대 123904)이 half-float cube/sanitation에서 64000으로 잘려
확산 적분이 원본과 5.5749% 달라지는 원인을 분리했다. Cube/prefilter를 float32로
보존하고, cold 변환 시 4×4 solid-angle footprint와 CDF 셀 경계 정밀도 처리를 적용했다.
CEIBL003 7-map cook/캐시·bootstrap과 실제 형식의 Forward/Deferred/SkyBox binding,
SceneHost identity 7을 연결했다. 배포 cook은 58.26→90.26MiB다.

Blender 131072-sample seed 0/11의 고정 7조건 전/후 Native 각 121540 checks,
validation 0. 같은 RMS/p95/max/noise target에서 6/7 통과: autumn diffuse
5.4994→0.4107%, 박막 3.6083→0.8607%. forest mirror는 footprint/filter 영향으로
2.6413→3.1285%로 증가했으며 미달 상태다. 부분 집합으로 전체 MAT-9를 닫지 않는다.
다음은 mirror source 보존·재구성과 normal/view/filter 기여도 분리, 전체 HDRI 수렴/수용,
기존 special·texture/normal-map·route parity·실제 모델 성능 순서다.
MAT-9 progress·32/34일 유지. [MAT9HdriRange](../analysis/MAT9HdriRange.md)를 따른다.

### 2026-10-01 forest mirror source·픽셀 중심 보간

원본 source와 Native normal/view에서의 직접 조회 RMS 1.2801%, 공유 triangle의
픽셀 중심 ray에서는 0.4713%로 기여도를 분리했다. CEIBL004에 원본 linear RGBA32F
2D source를 보존하고, 거칠기 0 반사의 wrap-U/clamp-V float bilinear 조회 및
Scene fragment의 같은 triangle plane 픽셀 중심 보간을 적용했다. SceneHost identity 8.
기존 7개 lighting/CDF 맵은 v3와 byte 일치하고 8-map GPU roundtrip·CDF/PDF 검증 통과.
배포 cook 90.26→98.26MiB, 원본 EXR은 bootstrap 의존성이 아니다.

기존 Blender 131072-sample seed 0/11, 동일 7조건과 target에서 7/7 통과.
forest mirror RMS 3.1285→0.4693%, autumn mirror 0.5531→0.3275%, 박막 0.8631%.
Native 121542 checks·validation 0. 기존 방향광/균일 환경 24장도 같은 target 24/24,
상수 재질 20조건 RMS 최대 0.1792%. Debug Editor 전체 빌드, Release raster 21067374 checks,
실제 Editor HTTP 11 checks·v4 bootstrap/캐시 영속화·손상 거부를 확인했다.
부분 집합으로 전체 MAT-9를 닫지 않는다. 다음은 전체 HDRI 26조건의 수렴/수용,
기존 special·texture/normal-map·route parity·실제 모델 성능 순서다.
MAT-9 progress·32/34일 유지. [MAT9MirrorSampling](../analysis/MAT9MirrorSampling.md)를 따른다.

### 2026-10-01 전체 HDRI 26조건 수렴·수용

고정 26조건 전체를 Blender 5.1.1 Cycles CPU 131072 samples, seed 0/11로 새로 렌더했다.
같은 입력·geometry·source·strength·공통 mask·target에서 forest 이방성 1.0659%,
autumn 금속 2.3418%, autumn 이방성 1.6345%의 이전 미달을 확인했다.
CEIBL005에 기존 1024 및 reflection 4096 bank를 쿠킹·캐싱·bootstrap으로 연결하고,
GGX1024/environment4096 unequal-count MIS와 retained source radiance 조회를 적용했다.
현재 제품의 별도 capture는 재질20/20·제어6/6, 기존 target 전체 통과다.
RMS/p95/max 최대 0.4959/0.4681/3.5924%, seed 최대0.1717%.
Native433369 checks·validation0·정상 종료, 8-map exact GPU roundtrip, Debug 전체 빌드 및
실제 Editor HTTP11 checks·v5 bootstrap/캐시/손상 거부를 통과했다. 배포98.38MiB(+128KiB).
기존 방향광·균일 환경 24조건 회귀도 target 24/24·Native400520 checks·validation0·정상 종료,
RMS/p95/max 최대0.1792/0.1958/0.4487%로 통과했다.
v4 대비 source·cube·irradiance·BRDF·CDF/base bank는 byte 일치하며 prefilter mip4의 작은 float
차이는 별도 delta에 기록했다. 추가 반사 샘플 비용의 실제 모델 성능 수용은 아직 남아 있다.
Special transport·texture/normal-map·route parity·cold/warm/이동 카메라/tier별 성능·원래
area-light ABI/consumer 게이트는 기존 범위로 남긴다. 상수 두 HDRI 범위로 전체 MAT-9를 닫지 않는다.
MAT-9 progress·32/34일 유지. [MAT9HdriConvergence](../analysis/MAT9HdriConvergence.md)를 따른다.

### 2026-10-01 Special 대조·균질 단일 산란 Volume 검증

지원 경계 안의 공유 smooth 80-triangle closed icosphere로 Special 24조건을 먼저 대조했다.
Release Native394222 checks·validation0·정상 종료. 초기 v1 기준은 Volume bounce/내부 감쇠가
단일 산란 엔진과 달라 전체 수용에서 제외한다. 숫자상 11/24 통과를 완료로 세지 않는다.
투과 방향광 RMS33.9166%·glass34.7308%, SSS6.2429%를 기록했고 일부 기준 seed도 미수렴이다.
SSS Scale=0/off는 제품에서 bit exact. flat 네 조건 진단은 diffuse geometry 기여를 보여주지만
glass 미달이 남아 smooth 결과를 대체하지 않는다.

비교 도구의 최종 Volume 합성을 연결하고 reference v2를 RNA volume_bounces=0·Volume 내부
감쇠로 고정했다. Blender131072 samples·seed0/11의 새 Volume 8조건 전체가 기존 target 통과.
RMS/p95/max 최대0.100973/0.134198/0.193067%, noise최대0.054315%.
Release Native131590 checks·validation0·정상 종료, 독립96/192점 적분과 제품 RMS0.051739%.
이번 묶음은 제품 Engine/Slang 구현 변경이 아닌 기준·측정 경로 수정과 제품 검증이다.
실제 Scene Blended queue 거부도 검사했으며 alpha+transmission 완료 기준은 남는다.

다음 기존 순서는 투과 출입 경계/SSS 품질·reference 수렴 → texture/factor/normal-map →
Deferred/Forward 교차 비교·실제 모델 이동 카메라/tier/cold-warm 성능 → 원래 area-light 소비 경로다.
큰/skinned/heterogeneous Volume나 multiple scattering 검증으로 확대하지 않는다.
MAT-9 progress·32/34일 유지. [MAT9SpecialTransportComparison](../analysis/MAT9SpecialTransportComparison.md)를 따른다.


### 2026-10-01 Special Surface 반경·스침각 수정과 기준 수렴

Scene artist SSS Radius×Scale을 1/(4π)로 환산하고 공용 물리 profile/numeric golden은 유지했다.
Glass 준비 적분을 공용 single-interface budget과 일치시켰다. Scene의 스침각/반대 shading normal을
standalone bake의 N·V 조건으로 잘못 거부하여 가장자리에 오류색을 내던 SSS/굴절 준비도 수정했다.
finite/tier/nonzero view 검사는 유지하며 standalone의 front-hemisphere 계약은 보존한다.
SceneHost identity는 최종 10이다. Scale=0/off 제어는 상한을 바꾸지 않고 전체 이미지로 확대했다.

Blender131072 samples·seed0/11로 기존 Surface18조건을 수렴시켰다. noise최대0.227574%로
18조건 모두 기존0.25% 상한 이내다. 동일 새 기준 A/B의 sun SSS RMS6.2598→4.1009%,
mixed sun2.0096→2.0373%로 소폭 악화, furnace SSS4.2380%는 그대로다.
Glass budget 수정의 이미지 영향은 작고 폐곡면 투과 차이는 남는다. target은 7/18, 전체 미수용.

최종 Debug/Release Native각18 frames·295717 checks·validation0·정상 종료와 294912 RGBA byte exact.
Scale=0/off는 두 조명 모두 전체 이미지 byte exact, source589개 변경0, 전체 Debug Editor 빌드 통과.
반경 수정 단계의 SSS12/굴절24 frame Debug/Release와 Special202049개 검사·golden 보존도 통과했다.
기존 Volume8조건은 보존하며 다른 세대의 검증을 합산해 Special 전체 완료로 세지 않는다.
실제 GUI·Vulkan GPU·cooked Player·모델 FPS 수용으로 확대하지 않는다.

다음 기존 작업은 폐곡면 출구 경계·SSS 공간 응답·smooth grazing normal 개선 및 같은 target 재검증이다.
Alpha+transmission, texture/normal-map, route parity·실제 모델 성능, area-light 소비 gate도 그대로 남는다.
MAT-9 progress·32/34일 유지, 새 완료/공수 행을 추가하지 않는다.
[MAT9SpecialProfileCorrection](../analysis/MAT9SpecialProfileCorrection.md)를 따른다.

### 2026-10-01 근접 카메라 이동 성능 조사

사용자 Debug 캡처의 `LX.Scene.LookupBake`는 유효 278표본 가운데 20개에서
72.819..107.506ms를 기록했다. 현재 동일 화면 픽셀의 입력 11개를 bit equality로 검사하므로
카메라 이동과 texture/normal/view 변화가 화면별 IBL 적분 miss로 이어진다.
GPU 병목과 profiler timeline/scene-lock CPU 비용을 구분했다.

고정 HDRI 26조건의 diagnostic 근사는 BRDF/environment 256/4096에서 기존 이미지 상한을
26/26 만족했으나, 안정적인 전체 재질 speedup을 입증하지 못했다. 환경 bank를 1024로 줄인
두 후보는 각각 22/26이며 제품 기본값으로 채택하지 않았다.
패스별 timestamp 측정을 추가했고, 도구 변경 전후 해당 네 조건의 전체 픽셀은 byte 일치했다.
제품 shader/renderer/cook 및 MAT-9 수용 상한은 이 조사에서 변경하지 않았다.

기존 moving-camera 성능 게이트의 구현 후보는 환경 proposal의 decoded-source radiance 공유 준비 →
BRDF LUT/basis 또는 제한된 sample 근사 → depth/normal/material/환경 generation을 검증하는
IBL reprojection/refinement 순서다. 예상 speedup을 완료 성과로 세지 않는다.
근접·전체 화면·grazing·texture/normal map·Special·disocclusion·정지 후 refinement와
실제 render completion FPS를 같은 품질 상한 및 Debug/Release 조건에서 검증한다.
MAT-9 progress·32/34일 유지, 새 완료/공수 행을 추가하지 않는다.
[근접 카메라 조사와 근사 실험](../analysis/MAT9NearCameraPerformance.md)을 따른다.

### 2026-10-01 후속 구조 정리와 최종 게이트 회수

제품 IBL 1024/4096과 현재 이미지 수용 상한을 유지한다. BASE-0과 RenderGraph 후속 작업은 구현·검증된 MAT-0~MAT-8 기반을 선행으로 소비하며 MAT-9 전체 완료를 기다리지 않는다. GPU Scene 상주·변경분 갱신·재질/PSO 배칭·간접 드로우와 IBL 재사용/갱신을 함께 설계하되, 설계 완료로 성능 게이트를 닫지 않는다. 실제 구현 후 같은 장면·카메라·해상도·환경·샘플 설정의 이미지 오차, Debug/Release CPU/GPU 시간, 근접 이동 끊김을 재검증한다. SSS·투과 품질, texture/normal-map·route·area-light 잔여 조건도 별도로 유지한다. MAT-9 progress·32/34일 유지, 미산정 구현을 완료 공수에 더하지 않는다.

### 2026-10-01 품질 기준선 유지·후속 페이즈 진행 결정

사용자 결정으로 제품의 **BRDF 1024 / environment 4096**, CEIBL005와 SceneHost identity 10을
현재 비교 기준선으로 유지한다. diagnostic 256/4096 후보는 제품 기본값으로 채택하지 않는다.
**PHASE 4.25와 MAT-9는 열린 상태(`progress`, 32/34일)**로 남긴다. 새 완료/공수 행은 없다.

- 후속 PHASE 4.3은 구현된 MAT-0~MAT-8의 typed graph·Principled ABI·Scene binding/cook을
  입력으로 받고 BASE-0에서 현재 픽셀·실행·수명 기준선을 고정한다. MAT-9 전체 수용을
  착수 선행으로 요구하지 않는다. 현재 미수용 결과도 기준선에 명시하며 품질 통과로 세지 않는다.
- RenderGraph·RHI 수명/동기화·필요한 시간축 계약을 먼저 정리하고, PHASE 4.8 GPU-driven
  설계에서 GPU Scene 상주·변경분 갱신·재질/PSO 묶음·간접 드로우와 IBL 재사용/갱신 배선을
  함께 결정한다. [상세 배선 설계](../design/GpuDrivenMeshletDxrWiring.md)는 제안 계약이며
  GPU-1/GPU-3 진행, GPU-9 교차 설계·구현 공수 확정은 아직 남는다.
- 성능 게이트는 **실제 후속 구현 뒤** 같은 모델·해상도·재질·HDRI·카메라 궤적·warmup에서
  Debug/Release CPU/GPU median·p95·max, 실제 render completion FPS와 입력 지연을 재판정한다.
  IBL의 이동 중 spike를 별도로 측정하고 GPU-driven만으로 사라진다고 가정하지 않는다.
  공통 Forward+ 혼합 transport는 Volume 활성/비활성 shader GPU 비용·register/VRAM,
  SSS/굴절의 픽셀당 40 byte scratch 및 cold/warm PSO 준비 시간을 함께 대조한다.
  GBV가 켜진 정확도 실행의 준비 시간과 프레임 수를 제품 loading/FPS 수용으로 세지 않는다.
- 기존 RMS≤1%·p95 normalized≤1%·max normalized≤5%·seed RMS≤0.25%를 낮추지 않는다.
  재투영/갱신 최적화는 disocclusion·camera cut·재질/HDRI 변경·정지 후 refinement도 검사한다.
- SSS/폐곡면 투과·alpha+transmission·texture/normal-map·route parity·area-light gate는
  그대로 남긴다. GPU-driven 설계나 성능 통과로 이 품질 항목을 대체하지 않는다.
  alpha가 포함된 굴절 배경의 단일 컬러/불투명 depth 근사와 강한 Volume의
  `T >= 1e-4` 역변환 오차도 같은 pre-tone HDR 기준으로 수용 또는 명시적 미지원 판정한다.

[통합 체크포인트](../analysis/MAT9IntegrationCheckpoint20261001.md),
[후속 선행 그래프](RenderPhaseRoadmap.md), [GPU 기능 계획](GpuFeaturePlanningPlan.md)을 따른다.

### 2026-10-04 살아 있는 화면의 split-sum 전환·Blender 차이 % 표기 결정

10-01 결정은 제품 IBL을 BRDF 1024 / environment 4096으로 구워 Blender 정확도를 가져오는 것이었다.
실제 사용에서 그 굽기가 프레임을 무너뜨렸다. 카메라 회전·애니메이션이면 같은 화면 좌표의
입력 bit equality가 거의 모든 픽셀에서 깨져, 사용자 Debug 캡처의 `LX.Scene.LookupBake`가
GPU 37ms(중앙값)~1.52s(최대)였고 Scene 이미지가 약 1.5초 멈췄다. 셰이더는 빌드 구성과 무관하게
`-O3`라 Release로도 줄지 않는다. 중간 단계로 넣었던 점진 정제(움직일 때 64/16/256 근사, 멈추면
1024/4096으로 수렴, `cb4e0d38`)도 근사 표본의 고정 비용 때문에 넓게 덮이면 여전히 비쌌다.

사용자 결정으로 다음과 같이 바꾼다(PR #117, `23a393cb`).

- **살아 있는 화면은 split-sum 근사만 쓴다.** 바뀐 픽셀은 해석적 DFG(EnvBRDFApprox, 기존 Fresnel
  모델의 F0/F90)와 irradiance·prefiltered cube로 한 번에 굽고, 입력이 그대로인 픽셀은 재사용한다.
  정지해도 1024/4096으로 수렴하지 않는다. 회전 캡처에서 LookupBake p99 330ms→5.5ms, 표시 이미지
  지연 163→3 입력 표본.
- **기준 적분은 검사·오프라인 경로에 남긴다.** `SceneHostBudget::lookupApproximate = false`가
  BRDF 1024 / environment 4096 경로이고, 위 MAT-9 대조 기록들의 수치는 그 경로의 결과로 보존한다.
- **Blender 대조의 판정 방식을 바꾼다.** 살아 있는 화면(근사 경로)에 대해 RMS≤1%·p95≤1%·max≤5%를
  통과 조건으로 두지 않는다. 대신 같은 fixture·입력·target으로 **근사 경로와 Blender의 차이를 재질·
  조명 조건별 %(RMS·p95·max)로 표기하고 넘어간다.** 기준 경로의 수치와 나란히 적어 근사가 얼마를
  잃는지 보이게 한다. 차이가 크다는 이유로 이 단계를 막지 않는다.
- 첫 표기(같은 소스·Release DX12·고정 target, 두 경로 동시 측정). 근사 경로 수치를 기준 경로의
  통과 결과로 옮겨 적지 않는다.

  | 묶음 | 조건 | 기준 RMS 평균 / 최대 | 근사 RMS 평균 / 최대 | 근사에서 큰 차이 |
  |---|---:|---:|---:|---|
  | 평행광·균일 환경 | 24 | 0.08 / 0.18% | 1.94 / 14.27% | 박막 14.27·이방성 13.96·금속 9.45% (평행광 12조건은 차이 0) |
  | HDRI forest·autumn | 26 | 0.30 / 0.50% | 8.97 / 60.42% | 이방성 31.15~60.42·금속 24.69~27.61·박막 15.88~21.20% |
  | 특수 부피 | 8 | 0.07 / 0.10% | 0.07 / 0.10% | 차이 없음(룩업 미사용) |
  | 특수 표면 SSS·유리 | 18 | 5.63 / 34.79% | 5.64 / 34.79% | 거의 같음(전송 경로는 자기 준비 적분). 큰 값은 10-01 부터의 폐곡면 투과 미수용 |

  환경광을 받는 거친 유전체는 1~3%대다. 큰 차이는 split-sum 이 표현하지 못하는 이방성 로브,
  해석적 DFG 에 없는 박막 간섭, HDRI 에서 단일 prefiltered 조회로 대신하는 금속 반사에서 나온다.
  특수 표면은 처음에 기준 경로도 유리 장에서 멈췄다. 대조 탐침이 `8bfd0be5` 이후의 공통 Forward+
  흐름을 선언하지 않아 알파 캐시 게시가 거부된 것으로, 탐침을 제품 렌더러 순서로 맞춰 수정했다
  (18장 모두 10-01 이미지와 해시 일치).
  조건별 수치·도구 수정·재현은 [MAT9SplitSumLiveLookupDifference](../analysis/MAT9SplitSumLiveLookupDifference.md)가 소유한다.
- SSS·투과·alpha+transmission·texture/normal-map·route parity·area-light 잔여는 이 결정과 별개로
  그대로 남는다. MAT-9 progress·32/34일 유지, 새 완료/공수 행은 없다.

</details>

## 6. 완료 기준

- `.shadergraph`에서 같은 identity의 `.shadermeta`와 `.slang`을 생성·검증·쿠킹한다. Graph 모드에서 생성 산출물을 별도로 편집하지 않는다.
- LX가 소유하는 공통 Material 실행 환경에서 Graph와 코드 입력의 속성·텍스처·PSO·실패 복구·Preview/Scene/Game/Player의 같은 재질 generation을 확인한다. ShaderMeta는 생성 계약의 표현으로 소비한다.
- 일반 alpha Blend 그래프와 코드 재질이 공통 Forward+의 light list·정렬·깊이·HDR 합성 경로를 사용한다. 물리 transmission은 별도 특성으로 검증한다.
- `.shadergraph(domain=material)` 저장→닫기→재개방 뒤 node/pin/connection/layout/Blackboard,
  default, color-space intent와 subgraph가 보존된다.
- LX-0 대응표에서 지원으로 표시한 Blender 노드의 소켓 이름·순서·기본값·표시 조건과 내부 컨트롤, 접힘·연결·그룹 조작이 독립 예제와 Editor 제품 경로에서 확인된다.
- unknown node와 schema migration 실패는 graph와 마지막 정상 compiled generation을 보존한다.
- Blender reference의 core·layered·special material grid가 기준 적분 경로(`lookupApproximate = false`)에서
  pre-tone linear HDR 허용 오차를 통과한다. 살아 있는 화면의 split-sum 근사 경로는 같은 grid의 차이를
  재질·조명 조건별 %로 표기한다(통과 조건 아님, 2026-10-04 결정).
- 같은 지원 feature의 Deferred/Forward+ route 교차 비교가 허용 오차를 통과한다.
- constant-only emission, texture-only input, mixed factor×texture, alpha와 transmission 조합을
  독립 fixture로 판정한다.
- Standard tier가 현행 Standard PBR GPU 시간·GBuffer 대역폭 회귀 상한을 넘지 않는다.
- 사용하지 않는 lobe·texture sample·permutation은 cooked shader에서 제거된다.
- variant 수와 compile/cache 크기가 상한을 넘으면 cook이 실패 원인과 graph node를 지목한다.
- artist workflow에 raw RHI handle, register, descriptor heap, RenderGraph pass 선택이 노출되지 않는다.
- PHASE 4.75의 post/shadow/probe를 끈 상태에서도 재질 golden을 독립 재현할 수 있다.

---

## 7. PHASE 4.75 인계

PHASE 4.75는 다음을 읽기 전용 입력으로 받는다.

- 공통 compiled material generation과 같은 identity의 생성 ShaderMeta/Slang.
- `PrincipledSurface` ABI와 `MaterialFeatureMask`.
- 자동 선택된 Standard/Layered/Special route.
- backend-neutral material resource table과 variant/cost metadata.

PHASE 4.75의 Pipeline Asset이나 Custom Pass는 이 계약을 소비할 수 있지만, Principled 의미,
Material Graph schema 또는 artist 비용 tier를 다시 정의하지 않는다.
