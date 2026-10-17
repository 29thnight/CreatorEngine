# PHASE 4.25 구현·검증 이력

**2026-10-02 대시보드 노트 압축·목표 재평가.**

현재 실행 상태는 [BlenderMaterialGraphPlan](../BlenderMaterialGraphPlan.md)과
[대시보드](../../RefactoringPlanDashboard.html#phase-4.25)가 소유한다.
아래 원문은 압축 직전 노트의 보존본이다. 당시 완료/잔여·샘플 설정·공수 표기는
현재 판정으로 사용하지 않는다. 이번 변경은 계획/소스 재평가이며 새 빌드·실행 검증이 아니다.

## 재평가 결과

| ID | 현재 판정 |
|---|---|
| MAT-0 | 기준 장면/EXR 고정 완료. 엔진 전체 품질 수용은 MAT-9. |
| MAT-1 | 공용 Principled 평가 ABI 완료. 공통 제품 소비 통합은 MAT-7. |
| MAT-2 | typed IR·저장 왕복·그룹 의미 완료. 전체 Blender 노드 지원을 뜻하지 않음. |
| LX-0 | Blender 대응 기준선 완료. 103개 타입의 전체 제품 지원 완료를 뜻하지 않음. |
| LX-1 | 문서/그룹/Frame·Material IR/생성·Reroute lowering·안정 진단 기반은 구현됨. annotation/전용 wire 삽입 편집 계약은 잔여. |
| LX-2 | 독립 예제 조작 게이트 완료. Editor 전체 조작은 LX-3. |
| LX-3 | Editor 창·진입·저장/Apply 기본 연결은 구현됨. 그룹/Blackboard 패널·전체 조작 마감은 잔여. |
| LX-3H | 인증 HTTP·문서 ID/revision·편집 명령은 구현됨. host lifecycle/health 마감은 잔여. |
| LX-4 | 미착수 유지. LX 횡단 후속으로 분리; 4.25 종료 조건 제외. |
| LX-5 | 미착수 유지. LX 횡단 후속으로 분리; 4.25 종료 조건 제외. |
| LX-6 | 미착수 유지. 활성 BT/Animator 소비자 이전 후 제거; 4.25 종료 조건 제외. |
| MAT-3 | Core Principled 의미 구현 완료. rendered parity는 MAT-9. |
| MAT-4 | Layered 의미와 박막 개선 구현 완료. 전체 품질/비용 수용은 MAT-9. |
| MAT-5 | SSS/투과/균질 Volume 의미 구현 완료. 전체 Special 품질 수용은 MAT-9. |
| MAT-6 | Slang·typed metadata·진단 생성 완료. ShaderMeta 생성/공통 소비 어댑터는 MAT-7 미완료. |
| MAT-7 | 기존 LX binding·수명·PSO·쿠킹 기반은 MAT-7-BASE로 보존. Graph→ShaderMeta와 공통 Forward+ 소비는 MAT-7로 재개방. |
| MAT-8 | 모델 기본 graph·Inspector·preview·비용/실패 안내 기반 완료. 공통 소비 재연결은 MAT-7. |
| MAT-9 | 진행 유지. 제한된 상수/HDRI/Volume 통과를 전체 Special/texture/route/성능 수용으로 확대하지 않음. |

## 재작성에서 제외하는 구현 기반

- MAT-0~6: Blender 기준선, Principled ABI·Core/Layered/Special 의미, typed IR·왕복·Slang 생성/진단.
- MAT-7-BASE: 기존 reflection binding, immutable generation/instance, PSO 교체·owner fence, LXMC/CEMF/PAK 쿠킹/캐시 기반. 기존 MAT-7의 3인일을 여기로 이동하며 중복 계상하지 않는다.
- MAT-8: 모델 기본 graph, graph 기반 Inspector/override, preview·비용/실패 안내.
- LX: registry·타입·ID·문서 명령·Frame/그룹/중첩, Editor/HTTP 기본 연결, 독립 예제 조작.
- 기존 ShaderMeta/Slang compiler·reflection·Material 로딩/packing·PSO/쿠킹 기반은 어댑터의 소비 대상으로 재사용한다.

공통 Graph→ShaderMeta+Slang→Material 소비와 일반 alpha Blend Forward+ 연결은 MAT-7의 열린 목표다.
annotation/wire 삽입 편집, 제품 group/Blackboard, HTTP host 수명은 해당 LX 행에 남는다.

## 이전 대시보드 노트 원문

<details>
<summary>MAT-0 · Blender 5.1.1 reference·pre-tone HDR golden · 이전 상태 done</summary>

**재평가:** 기준 장면/EXR 고정 완료. 엔진 전체 품질 수용은 MAT-9.

**압축 직전 노트:**

2026-09-28 Blender 5.1.1의 15개 core/layered/special 사례와 scene-linear RGBA32F EXR·장면·manifest 고정. 독립 재렌더 전체 RGBA 픽셀 최대 차이 0.0. AgX PNG는 표시 전용. 제품 일치 판정은 MAT-9; shadow·AO·probe·post는 제외.

근거: BlenderMaterialGraphPlan MAT-0 · Tools/blender/fixtures/material-reference-5.1.1

</details>

<details>
<summary>MAT-1 · PrincipledSurface·MaterialFeatureMask 공용 Slang ABI · 이전 상태 done</summary>

**재평가:** 공용 Principled 평가 ABI 완료. 공통 제품 소비 통합은 MAT-7.

**압축 직전 노트:**

2026-09-28 MaterialInputs→EvaluateMaterial→PrincipledSurface 공용 ABI와 정적 feature mask 연결. GBuffer/Forward 평가·Deferred 복원·공용 직접광/IBL 소비. 고정 Slang 2026.14 DXIL/SPIR-V 70개 엔트리·미지원/unknown mask 거부 4건 통과. 변경 Slang을 기존 Debug host에 지정한 GBuffer·Forward+ reference 16,384 픽셀·IBL/AO GPU readback 통과. b2/MRT 유지. Blender 의미·고급 lobe·graph codegen·자동 route·제품 golden/성능은 MAT-3~MAT-9 후속.

근거: BlenderMaterialGraphPlan MAT-1 · PrincipledMaterialAbi

</details>

<details>
<summary>MAT-2 · typed Material Graph IR·domain·round-trip · 이전 상태 done</summary>

**재평가:** typed IR·저장 왕복·그룹 의미 완료. 전체 Blender 노드 지원을 뜻하지 않음.

**압축 직전 노트:**

2026-09-28: .shadergraph schema 1·typed Blackboard/socket·색 공간·공유/중첩 group·결정적 IR·migration/unknown/backup 복구와 정상 IR 유지 구현. 독립 Debug/Release 신규 95개 검사 및 LX 전체 검사 통과. 초기 Blender 6개 정의와 Parameter/Reroute 기본형이며 전체 Blender 지원은 아니다. Editor/HTTP 연결은 LX-3/LX-3H, Slang/GPU generation은 MAT-6/MAT-7에 남는다.

근거: BlenderMaterialGraphPlan MAT-2 · MaterialGraphSchema

</details>

<details>
<summary>LX-0 · Material 노드·소켓 Blender 대응표 · 이전 상태 done</summary>

**재평가:** Blender 대응 기준선 완료. 103개 타입의 전체 제품 지원 완료를 뜻하지 않음.

**압축 직전 노트:**

Blender 5.1.1 로컬 설치본에서 생성 가능한 103개 타입의 기본 소켓·값·17개 단일 enum 변경을 추출하고 2회 SHA-256 일치 확인. Material 목표 schema·대표 graph·독립 예제/Editor 소비 경계 고정. 제품 node/compiler 지원은 미구현.

근거: LatticeAdoptionPlan LX-0 · LatticeMaterialBlenderParity · BlenderMaterialGraphPlan

</details>

<details>
<summary>LX-1 · Lattice 공통 그래프 모델·동적 핀 · 이전 상태 progress</summary>

**재평가:** 문서/그룹/Frame·Material IR/생성·Reroute lowering·안정 진단 기반은 구현됨. annotation/전용 wire 삽입 편집 계약은 잔여.

**압축 직전 노트:**

LX-0 Material 기준선 완료. 정의 registry·domain/schema, 소켓 identifier·typed value·위치/접힘/Frame/view LXLayout을 LXG 8로 왕복. 독립 LXDocument 명령·revision·dirty·Undo/Redo와 Material 예제 UI 명령 연결을 검사. Factor·Color·Texture 항목을 typed 핀 값에 결속하고 Property Editor의 bool/int/float/vector/normal/color/texture 다중 항목 수정→저장 왕복 통과. Frame 묶음 이동·삭제, DPI별 view 복원과 외부 LXS 5 스타일·기존 4형식 읽기 검증. Node Group 내부 graph·interface ID·instance와 저장 왕복, 손상 파일 거부 및 LXG 7→8 재저장 통과. 그룹 내부 graph 초안 편집과 interface 입력·출력 노출/이름/순서 수정, 인스턴스 핀 ID·외부 링크 유지, 연결된 소켓 제거 거부 및 단일 Undo 통과. Group Input/Output 경계·내부 링크, 타입별 interface 기본값·순서·인스턴스 동기화와 저장 왕복 통과. 선택 Material 노드의 그룹화 명령·경계 소켓·외부 링크/Frame 보존·단일 Undo·저장 왕복 통과. 루트 정의와 중첩 복사본의 ID 공유, 수정·새 소켓의 중첩 인스턴스 전파, Undo/Redo·저장 왕복 통과. 참조·소유 순환, 복사본 불일치, 중첩 연결 소켓 제거와 16단계 초과를 거부. Material 컴파일, reroute·annotation·안정 오류 코드는 미완료. BT/Animator 기존 자산 변환 제외.

근거: LatticeAdoptionPlan LX-1 · LatticeExample README

</details>

<details>
<summary>LX-2 · Lattice 독립 ImGui 예제 조작 검증 · 이전 상태 done</summary>

**재평가:** 독립 예제 조작 게이트 완료. Editor 전체 조작은 LX-3.

**압축 직전 노트:**

2026-09-28 독립 예제 게이트 통과. Debug/Release 빌드·자체 검사, 250노드, 2모니터 DPI/창 크기와 DX11 캡처를 확인했다. 보이는 창에서 접힘·핀 정렬·연결/타입 거부·검색/생성·복사/붙여넣기·Undo/Redo·진단 클릭 이동·외부 창 스타일·저장 후 재실행을 검증했다. Library 노드 겹침을 수정하고 좁은 창에서 연속 생성·화면 이동·저장 왕복을 확인했다. Ctrl+클릭 다중 선택과 동시 이동은 사용자가 실제 창에서 확인했다. Editor/Material 제품 경로는 LX-3과 MAT 작업이다.

근거: LatticeAdoptionPlan LX-2 · LatticeExample README

</details>

<details>
<summary>LX-3 · Lattice Material 창 Editor 연결 · 이전 상태 progress</summary>

**재평가:** Editor 창·진입·저장/Apply 기본 연결은 구현됨. 그룹/Blackboard 패널·전체 조작 마감은 잔여.

**압축 직전 노트:**

MeshRenderer 진입·등록 Editor 창·Blender형 typed 항목/section·외부 스타일·제품 .shadergraph 저장/Apply와 authoring Slang 연결 구현. 최종 DX12 Debug/Release 각 1,298검사·Scene capture 3회·source 1,132개 변경 0·GPU 문제 0으로 실제 ImGui frame/저장 왕복/재시작/GBuffer 적용 통과. 전체 마우스 제스처와 group/Blackboard 저작 패널은 잔여다. Vulkan Editor 비교는 PHASE 4.9로 이관한다. 공수 미산정.

근거: LatticeAdoptionPlan LX-3 · MaterialNodeEditor · BlenderMaterialGraphPlan MAT-2/MAT-6

</details>

<details>
<summary>LX-3H · 열린 LX 그래프의 HTTP CLI 실시간 조작 · 이전 상태 progress</summary>

**재평가:** 인증 HTTP·문서 ID/revision·편집 명령은 구현됨. host lifecycle/health 마감은 잔여.

**압축 직전 노트:**

인증된 material.editor로 열린 LXDocument 조회·typed 핀 연결/해제·값/property·Undo/Redo·저장/Apply 구현. 문서 ID와 revision을 함께 검사하고 실패 시 무변경을 보장한다. 최종 DX12 Debug/Release 전용 gate에서 창 frame·ID/revision 충돌·정확한 저장·재시작/Scene 적용 통과. 전체 host lifecycle/health 게이트와 공수는 별도.

근거: LatticeAdoptionPlan LX-3H · MaterialNodeEditor · LatticeNodeSystem

</details>

<details>
<summary>LX-4 · Animator 새 저작 창 LX 적용 · 이전 상태 todo</summary>

**재평가:** 미착수 유지. LX 횡단 후속으로 분리; 4.25 종료 조건 제외.

**압축 직전 노트:**

Material 선행 작업의 완료 조건이 아니다. 독립 예제에서는 제목 전용 FSM 스타일만 검증했다.

근거: LatticeAdoptionPlan LX-4

</details>

<details>
<summary>LX-5 · Behavior Tree 새 저작 창 LX 적용 · 이전 상태 todo</summary>

**재평가:** 미착수 유지. LX 횡단 후속으로 분리; 4.25 종료 조건 제외.

**압축 직전 노트:**

Material 선행 작업의 완료 조건이 아니다. 기존 BT 편집 창은 활성 상태로 유지한다.

근거: LatticeAdoptionPlan LX-5

</details>

<details>
<summary>LX-6 · imgui-node-editor 의존성 제거 · 이전 상태 todo</summary>

**재평가:** 미착수 유지. 활성 BT/Animator 소비자 이전 후 제거; 4.25 종료 조건 제외.

**압축 직전 노트:**

현재 Animator/BT 소비자가 남아 있다. Editor Debug/Release와 새 그래프 제품 경로를 확인한 뒤 제거한다.

근거: LatticeAdoptionPlan LX-6

</details>

<details>
<summary>MAT-3 · core Principled 의미·기본값 · 이전 상태 done</summary>

**재평가:** Core Principled 의미 구현 완료. rendered parity는 MAT-9.

**압축 직전 노트:**

2026-09-28 core 기본값·IOR/Specular Level·dielectric tint/금속 F82·geometric normal·HDR emission·별도 opacity 구현. 공용 직접광과 IOR-aware IBL 적분/소비 분리. RTX 4070 Ti 23사례×9각도 8,280개 CPU double 대조 통과, 최대 정규화 오차 0.000011920798. core DXIL/SPIR-V 2개·고정 pass 70개 컴파일 및 미지원/unknown/미배선 route 거부 10건 통과. 현재 Debug host GBuffer·Forward/reference·Water/Wind·IBL/AO 회귀 통과. 제품 비기본 IOR lookup/packing·graph binding은 MAT-6/MAT-7, Blender golden/route/성능은 MAT-9 후속.

근거: BlenderMaterialGraphPlan MAT-3 · PrincipledCoreSemantics

</details>

<details>
<summary>MAT-4 · coat·sheen·anisotropy·iridescence layered lobe · 이전 상태 done</summary>

**재평가:** Layered 의미와 박막 개선 구현 완료. 전체 품질/비용 수용은 MAT-9.

**압축 직전 노트:**

2026-09-28 Sheen→Coat→base 감쇠·LTC sheen·별도 coat normal/tint·anisotropic GGX/회전·RGB thin film 구현. 13개 Blender 기본값·고정 계수 1,024개, RTX 4070 Ti 35사례×8시선×7조합 216,776개 GPU/CPU 검사·energy bound 5,712개·numeric golden 1,960행/7,840성분 통과. 최대 정규화 오차 0.0000011920929. Layered DXIL/SPIR-V 14개·의존 mask 거부 8개와 core 8,280개·기존 pass 70개·미지원/미배선 거부 34개·현재 Debug host GPU 3종 회귀 통과. 미사용 Layered 비용은 정적 mask로 제거. EEVEE가 미사용하는 aniso/film은 Cycles 의미 기준이며 film은 3파장 근사. 제품 lookup/convolution/binding/route는 MAT-6/MAT-7, rendered parity·근사 수용·성능은 MAT-9 후속.

근거: BlenderMaterialGraphPlan MAT-4 · PrincipledLayeredSemantics

</details>

<details>
<summary>MAT-5 · transmission·subsurface·volume Special route · 이전 상태 done</summary>

**재평가:** SSS/투과/균질 Volume 의미 구현 완료. 전체 Special 품질 수용은 MAT-9.

**압축 직전 노트:**

2026-09-28 authored IOR isotropic glass·front/back TIR·GGX BTDF, RGB SSS radius/scale/IOR/anisotropy profile, Volume 흡수·산란·발광·HG phase·Beer–Lambert 합성 구현. SSS 입사 source와 destination weight 분리. Blender 기본값 12개·Volume exact 재생성, RTX 4070 Ti 35사례×6시선×7마스크 202,049개 GPU/CPU·독립성 검사·energy bound 4,410개·numeric golden 4,410행/17,640성분 통과. 최대 정규화 오차 0.0000011920929. Special DXIL/SPIR-V CS/PS 28개·route/의존성 거부 22개, 기존 core/Layered/golden·pass 70개·미배선 거부 58개·현재 Debug host GPU 3종 회귀 통과. SSS는 diffusion 근사, glass는 단일 경계, Volume은 homogeneous single scattering. Blackbody/attribute와 제품 transport/binding/codegen/자동 route는 후속. Blender rendered parity·근사 수용·성능은 MAT-9.

근거: BlenderMaterialGraphPlan MAT-5 · PrincipledSpecialSemantics

</details>

<details>
<summary>MAT-6 · material graph→deterministic Slang codegen·diagnostic · 이전 상태 done</summary>

**재평가:** Slang·typed metadata·진단 생성 완료. ShaderMeta 생성/공통 소비 어댑터는 MAT-7 미완료.

**압축 직전 노트:**

2026-09-28 초기 6개 Blender 정의·엔진 Multiply/TextureSample/Volume·공유/중첩 그룹의 Slang 생성. 전체 Principled 그룹의 disabled/implicit UV 입력 노출 수정. live parameter/resource·dead lobe·Color/Alpha 공유 sample·정적 feature와 Special 요구 조건 추출. 안정 source/cache identity·저장 순서/재개방·source/node/pin/group 진단·실제 Slang 오류와 마지막 정상 CPU artifact 복구 검증. RTX 4070 Ti 20 graph×5입력 3,600성분 GPU 대조, DXIL/SPIR-V CS/PS 93개 성공·오류/route/publication 거부 31건·source/graph 4쌍·metadata JSON·SPIR-V binding 검증. Debug/Release 기존 Material 95개·생성기 21개·LX self-test와 고정 pass 70개/거부 58건 통과. 전체 Blender node/enum·제품 ShaderMeta/자원/PSO/Scene 교체·자동 route는 후속. generated source는 읽기 전용.

근거: BlenderMaterialGraphPlan MAT-6 · MaterialSlangCodegen

</details>

<details>
<summary>MAT-7 · 제품 binding·generation/PSO 교체·자동 route·쿠킹 · 이전 상태 done</summary>

**재평가:** 기존 LX binding·수명·PSO·쿠킹 기반은 MAT-7-BASE로 보존. Graph→ShaderMeta와 공통 Forward+ 소비는 MAT-7로 재개방.

**압축 직전 노트:**

2026-09-28 RenderEngine/AssetCooker 기반·D3D12 SRGB/PSO·owner fence·LXMC/CEMF/PAK 검증. DataSystem/Material GUID generation·typed instance 저장 왕복/실패 복구 연결, runtime 43개 및 실제 DataSystem Debug/Release 각 26개 검사 통과. 제품 b2/texture/독립 sampler adapter는 실제 엔진 DX12 draw로 Debug/Release 각 168개 검사·64개 GPU 성분 통과. Core/Layered evaluated-point IBL bake와 별도 base/coat/sheen lookup 소비 draw는 Debug/Release 각 19,351개 검사·18,240개 GPU 성분 통과(최대 정규화 오차 0.00000950694, GPU validation 0건). Scene render owner는 graph instance·coverage/queue·PSO·IBL·binding을 함께 소유하고 제출 성공 확인 뒤 게시한다. 실제 두 in-flight GPU 제출의 교체·실패·abort·중간 제출·완료 해제를 검사했다(Debug/Release 각 165개 검사·GPU 16성분·DXIL/SPIR-V 10개·GPU validation 0건). 공간·시선 GPU graph 평가→point buffer bake→bounded draw는 Debug/Release 각 19,387개 검사·GPU 19,092성분·37지점·8프레임·DXIL/SPIR-V 14개·GPU validation 0건 통과. SRGB source 오차 0.001과 물리 계산 0.0001 기준을 분리했다. GPU 실패 진단·완료 readback acceptance·정확한 재사용·취소 배치 거부·owner 회수를 검사했다(MaterialGraphSurfaceBatch). 제품 Model ABI의 immutable 정점/pose 입력과 GPU skin×world→graph→IBL, GPU Scene packet의 완료 검증 후 게시를 연결했다(MaterialGraphMeshSurface). 8개 layout×3포즈·37정점의 CPU double 대조, 실제 indexed geometry와 bounded lookup draw, 실패/취소·native 할당 중 구간 전환 거부를 검사했다. 큰 메시 triangle 분할·원본 remap/예산 거부, 재질 평가 전 UV/world frame 보간과 명시적 footprint LOD를 추가했다. 8200정점·3 chunk·Core/Layered 82샘플, 서로 다른 vertex/sample 개수·edge weight·0 bitangent 거부·native sample prefix 전환을 포함해 Debug/Release 각 621,160개 검사·GPU 286,156성분·31개 완전/3개 prefix 제출·DXIL/SPIR-V 22개·GPU validation 0건 통과(최대 정규화 오차 0.0000596344). 불투명 depth/MRT에서 가시 픽셀·원근 frame·fine UV derivative를 수집하는 독립 RHI/순차 RenderGraph 패스를 추가했다(MaterialGraphRasterSurface). 한 재질 chunk 묶음·4096픽셀 범위다. Debug/Release 각 408,806개 검사·GPU 392,560성분·7프레임·DXIL/SPIR-V 22개·GPU validation 0건 통과. 가시 2,435/배경 6,269픽셀과 근평면 노출 63픽셀을 확인했고 물리 정규화 0.0001·SRGB 0.001·LOD transport 절대 0.0005 기준을 분리했다. 같은 RenderGraph의 capture/resolve·재질 평가·IBL을 준비/선언/기록으로 나누고 순차·워커 1개/4개 제출을 검증했다(MaterialGraphPassRecording). Debug/Release 각 409,684개 검사·GPU 392,560성분·추가 21 graph/35 worker list/6 실패 fixture, 모든 raster/material/IBL 결과의 baseline byte 일치·GPU validation 0건을 확인했다. 중간 prefix·abort·다른 graph/중복 선언·record 반복·callback 오류 전달·completion owner 회수를 검사했다. current-pose mesh 생산과 Core/Layered 공유 depth·정확한 opaque draw winner를 같은 graph에 추가했다(MaterialGraphSharedDepth). Coplanar·clipping·스키닝·draw 순서를 독립 RHI에서 검증했다. Debug/Release 각 776,402개 검사·GPU 652,048성분·current mesh graph 21개/공유 depth graph 24개·공유 가시 5,688/coplanar 1,422픽셀·스키닝 graph 6개·기록 실패 6개/mesh 수명 실패 4개·DXIL/SPIR-V 24개·GPU validation 0건 통과. 공유 depth bytes 보존과 graph 주소 재사용의 stale producer 거부를 확인했다. 당시 독립 RHI 검증은 실제 Scene draw pool/GBuffer·legacy/Masked depth·조명/컬러 설치 완료로 세지 않았다. SceneRuntime producer proxy/delta에 LX instance·coverage를 밀봉하고 실제 draw pool/per-view에서 geometry·world·pose·camera를 owning plan으로 복사했다(MaterialGraphSceneInput). 동일 legacy Material owner/GUID라도 LX owner 교체·제거를 delta에 반영한다. 해당 입력의 native 공유 depth graph 24개·입력 실패 24종을 포함해 Debug/Release 각 773,635개 검사·GPU 649,168성분·DXIL/SPIR-V 24개·GPU validation 0건 통과. 후속 MaterialGraphSceneHost는 실제 DX12/Vulkan Scene consumer에 LX.Scene.GBuffer와 LX.Scene.Color를 연결했다. 같은 5 MRT/D32의 legacy/LX Opaque/Masked 가림·coplanar draw winner·read-only depth·Core/Layered 직접광/IBL·HDR 컬러 합성을 검사했다. Debug/Release 각 830,436개 검사·GPU 659,914성분·실제 합성 graph 24개(순차/1워커/4워커)·가시 3,582픽셀·실패 거부 120개·GPU validation 0건 통과. 새 Scene shader는 DXIL/SPIR-V 12개를 별도 검증했다. 업로드 취소 시 삭제된 iterator를 재사용하던 DX12TextureCache 어설션을 수정하고 texture 3개의 취소/재시도를 검증했다. 초기 Scene host는 4,096픽셀·64 draw 이하의 설치 검증 범위였다. 2026-09-29 MaterialGraphSceneLookup에서 11 float4의 정확한 입력 비교·변경 픽셀 compute bake·base/coat/sheen lookup 소비·제출 recording/fence 확인 뒤 캐시 게시를 추가했다. 기본 한도는 4,096²픽셀·4,096 draw·lookup payload 2 GiB다. 카메라/world/환경 변경은 갱신하고 재질/UV 변경은 해당 픽셀만 갱신한다. Debug/Release 각 20,762,086개 검사·GPU 3,623,244성분·합성 36 graph/42,292픽셀·거부 192개·새 적분 20,491/재사용 21,801픽셀·GPU validation 0건 통과. 1920×1080 희소 coverage 2프레임과 전달 입력/독립 IBL/직접광을 분리해 검증했다. 새 Scene shader/lookup CS의 DXIL/SPIR-V 24개 artifact는 기존 compiled=24와 별도다. 후속 MaterialGraphTextureFootprints에서 각 샘플의 Vector fine derivative·이미지 크기/mip 수에 따른 독립 LOD를 구현하고 단일 텍스처 제한을 해제했다. 생성기 v2·golden 20 graph·DXIL/SPIR-V 93개·GPU 3,600성분을 확인했다. 크기/mip 수가 다른 이미지 2개·같은 이미지의 다른 Vector/sampler·fractional/마지막 mip·parameter/이미지 크기 교체의 부분 갱신을 8프레임에서 검증했다. 전체 Debug/Release 각 20,892,205개 검사·GPU 3,735,460성분·합성 44 graph/43,644픽셀·거부 240개·GPU validation 0건 통과. compute host는 explicit LOD를 유지하고 compiler v1 LXMC는 재쿠킹한다. 후속 MaterialGraphSceneGeneration은 generation/backend별 Slang worker·DX12 native PSO worker·8 PSO 전체 ready 선택을 연결했다. 실제 DX12/Vulkan consumer가 residency/prefix 전에 같은 epoch/view/Material의 마지막 정상 instance·coverage와 현재 geometry를 선택한다. exact graph batch completion·티켓 성공 뒤 게시하고 pending·실패·abort·stale 요청·다른 epoch를 보호한다. Debug/Release 각 21,079,762개 검사·GPU 3,899,966성분·합성 56 graph/45,626픽셀·거부 312개·GPU validation 0건 통과. 신규 12프레임의 이전 재질 유지 7회·abort/pending ticket/stale 게시 거부 각 1회·generation worker 6회·실패 memo 2개·native PSO worker 24회를 확인했다. 같은 ready generation의 numeric 교체는 재컴파일하지 않는다. 후속 Vulkan native PSO worker는 owned descriptor·64개 admission·실패 memo·invalidation 시 abandoned 완료 회수·shutdown join을 연결했다. SPIR-V의 실제 stage entry 이름을 사용해 생성 Scene shader 설치 실패를 수정했다. 554개 source SHA-256을 고정한 Debug/Release 빌드·RTX 4070 Ti native Vulkan 준비 검증에서 Core/Layered 16 Ready 응답·14 native worker(동일 GBuffer 2개 공유), 주 회귀 83 accepted/worker·stale 65개 회수와 worker PSO 768픽셀/3072성분 draw/readback을 확인했다. 기존/생성 compute entry 설치·잘못된 stage 거부·device 종료까지 validation 0건을 검사했다. 상세 검증은 MaterialGraphSceneGeneration §6. 후속 MaterialGraphSceneSubsurface는 SSS Special Forward·10 PSO·source/profile 7 MRT·독립 금속/유전체 반사·draw owner mask·world-distance dipole gather·HDR 합성을 연결했다. 0 Radius 채널/0 Scale은 local source로 돌아가며 SSAO와 transported source를 분리한다. 기본 SSS payload 예산은 프레임당 512 MiB(224 B/픽셀)다. 562개 source SHA-256 변경 0개로 Debug/Release 빌드·native D3D12 SSS 각 12 graph·가시 4503픽셀·Masked 알파 구멍 648개·119414개 검사·108072 GPU 성분·validation 0건 통과. 기존 전체 회귀도 각 21079762개 검사·3899966 GPU 성분·Scene 합성 56 graph·generation 12 frame 통과. 9×9 가시 표면의 유한한 dipole 근사이며 Cycles Random Walk/Blender pixel parity·dense Scene 성능 수용은 MAT-9다. 후속 MaterialGraphSceneRefraction은 투과 draw를 최초 GBuffer에서 제외하여 뒤쪽 불투명 HDR/depth를 완성·복사한 뒤, closest 투과 GBuffer·최종 lookup·glass/BTDF 적분·32 GGX 방향 굴절·Special HDR을 합성한다. IOR 1·뒤쪽 면 IOR 역수/전반사·현재 환경 fallback·Masked·금속/SSS 혼합·Tint/Thin Film을 지원하며 배경 SSAO는 투과 표면에 적용하지 않는다. 124 B/픽셀의 capture/sample/HDR·D32 복사에 refraction 프레임 예산 512 MiB를 적용하고 기존 224 B/픽셀 Special 자원을 공유한다. 같은 이전 lookup의 중복 import와 persistent state writeback도 수정했다. Debug/Release native D3D12 굴절 각 24 graph·가시 6642픽셀·교차 5646개·miss 996개·전반사 516개·Masked 구멍 378개·혼합 864픽셀·budget/graph reset 거부 48개·독립 rough convolution 78픽셀·185066개 검사·155877 GPU 성분·validation 0건 통과. 567개 source SHA-256 변경 0개로 VS18/v145 Debug/Release 빌드와 굴절·기존 SSS·전체 Scene 회귀를 완료했다. SSS는 새로 지원하는 refraction 거부 검사 1개를 제거하여 각 119413개 검사·108072 GPU 성분이며 기존 전체 21079762개 검사·3899966 GPU 성분·Scene 합성 56 graph·generation 12 frame도 유지된다. 모든 native 실행의 WARNING 이상 GPU validation은 0건이다. DXIL/SPIR-V artifact를 검증했으며 native Vulkan 전체 Scene은 아래 잔여에 유지한다. 단일 closest 표면과 현재 화면의 불투명 배경을 연결한 유한 근사이며 다중 유리 경계/숨은 geometry/caustics·최종 성능/Blender parity는 MAT-9다. 후속 MaterialGraphSceneVolume은 닫힌 정적 메시의 균질 매질을 계수 CS와 world triangle으로 준비하고 near plane부터 최종 Scene 깊이까지 흡수·발광·직접광/환경광 단일 산란을 HDR에 합성한다. Volume-only는 불투명 GBuffer/depth를 기록하지 않으며 Surface+Volume은 굴절 ray 내부에 적용하고 카메라 중복 적분을 막는다. context 의존 Volume·열린/비다양체/퇴화/skin 경계·16 객체/128 triangle·예산 초과를 거부하며 accepted frame을 보존한다. stage별 reflection은 합집합을 사용하고 공유 binding 충돌을 거부한다. 환경맵의 다음 프레임 상태 복원도 수정했다. 기존 렌더 설정은 SceneRenderProfile·SceneRenderProfileComponent·.renderprofile·RequestRenderProfileApply로 명명하고 이전 확장자/직렬화 이름의 호환 읽기는 제거했다. 7개 기존 자산 내용·GUID와 컴포넌트 UUID는 보존했다. 574개 source SHA-256 변경 0개로 VS18/v145 Debug/Release 빌드·native D3D12 Volume 각 33 graph·8448픽셀·35026개 검사·57840 GPU 성분·budget/graph reset 거부 66개·validation 0건 통과. 굴절 각 185066개·SSS 119413개·전체 21079762개 검사·Scene 합성 56 graph·generation 12 frame도 통과했고 모든 native 실행의 WARNING 이상 GPU validation은 0건이다. MAT-6 codegen 20 graph·13306개 검사·93개 컴파일·31개 거부·3600 GPU 성분도 통과했다. 상세 실행 증거는 MaterialGraphSceneVolume 문서가 소유한다. 후속 MaterialGraphSceneShadowDecal은 그래프 Alpha의 세 캐스케이드 shadow caster와 현재 skin/world/chunk geometry 공유를 연결했다. 기존 caster는 LX instance를 제외하고 순수 Volume을 불투명 caster로 처리하지 않는다. 기존 Decal snapshot을 공유하여 변경 raw 채널만 반영하고 derived Principled lobe·IBL을 재평가한다. owner bitmask로 LX AO/R/M과 legacy M/R/AO를 구분하며 기존 alpha 제곱·normal no-op 블렌드를 유지한다. 576개 source SHA-256 변경 0개로 VS18/v145 Debug/Release 빌드·native D3D12 Shadow 각 42 frame·Decal 114 frame·누적 295799개 검사/290898 GPU 성분·선언 실패 복구 72회를 통과했다. Volume 33·굴절 24·SSS 12 frame·전체 21079762개 검사·Scene 합성 56 graph·generation 12 frame도 유지됐고 WARNING 이상 GPU validation 0건이다. native Vulkan도 569개 source 변경 0개·그림자를 포함한 Ready 응답 18개/native worker 15회·768픽셀 draw·validation 0건으로 Debug/Release 준비 회귀를 통과했다. CreatorEditor x64 Debug 전체 빌드도 통과했다. 상세 증거는 MaterialGraphSceneShadowDecal 문서가 소유한다. 후속 MaterialGraphSceneCook은 Editor/AssetCooker 공용 compiler·complete DXIL/SPIR-V Scene stage·typed instance 폐포와 자동 graph 수집을 연결했다. MeshRenderer inline LX 재질 post-load 누락과 Library 캐시의 패키지 유입을 수정했다. 588개 source 변경 0개로 Debug/Release 자동 cook 반복 바이트 일치·6종 거부/accepted 해시 보존과 encrypted PAK native cooked Scene 각 24 frame·57185개 검사·10746 GPU 성분·Scene compile 0회·validation 0건을 확인했다. 기존 전체 Scene/Shadow/Decal/Volume/굴절/SSS 회귀와 BuildTool 46개 검사도 통과했다. Player/CreatorEditor Debug/Release 전체 빌드 후 독립 프로젝트의 실제 package-game·Player에서 각 program 2개/재질 1개·CEMF 16 entry/110 identity·inline LX Scene ready·화면 게시 24회·Scene compile 0회·텍스트 파서 호출 0회·runtime/PAK 해시 보존을 확인했다. 각 실행의 615 source 변경 0개이며 최초 Release의 긴 검증 경로 error 206은 짧은 경로로 재실행했다. 상세 증거/경로 제한은 MaterialGraphSceneCook 문서가 소유한다. 2026-09-29 마지막 제품 통합 묶음도 완료했다(MaterialGraphProductIntegration). full CreatorEditor Debug/Release 각 1430개 검사·11개 최종 capture·1225 source 변경 0개로 HTTP material.graph의 재질 변경/Undo/Redo·거부/reload 실패 복구·Scene 저장/재개방·Play/Stop·삭제/Undo의 실제 owner/픽셀 복구와 정상 종료를 확인했다. GPU validation layer=on/mode=gpu/problems=0/drop=0. native Vulkan Debug/Release 각 6개 경로·573 source 변경 0개로 Scene 56 frame(generation 12 포함)·Shadow 42/Decal 114·SSS 12·굴절 24·Volume 33·encrypted cooked Scene 24 frame을 통과했고 종료까지 validation/encoder drop 0개·cooked Scene compile 0회다. cube/array upload·register별 descriptor 선언·1920x1080 descriptor pool paging·GBuffer의 Decal 이전 평가 경로를 수정했다. 마지막 shader 수정 후 DX12 자동 cook도 588 source 변경 0개·반복 바이트 일치·6종 거부/accepted 해시 보존·cooked Scene 24 frame/compile 0회로 재검증했다. 고정한 MAT-7 세 종료 묶음 완료, status done, MAT 완료 공수 29/34일. artist canvas 마우스 편집을 HTTP 재질 검증 완료로 세지 않는다. 초기 CS 전체 비동기화·전역 cache 최적화·환경 MIS/수렴·named UV 확대는 별도 개선, artist 표시/preview는 MAT-8, rendered parity/성능 수용은 MAT-9, LX canvas/HTTP는 LX-3/LX-3H다.

근거: BlenderMaterialGraphPlan MAT-7 · MaterialGraphProduct.md · MaterialGraphSceneGeneration.md · MaterialGraphSceneSubsurface.md · MaterialGraphSceneRefraction.md · MaterialGraphSceneVolume.md · MaterialGraphSceneShadowDecal.md · MaterialGraphSceneCook.md · MaterialGraphProductIntegration.md

</details>

<details>
<summary>MAT-8 · artist preview·cost badge·fallback 설명 · 이전 상태 done</summary>

**재평가:** 모델 기본 graph·Inspector·preview·비용/실패 안내 기반 완료. 공통 소비 재연결은 MAT-7.

**압축 직전 노트:**

2026-09-29 모델 PBR→기본 graph SoT·재import 보존·graph 기반 Inspector와 instance Undo/Scene 저장 연결. 초기 Debug native 94개 검사는 Robot 배치와 Plane Scene capture 2회를 확인했다. 2026-09-30 Robot 렌더 정지의 미사용 본 인덱스 255·탄젠트 없음 거부를 수정. GPU mesh 621,152개 검사·native Robot 4개 graph draw·배치 및 새 Editor 재개방 후 프레임 증가·GPU validation/nonfinite 0개·정상 종료 통과. 큰 창의 Scene 1828×992에서도 4개 graph draw·프레임 증가·validation/nonfinite 0을 확인. UV0 자동 생성만 지원한다. 2026-09-30 Inspector에 적용 generation의 tier/route/sample/texture 비용과 중첩 경고, 노드 창에 초안 지원 진단·수정 방향을 추가했다. 독립 sphere preview·Apply/import 실패 복구 안내를 연결했다. Debug 전체 빌드·native DX12 1337개 검사·최종 Scene/구체 capture 5회·1134 source drift 0·정상 종료 통과. 실제 구체 draw/픽셀 변경·완료 결과 재사용·숨김 후 재게시·실패 Apply의 accepted preview 보존을 확인했다. 실제 Windows UI로 메뉴 씬 로드·Inspector→노드 창·preview 열기/Refresh를 확인하고 소유 스레드 위반 메뉴 크래시와 작은 창 설명 잘림을 수정했다. 별도 세션의 Inspector 재질 검색/선택/추가·기본 수치 편집·접힌 구체/격자 preview와 노드 창 선택 노드/소켓 설명을 통합했다. 최종 소스 8 SHA 일치·Debug/Release 1341/1339개 검사·각 5 capture·GPU validation 0건. canvas 26.34→9.27~9.70ms, 같은 1302×614 Scene의 Debug 약 29.1→41.7~43.1 FPS·Release 기록 중지 342.7 FPS를 당시 측정으로 보존했다. 별도 MaterialGraph bodyless 감사 1건은 남긴다. MAT-8 done, 완료 공수 32/34일. MAT-9 rendered parity·성능 수용은 남고 ImGui scale 원인 분석은 사용자 요청으로 후속이다. register·descriptor·MRT·PSO 선택은 artist UI에 노출하지 않는다.

근거: BlenderMaterialGraphPlan MAT-8 · MaterialNodeEditor.md · analysis/MAT9NodeEditorPerformance.md · analysis/ReflgenPhase425Integration.md

</details>

<details>
<summary>MAT-9 · Blender golden·route parity·Standard 성능 gate · 이전 상태 progress</summary>

**재평가:** 진행 유지. 제한된 상수/HDRI/Volume 통과를 전체 Special/texture/route/성능 수용으로 확대하지 않음.

**압축 직전 노트:**

2026-09-30 Debug Robot 배치 회귀를 재현하고 반복 LOD/pose 준비·4 mesh의 16 chunk 분할·graph culling/배열 할당·동일 프레임 shadow binding 중복을 수정했다. 작은 씬뷰 Debug 렌더 CPU 평균 21.345→14.949ms; 큰 씬뷰 1612×796은 Debug 13.094ms, Release 1.119ms다. Debug/Release 빌드와 raster 각 21067374개 검사·mesh 1117765개·bindings 179개 검사를 통과했다. 측정은 validation off의 실제 완료 렌더 처리량이며 녹화 비용과 화면 표시 FPS를 구분한다. 동일 triangle/normal/UV tangent·카메라·light의 Blender 5.1.1 Cycles/native DX12 Debug/Release Core/Layered 상수 10종+제어 2종을 평행광/white furnace 24장으로 대조했다. 각 native 400495개 검사·GPU validation 0건·정상 종료와 393216 RGBA 성분 바이트 일치, emission 제어 오차 0·박막 외 relative RMS 최대 1.941%다. 기존 RGB 3파장 박막은 18.348%/15.646% 차이를 보여 parity 통과로 처리하지 않았다. Blender 독립 시드 0/11 박막 변동은 0.00636%/0.22359%이며 Core rough 평행광 1.941% 차이도 시드 변동 0.00132%로 설명되지 않는다. 추가 matched fixture·실제 소비 입력/geometry identity 검사·재현 runner를 고정했다. 후속 가시광 Fourier LUT 512×6·3차 Airy·F82와 dielectric diffuse/substrate Fss 보정으로 박막 RMS는 2.835%/1.352%로 줄었다. Debug/Release 각 24장·400495개 검사·바이트 일치·GPU validation 0건, Layered 216776개·Special 202049개·IBL 각 19351개 검사와 non-film 역사적 수치 보존을 통과했다. 직접 적분 2646성분의 LUT/3차 최대 절대 차이는 0.002072, grazing 3차/infinite 차이는 0.124655다. Release 64×64 probe의 박막 warm GPU 비용은 평행광 +0.054864ms·furnace +0.102288ms로 늘었으며 실제 Scene FPS 수용으로 환산하지 않는다. 사용자 선택 forest 기본 환경은 네 맵을 쿠킹한 34.183MiB 데이터만 Resources에 보관하고 bootstrap preload·첫 device upload한다. 기존 HDR 생성도 source/recipe SHA cache·fence readback·비동기 원자적 저장을 연결했다. DX12 Debug/Release·Vulkan Release 실제 Editor의 default load·HDR cold 저장·warm hit·손상 거부/이전 렌더 유지·정상 종료와 네 맵 exact GPU roundtrip, Editor 전체 및 BuildTool 빌드를 확인했다. Player resource/packaging 복사는 구현·C# 빌드 검증이며 새 Player 패키지 실행은 이번 묶음에서 하지 않았다. 2026-10-01 film/off 8쌍·두 조명 36장과 독립 시드를 확대 대조했다. Release 수정 전/후 각 597673개 검사·GPU validation 0건. 0.1nm cutoff/최종 반사색 이중 보간을 수정하고 SceneHost identity 3·별도 경계 CPU golden 112행·no-film 제어 224성분을 고정했다. 박막 16조건 중 RMS 1%·p95 1%·max 5% 목표를 통과한 것은 4조건이며 전체 수용은 미달이다. 후속 공통 GGX 수정에서 금속·유전체 감쇠를 분리하고 Lambertian 보상을 GGX 곱셈 보상으로 교체했다. Blender E/Eavg·dielectric layering 5152 float를 라이선스/SHA와 고정했고 Scene Core Principled도 같은 base를 사용한다. CPU/GPU 계산 모델 tag·SceneHost identity 4·버전 4 golden과 기존 광학/profile 불변 성분을 검증한다. 고정 film/off 36장 597673개 검사·GPU validation 0건에서 기존 RMS 1%·p95 1%·max 5%·noise 0.25% 목표에 on 16/16·off 16/16이 통과했다. 거친 혼합 금속 off sun/furnace는 35.5239/19.6764%→0.0427/0.1638%, 박막 RMS 최대 0.2050%다. 기존 Core/Layered 24장 400495개 검사도 검증했고 상수 재질 20조건 RMS 최대 0.1999%다. Layered 216776개·Special 202049개·IBL Debug/Release 각 19351개 검사와 135개 독립 closure 불변식, Editor Debug 전체 빌드를 통과했다. 이는 원래 grid area-light/HDRI·Special/texture/normal-map·route parity·이동 카메라/tier별 실제 Scene 성능 수용까지 완료한 것은 아니다. 후속 forest/autumn 고정 26장 대조에서 Scene HDR point sampler를 linear로 수정하고 host identity 5를 적용했다. Release DX12 수정 전/후 각 433367개 검사·validation 0건. 같은 target에서 재질 1/20·제어 3/6이며 HDRI 전체 수용은 미달이다. 거울 forest/autumn RMS는 8.2410/0.7838%→2.6332/0.5034%다. 131072-sample 박막 reference seed 차이 0.1587%에 비해 제품 RMS 84.1182%, 진단 32768샘플에서는 13.7322%다. 진단 32768샘플을 기본값으로 채택하지 않았다. 2026-10-01 environment/BRDF MIS(각 1024), CEIBL002 7-map 쿠킹·bootstrap·owner/generation 연결. forest/autumn 모든 GPU roundtrip 및 기존 4-map byte equality 통과. 최종 v2 cook dense 박막 RMS 84.1182→3.6083%, 전체 target은 재질 3/20·제어 3/6 미달. 64×64 강제 재계산의 박막 GPU 비용 증가도 남아 실제 모델 FPS 수용으로 세지 않음. 후속 source/cube 독립 적분에서 HDR 태양(최대 123904)의 64000 clipping이 확산 5.5749% 손실의 주원인임을 확인. CEIBL003 cube/prefilter float32·cold 4x4 footprint·CDF 경계 정밀도·실제 형식 binding·host identity 7 적용. 쿠킹/캐시/bootstrap 연결, 배포 58.26→90.26MiB. 131072-sample seed 0/11 고정 제어 7조건 전/후 각 121540 checks, validation 0, 기존 target 6/7 통과. autumn diffuse 5.4994→0.4107%, 박막 3.6083→0.8607%. forest mirror는 필터 영향으로 2.6413→3.1285% 증가하여 미달. 후속 CEIBL004 source 보존·픽셀 중심 보간·host identity 8로 forest mirror 3.1285→0.4693%, autumn mirror 0.5531→0.3275%. 동일 131072-sample seed 0/11·7조건 target 7/7, Native 121542 checks·validation 0. 기존 lighting/CDF byte 일치·8-map exact GPU roundtrip, 배포 90.26→98.26MiB, 실제 Debug Editor HTTP 11 checks·v4 bootstrap/캐시/손상 거부 통과. 기존 방향광/균일 환경 24장 target 24/24·RMS 최대 0.1792% 재검증. 후속 전체 HDRI 26조건을 131072-sample seed0/11로 수렴시켰다. CEIBL005 1024+4096 proposal bank 영속화·GGX1024/environment4096 unequal-count MIS·source radiance로 현재 제품 재질20/20·제어6/6 모두 기존 target 통과. RMS/p95/max 최대0.4959/0.4681/3.5924%, seed최대0.1717%. Native433369 checks·validation0, 8-map exact GPU roundtrip, Debug 전체 빌드·실제 HTTP11 checks·v5 bootstrap/캐시/손상 거부 통과. 배포98.38MiB(+128KiB), prefilter mip4 작은 float delta 별도 기록. 기존 방향광/균일 환경24조건도 target24/24·Native400520 checks·validation0·정상 종료, RMS최대0.1792%로 회귀 통과. 상수 Core/Layered 두 HDRI 범위이며 기존 Special/texture/normal-map/route parity/실제 모델 성능/area-light 게이트는 남는다. 추가 샘플의 성능 수용 전이다. 상세 MAT9HdriConvergence.md. 2026-10-01 Special smooth 80-triangle 24조건 초기 진단·Native394222 checks·validation0. v1 Volume transport 불일치로 11/24 숫자 통과를 수용으로 세지 않음. 투과 방향광 RMS33.9166%·glass34.7308%, SSS6.2429% 및 일부 reference 미수렴. SSS Scale0/off는 bit exact, flat 분리 진단은 diffuse 기하 기여·glass 미달을 확인. 비교 도구 최종 Volume 합성·reference v2 RNA bounce0/내부 감쇠를 맞춘 새 131072-sample seed0/11 Volume8조건은 기존 target8/8, RMS/p95/max 최대0.100973/0.134198/0.193067%, noise0.054315%. Release Native131590 checks·validation0·정상 종료, 독립96/192점 적분 대비 제품 RMS0.051739%. 이번 묶음에서 제품 Engine/Slang 구현은 변경하지 않았다. Scene Blended queue 실제 거부 확인, 기존 alpha+transmission gate는 남음. 다음 투과 경계/SSS 품질·수렴 → texture/normal-map → route parity·실제 모델 성능. 상세 MAT9SpecialTransportComparison.md. 후속 Scene artist SSS Radius×Scale 1/(4π) 환산·glass 별도 budget 일치·스침각 준비 거부/가장자리 오류색 수정, 최종 host identity10. 기존 Surface18조건 Blender131072-sample seed0/11 noise최대0.227574%로 모두 기존0.25% 이내 수렴. 동일 새 기준 sun SSS RMS6.2598→4.1009%, mixed sun2.0096→2.0373% 소폭 악화·furnace SSS4.2380% 그대로. fixed target7/18·전체 미수용. Debug/Release Native각18 frames·295717 checks·validation0·정상 종료, 294912 RGBA byte exact·Scale0/off 전체 이미지 byte exact·589 source 변경0, 전체 Debug Editor 빌드 통과. 반경 단계 SSS12/굴절24 frame 각Debug/Release 및 공용 Special202049개·golden 보존 통과. Volume8과 별도 검증이며 다음은 폐곡면 출구 경계/SSS 공간 응답/smooth grazing normal 개선과 같은 target 재검증. 기존 alpha+transmission/texture/normal-map/route·실제 모델 성능/area-light gate 유지. 상세 MAT9SpecialProfileCorrection.md. 기존 disk area는 light ABI/consumer 미지원으로 원래 grid 조명 gate에 남긴다. 새 완료/공수 행을 추가하지 않는다. MAT-9 progress, 완료 공수 32/34일. 후처리나 그림자로 재질 차이를 덮지 않는다. 2026-10-01 근접 카메라 캡처 LookupBake278표본 중20개 72.819~107.506ms. BRDF256/env4096 진단26/26·RMS최대0.9822%지만 일괄 성능 개선 미입증, 제품1024/4096 유지. 사용자 결정으로4.25/MAT-9 progress·32/34일 유지. BASE-0는 MAT-0~8 구현 기반을 소비하고 MAT-9 전체 수용을 착수 선행으로 요구하지 않음. 후속 GPU-driven 설계에서 영속 GPU Scene·변경분·PSO bin·indirect와 IBL 재사용/갱신을 연결하고 실제 구현 뒤 같은 품질 상한과 Debug/Release CPU/GPU median·p95·max·완료FPS·입력 지연을 재판정. SSS/투과 등 기존 품질 gate 별도 유지. 상세 MAT9NearCameraPerformance.md·MAT9IntegrationCheckpoint20261001.md.

근거: BlenderMaterialGraphPlan MAT-9 · analysis/MAT9MaterialScenePerformance.md · analysis/MAT9BlenderImageComparison.md · analysis/MAT9ThinFilmAndEnvironment.md · analysis/MAT9ThinFilmAcceptance.md · analysis/MAT9GgxClosureComparison.md · MAT9HdriImageComparison.md · analysis/MAT9MirrorSampling.md · analysis/MAT9HdriConvergence.md · analysis/MAT9SpecialTransportComparison.md · analysis/MAT9SpecialProfileCorrection.md

</details>
