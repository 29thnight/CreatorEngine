# Material Graph 제품 generation·바인딩·쿠킹 계약

**2026-10-02 통합 완료 · 기존 LX 제품 기반은 MAT-7-BASE 이력 · 공통 재질 소비 MAT-7 완료. PHASE 4.25는 LX 마감·MAT-9 때문에 진행 중.**

**2026-10-02 실행 환경 방향:** LX가 generation/instance·바인딩·자원/PSO 수명의 공통 runtime을 소유한다. ShaderMeta는 생성된 셰이더의 계약 표현이며 기존 authored 코드 셰이더 입력도 LX 계약으로 적응한다. 코드 입력 어댑터와 공통 graphics/compute owner를 연결했고, 코드 셰이더의 generic property도 유지한다. 프레임 owner 없는 RT registry 대체 조회를 제거했다. Code 입력 준비·진단 registry는 유지한다.

Graph→실제 `.shadermeta`+`.slang` 생성과 공통 Material 값/바인딩·LXMC v4 복구를 연결했다. Code와 Scene host의 graphics shader/PSO는 [LX 공통 owner](../analysis/MAT7GraphicsPipelineOwnership.md), 보조 compute는 [compute owner](../analysis/MAT7ComputePipelineOwnership.md)를 사용한다. [Core/Layered 일반 Blend](../analysis/MAT7CommonForwardBlend.md)에 이어 [SSS/transmission/Volume 혼합 transport](../analysis/MAT7ForwardTransportComposition.md)를 같은 Forward+ 순서에 연결했다. 제품 편집/실패 복구·실제 cooked Player 회귀는 [제품 검증 기록](../analysis/MAT7ProductEditingRecovery.md)을 따른다. SceneHost identity 12로 재쿠킹한다. 기존 2026-09-29의 “MAT-7 완료”는 아래 LX 경로에서 검증한 기반의 이력이며, 현재 통합 완료선은 2026-10-02의 여덟 번째 단계다.
현재 통합 순서·원본/파생물 소유·공통 Forward+/일반 alpha Blend·제품 회귀의 완료 기준은 [BlenderMaterialGraphPlan §0](../plans/BlenderMaterialGraphPlan.md#0-현재-판정과-실행-범위-2026-10-02)이 소유한다. 생성 어댑터의 검증 범위와 잔여는 [MAT7ShaderMetaAdapter](../analysis/MAT7ShaderMetaAdapter.md)에 분리한다.

공통 Material 값/바인딩의 초기 검증 범위는 [MAT7CommonMaterialConsumption](../analysis/MAT7CommonMaterialConsumption.md)이 소유한다. 아래 LXMC v2/artifact v2 설명은 기존 구현 이력이며 현재 writer/reader는 **v4**다. 실제 생성 메타·전체 Slang·bytecode와 CEDO 생성 계약을 한 generation으로 복구한다. immutable owner의 바이너리 계약으로 재검증·재쿠킹하며 Player에서 저작용 텍스트 파서를 호출하지 않는다. v3 이하는 재쿠킹하고 CEMF 자체 schema는 유지한다.

MAT-6의 [결정적 Slang 생성](MaterialSlangCodegen.md)을 제품의 reflection,
텍스처 소유권, PSO 교체와 cooked 자산으로 내리는 경계다. 구현은
`Engine/RenderEngine/MaterialGraphProduct.{h,cpp}`, `MaterialGraphRuntime.{h,cpp}`,
`MaterialGraphRenderBindings.{h,cpp}`, `MaterialGraphIblBake.{h,cpp}`와
`Experiment/Cooked/CookedMaterialProgram.{h,cpp}`에 있다.

## 1. 기존 LX 경로에서 검증된 구현 기반

| 경로 | 현재 상태 |
|---|---|
| Slang bytecode·reflection·실제 include/compiler 의존 identity | 같은 linked program에서 검증·게시 |
| 숫자·Texture2D·독립 Sampler의 제품 register/layout | 실제 DXIL/SPIR-V reflection 대조 |
| typed override·sRGB view·텍스처 generation 소유 | CPU packet 검증 및 D3D12 hardware readback |
| PSO·layout·route·uniform·texture owner 교체 | 실제 D3D12 PSO 성공/실패와 완료 fence 검사 |
| LXMC·CEMF·AssetCooker·loose/encrypted PAK | typed 재개방·SHA-256·폐포·게시 실패 검사 |
| DataSystem/Material의 LX generation·instance 소비 | GUID 적재·값/텍스처 override·실제 저장 재개방 검증 |
| 제품 RHI의 b2·Texture2D·독립 Sampler 바인딩 | 실제 엔진 DX12 서비스로 오프스크린 draw/readback 검증 |
| Core/Layered evaluated-point IBL bake | 실제 엔진 RHI compute→lookup 소비 draw/readback 검증; Scene 픽셀 캐시 확장 |
| 불투명 가시 픽셀·UV fine derivative 수집 | 독립 RHI RenderGraph의 순차/병렬 기록 → graph/IBL 검증; 한 재질 chunk 묶음·4096픽셀 범위 |
| Scene의 LX generation 렌더 소비 | Core/Layered Opaque/Masked의 실제 GBuffer·공유 depth·HDR 합성 연결 |
| 실제 Scene의 자동 route 분류 | 지원 Core/Layered/Special Surface·Volume host 선택·미지원 거부; route 교차 parity는 MAT-9 |
| Core/Layered 환경 lookup | 정확한 Scene 픽셀 캐시·변경 입력 갱신; 일반 Scene 실시간 비용은 후속 |
| 실제 Scene의 이미지별 UV·LOD | 샘플별 Vector fine derivative·이미지 크기/mip 수; compute는 explicit LOD 유지 |
| 실제 Scene generation 준비·교체 | Slang·DX12/Vulkan PSO worker 준비, exact batch 성공 뒤 마지막 정상 instance·coverage 게시 |
| 실제 Scene 재질별 SSS | owner/profile 기반 유한 dipole transport·Special HDR 연결 |
| 실제 Scene transmission/refraction | 뒤쪽 Code/Graph alpha가 합성된 현재 HDR·불투명 depth 복사, 자기 surface scratch depth/owner, GGX 굴절·공통 Forward+ 연결 |
| 실제 Scene의 닫힌 균질 Volume | 계수 생성·깊이 제한 카메라/굴절 ray의 흡수·발광·단일 산란 합성 |
| 실제 Editor Scene/Game 상태·수명 | HTTP 재질 변경·Undo/Redo·실패 복구·저장/재개방·Play/Stop·삭제/복구의 owner·픽셀 검증 |
| native Vulkan 전체 Scene | Core/Layered·Shadow/Decal·SSS·Refraction·Volume·encrypted cook의 Debug/Release readback |
| 일반 alpha Blended queue | Code/Core/Layered/SSS와 physical transmission의 공통 Forward+ 정렬·합성, Volume의 표면 깊이별 카메라 transport. DX12 Debug/Release mixed/Volume·제품 회귀 완료; 전체 색/비용 수용은 MAT-9 |

기존 LX 제품 기반의 종료 증거는 [MaterialGraphProductIntegration.md](MaterialGraphProductIntegration.md)가
소유한다. LX-3 artist canvas 이관, MAT-8 preview와 MAT-9 rendered parity·성능 수용은 별도다.
기존 Scene은 여전히 `m_renderingMode`로 opaque와 transparent draw를 나눈다.
기존 Standard pass를 LX host로 바꾸지 않았다.

## 2. 자동 선택과 미지원 거부

`SelectRoute`는 active output의 정적 feature와 renderer가 실제 설치한
`Capabilities`로 Standard/Layered/Special과 Deferred/Forward/Volume을 선택한다.
artist가 register/pass를 선택하지 않는다. sample·resource·variant·compiled byte
상한도 같은 경계에서 검사한다.

- 현행 GBuffer 지원은 `0x003F`. IOR/specular/layered를 저장할 수 없으면 Forward를 요구한다.
- `Capabilities`의 Core Forward, layered reflection lookup, refraction, 재질별 SSS,
  Volume 기본값은 false다. 제품 Scene host/compiler는 설치한 경로의 capability를 명시적으로
  켜고 각 feature의 실제 자원·stage·예산을 준비한다.
- 미설치 기능을 선택하면 원인을 진단하고 기존 generation을 보존한다. feature를
  지우거나 transmission을 alpha로 바꾸지 않는다.
- Capabilities의 `true`는 자원과 evaluator가 실제 연결됐다는 renderer의 보증이다.
  독립 probe는 해당 검사 host의 보증을 주며 Scene의 보증을 대신하지 않는다.
- 일반 graphics `PipelineSlot`은 surface-only만 게시한다. Surface+Volume도
  Volume compute/합성 generation과 함께 게시하는 renderer 경로가 생기기 전에는 거부한다.

정적 tier는 물리 재질 기능 분류이며 authored opacity/coverage와 독립이다.
실제 제품 연결 시 Forward를 transparent와 동일한 draw queue로 취급하면 안 된다.

## 3. 바인딩과 색 공간

| 논리 입력 | 제품 위치 | 판정 |
|---|---|---|
| Texture slot N | `t16+N, space0` | 이름·kind·slot·개수·Texture2D view 검증 |
| Sampler slot N | `s3+N, space0` | texture slot과 독립, nearest/linear × repeat/clamp |
| Numeric Blackboard | `b2, space0`, `LXMaterialProperties` | reflection offset/size/type로 pack |

`BuildBoundSource`는 기존 source map 줄을 유지하며 register를 투영한다.
숫자 필드는 float32/int32/bool32와 float3/float4다. 두 backend의 uniform layout이
다르거나 겹치거나 예기치 않은 필드/자원이 있으면 검증을 실패시킨다.
Uniform은 제품 상수 버퍼 한도인 64 KiB 이내다.

`PrepareResources`는 공개된 numeric parameter만 override하고 finite float32와
int32 범위를 확인한다. padding은 0으로 고정한다. texture 등록 handle과 generation
owner를 함께 받는다. SRGB 입력은 SRGB view 또는 이미 선형화한 storage라는 명시적
보증이 필요하다. Data/Linear 입력에 SRGB view를 사용하면 거부한다.
Hardware SRGB filtering은 decode→filter 순서를 사용한다.

ResourcePacket은 owning CPU packet이다. `RenderBindingCache`가 실제 texture cache의
`GetOrUpload`와 `UploadConstants/CreateBindings/CreateSamplers`, 인코더 root slot 바인딩을
연결한다. 실제 엔진 DX12 서비스를 사용한 오프스크린 draw/readback은 검증했으며,
Scene pass의 소비는 아직 연결하지 않았다.

### 제품 render binding 경계 (2026-09-28)

`CreatePassLayout`은 host root 범위를 유지하고 사용 중인 material b2, t16+,
s3+ 범위를 덧붙인다. 숫자/texture/sampler가 없는 범위는 만들지 않는다.
host의 CBV/root constants, SRV buffer/table, dynamic/static sampler와 겹치면
기존 layout을 보존하고 실패한다. 중복/범위 밖 resource slot도 생성 전에 거부한다.

`RenderBindingCache::Prepare`는 같은 graph generation의 reflected layout과
불변 instance를 받아 실제 Texture owner를 업로드하고 숫자 bytes를 다시 대조한다.
cache가 실패한 텍스처를 neutral로 대신 반환하더라도 오류로 처리한다.
sampler는 각 resource의 nearest/linear × repeat/clamp 설정을 독립된 슬롯에 넣는다.
전체 배열이 같은 sampler 테이블은 프레임과 숫자 override 변경 사이에 재사용한다.
실패한 upload/layout/value 후보는 이전 render packet을 교체하지 않는다.

packet의 constant/texture descriptor는 recording ID와 descriptor version으로 묶는다.
다른 device·recording의 packet을 `Bind`하면 명령을 기록하기 전에 거부한다.
PSO를 먼저 설정한 뒤 반환된 material 슬롯에만 바인딩한다. RenderGraph가 shader-read
usage와 상태 전이를 선언하며 이 adapter가 임의로 texture 상태를 바꾸지는 않는다.
실패 시 보존된 packet도 새 recording에서는 재준비해야 하며, 이전 프레임의 transient
handle을 재사용하는 fallback은 허용하지 않는다.

CPU/program owner는 packet이 유지하고 GPU texture registration과 fence retirement는
외부 RHI texture cache가 소유한다. render owner는 제출 완료까지 이 cache와 packet을
보존하며 매 recording에서 `Prepare`로 사용 기록을 갱신한다. sampler cache는 device
idle 뒤 device 재생성 전에 비운다. 이 구현은 Core/Layered의 물리 lookup·환경 convolution을
설치하거나 renderer capability를 켜지 않는다.

### Core/Layered GPU bake 경계 (2026-09-28)

[PrincipledIblBake.md](PrincipledIblBake.md)의 `IblBaker`는 평가된 재질·normal/tangent·
view와 환경 generation을 받아 별도 반사 계수, base/coat/sheen 방향 convolution을 만든다.
coat normal의 다중산란 조도도 별도로 소비한다. 입력 전체와 환경 owner/generation을
비교해 override/view 변경을 검출하며 실패한 bake 요청은 이전 result를 보존한다.
compiled/cooked CS를 받고 생산 픽셀 셰이더는 적분 루프 없이 결과를 읽는다.

이는 1~4,096개 고정 평가 지점 묶음이다. Scene의 texture-varying 입력·lookup 배치와
보간/재사용·RenderGraph usage·in-flight 게시/회수는 아직 연결하지 않았다.
환경 중요도/MIS·HDR 표본 수 수렴과 실제 시간/메모리 예산도 제품 연결 단계에서 판정한다.
기존 Standard glTF 2채널 LUT는 유지하며 Core/Layered Scene capability는 계속 미설치다.

## 4. 후보 전체 교체와 수명

`VerifyProduct`는 engine host를 붙인 파일이 정확한 bound program으로 시작하는지
검사한다. DXIL/SPIR-V에 필요한 VS/PS 또는 Volume CS 전부를 검증하고,
backend별 non-vertex entry의 재질 reflection을 합쳐 `VerifiedProduct`를 게시한다.
stage별 미사용 resource 제거는 허용하되, 공유 binding의 이름·타입·offset·배열 길이가
다르면 거부한다. 합친 layout은 원래 typed resource/parameter 계약과 일치해야 한다.
semantic key에 실제 공용 include·compiler binary·option·entry identity를 포함한다.

`PipelineSlot::Publish`는 verified bytecode에서 해당 backend VS/PS를 선택한다.
caller는 fixed-function state·render format·설치된 root layout을 공급한다.
PSO 생성 전에 route·layout·값·텍스처를 검증하며 성공할 때만 다음을 함께 교체한다.

```text
semantic key + route + reflected layout + numeric bytes
    + texture generation owners + owning shader request + PSO handle
```

컴파일/바인딩/값/native PSO 실패는 마지막 정상 generation을 보존한다.
교체된 PSO는 caller의 GPU completion point에 retire하며 재사용한 동일 handle은
무효화하지 않는다. 텍스처 owner는 같은 handle을 재사용했더라도 별도로 retire한다.
완료 fence가 `0`이면 완료 시점을 모르는 것이므로 owner를 장치 idle 뒤 슬롯 파괴까지
보존한다. 슬롯은 제출된 generation보다 오래 살아야 하며 render owner 한 곳에서 쓴다.
공유 PSO의 외부 CPU holder가 있으면 owning renderer가 함께 교체 경계를 보증해야 한다.

## 5. 쿠킹과 runtime 재개방

- `.shadergraph`의 UUIDv4를 `Derived/MaterialPrograms/<prefix>/<GUID>.lxmaterial`에 매핑한다.
- LXMC schema 2는 compiler schema, dependency-bearing key, typed defaults/resource table,
  reflected offset, source map, route와 backend/entry/profile별 bytecode를 소유한다.
  JSON metadata와 bound/original Slang은 진단·일관성 대조용이다.
- LXMC reader는 크기·개수·enum·layout·stage·checksum을 검사하고 metadata/bound source를
  typed table에서 다시 만들어 대조한다. 실패하면 기존 결과를 보존한다.
- CEMF schema 2에 `MaterialProgram=8`, artifact version 2를 추가했다.
  texture UUIDv4/v8 의존성은 중복 제거·정렬하고 manifest 폐포가 닫혀야 한다.
- `CookedAssetCatalog::OpenMaterialProgram`은 GUID/kind/path/version/size/SHA-256와
  texture dependency 종류를 확인한다. loose tree와 immutable mounted encrypted PAK에서
  동일한 full typed generation을 복원하며 Slang 컴파일/reflection이 필요하지 않다.

AssetCooker 입력:

```text
--asset-root <Assets>
--output <새 cooked tree>
--shadergraph <source> ...
--material-program-root <검증된 program tree>
--texture <graph가 참조하는 실제 source texture> ...
```

source graph를 strict reader로 열어 canonical compiler로 재생성하고, 검증된 산출물의
Slang/typed metadata와 정확히 일치해야 쿠킹한다. artifact·CEMF·폐포를 staging에서
재개방/대조한 뒤 새 디렉터리를 게시한다. 손상 shader, 오래된 graph 또는 texture 폐포
누락이면 게시하지 않는다.

현재 입력은 **앞서 host compiler가 검증한 specialization**이다. cooker가 제품 host를
자동 선택·생성하는 단계와 프로젝트 build/packaging의 graph 자동 수집은 남아 있다.
source graph 일치 검사만으로 오래된 compiler/common include와의 freshness를 보증하지
않는다. host compiler가 현재 의존성으로 산출물을 재검증한 뒤 넘기는 계약이며,
제품 cooker의 compiler 소유·freshness 검사를 연결해야 한다.

## 6. 검증과 실제 제품 연결 기록

아래 각 절은 단계별 구현 당시의 범위와 증거다. 현재 완료선은 1절과 마지막 MAT-7 종료
결과 및 [MaterialGraphProductIntegration.md](MaterialGraphProductIntegration.md)를 따른다.

### DataSystem·Material generation 연결 (2026-09-28)

`MaterialGraphRuntime`의 `GenerationStore`는 graph UUIDv4로 full typed program을
소유한다. 동시에 같은 GUID를 적재하면 한 번만 읽으며, 동일한 전체 payload는
generation 번호를 유지한다. 실패한 후보는 성공으로 반환하지 않고 마지막 정상
lookup을 보존한다. 제거·재등록과 catalog 재마운트에서도 번호를 재사용하지 않는다.
이미 생성된 Material/clone은 자기 불변 generation owner를 계속 보유한다.

`DataSystem::LoadMaterialGraphGeneration`은 현재 mounted CEMF의 size/SHA-256와
typed metadata/bytecode를 검사한다. Editor에서는 현재 `.shadergraph`를 다시 생성해
Slang/metadata가 정확히 일치해야 재로딩한다. packaged load는 source graph와 Slang
compiler를 사용하지 않는다. 실제 DataSystem은 추출된 cooked tree를 읽고,
`LoadCookedGeneration`의 byte-source 경계는 encrypted PAK도 검사했다.
DataSystem 자체의 직접 PAK 마운트를 완료로 기록하지 않는다.

`ConfigureMaterialGraph`는 공개 numeric parameter ID와 Texture parameter ID/GUID를
검증하고 reflected uniform bytes 및 색 공간별 CPU Texture owner를 모두 준비한 뒤
Material instance snapshot을 교체한다. 잘못된 타입·비공개/미지 ID·없는 Texture GUID는
기존 snapshot을 보존한다. 숫자 setter와 clone은 새 snapshot을 만들어 값을 분리한다.
Material 변경은 소유 thread에서 직렬화하며, RT는 후속 Scene 단계의 owning draw
snapshot을 소비한다. 이 단계의 CPU Texture는 아직 RHI 등록·GPU 제출을 뜻하지 않는다.

저장 정본 `lattice_material: 1`은 graph GUID, material GUID/name/doubleSided,
안정적인 parameter ID와 bool/int/float/vector/color 값, Texture override GUID를 담는다.
generation 번호·주소·바이트 오프셋을 저장하지 않는다. double 저작 값을 최대 유효
자릿수로 저장하고 실제 binding은 float32/int32 계약으로 검사한다. unknown/duplicate
key, 버전/타입/범위 오류를 거부하며 실패한 decode는 이전 instance를 보존한다.
DataSystem의 YAML 및 기존 CEMT/CEDO binary 경계에서 같은 정본을 사용한다.
Sampler 변경, coverage/queue 정책, standalone graph material/Scene의 자동 cook 폐포는
후속 단계다. ShaderMeta 문서의 기존 read/write 경로는 유지한다.

Scene clone finalization은 LX snapshot을 지우지 않는다. 명시적인 legacy 문서를
성공적으로 읽었을 때만 LX 상태를 해제한다. LX 재질은 기본 ShaderMeta pass로
내려가지 않으며 [bounded Scene host](MaterialGraphSceneHost.md)의 지원/예산 계약을 적용한다.
Scene 실제 렌더 또는 Editor 전체 GUI 완료를 주장하지 않는다.

재현:

```powershell
# RenderEngine와 AssetCooker Debug/Release를 현재 소스로 빌드한 뒤
& Tools/regression/verify-material-product.ps1 -VerifyAssetCooker
& Tools/regression/verify-material-datasystem.ps1
& Tools/regression/verify-material-render-bindings.ps1
& Tools/regression/verify-material-ibl-bake.ps1
```

RTX 4070 Ti D3D12 hardware probe는 121개 검사와 SRGB·Alpha·reflected override 값 60성분,
실제 native PSO 거부, last-good 교체, typed 저장 왕복/손상 거부와 owner fence 수명을
검사한다. 실제 AssetCooker Debug/Release 결과를 loose 및 encrypted PAK으로 다시 열고
compiler 호출 0회로 native PSO를 만든다(각 7개 검사). source 변경·누락 texture·손상 shader 각각의
게시 거부와 이미 성공한 cooked bytes 보존을 검사한다.
앞선 기반 검증에서 RenderEngine library와 AssetCooker host/launcher의 Debug/Release
빌드가 통과했다. 이번 연결 변경은 RenderEngine/Utility_Framework와 직접 호출 probe를
Debug/Release로 다시 빌드하고, 기존 AssetCooker 실행 파일로 새 fixture를 쿠킹했다.
generation·instance·typed 저장 왕복·실패 복구·동시 8개 GUID 적재 회귀 43개가 통과했다.
제품 RenderEngine/Utility_Framework에 링크한 `MaterialDataSystemProbe`는 Debug/Release
각 26개 검사로 실제 DataSystem GUID 로딩·PNG decode/SRGB·Material setter/clone,
YAML/CEMT/CEDO 저장 재개방·기존 ShaderMeta 문서 왕복·stale reload/removal/finalize를
검사한다. Console host는 WIC용 COM과 Material typed reflection ops를 초기화한다.
`MaterialRenderBindingsProbe`는 현재 RenderEngine/Utility_Framework에 링크해 Debug/Release
각 168개 검사와 64개 GPU 성분 비교를 통과했다. 실제 DX12 device/root cache/PSO cache/
texture cache/device services/encoder로 2×1 SRGB 이미지 하나에 네 sampler를 함께 적용한다.
텍셀 사이와 범위 밖 UV의 필터·repeat/clamp, 프레임별 numeric override, sampler 테이블 재사용,
root 범위 충돌·누락/중복/alias 슬롯·stale recording·픽셀 없는 upload의 이전 packet 보존을
검사했다. 각 4프레임을 제출·완료 대기 후 읽었고 GPU validation WARNING 이상은 0건이다.
이 검사는 Scene draw, 동시 인플라이트 수명이나 물리 Core/Layered IBL 결과를 판정하지 않는다.
후속 `MaterialIblBakeProbe`는 Debug/Release 각각 19,351개 검사·18,240개 GPU 성분을 통과했다.
152개 지점×3프레임의 균일/면별 HDR 환경, Core/Layered 계수와 별도 base/coat/sheen 응답,
실제 lookup 소비 픽셀을 독립 double CPU 수식과 대조했다. DXIL/SPIR-V CS/VS/PS 6개,
GPU validation WARNING 이상 0건, 최대 정규화 오차 0.00000950694다.
새 비교 상한은 0.0001, 거의 0인 반사 에너지의 상한은 0.000001이다.
기존 Layered 216,776개·energy bound 5,712개 및 numeric golden 1,960행/7,840성분,
DXIL/SPIR-V 14개·지정 거부 8개도 이번 bake 변경으로 다시 검사했다.
이 검사는 Scene lookup 설치·임의 HDR 수렴·실시간 성능·동시 인플라이트 수명을 판정하지 않는다.
앞선 기반 검증의 MAT-6 20개 graph/13,306개 검사·3,600 GPU 성분·93개 컴파일과
기존 pass 70개 컴파일/지정 거부 58개 결과는 유지한다. 이번 연결 변경의 재실행으로
기록하지 않는다.

대시보드 JS 파싱은 통과했다. 전체 dashboard checker는 현재와 HEAD 모두 같은
미산정 `days:null` 24행 및 PHASE 4.6 meta 불일치 1건으로 실패한다. MAT-7 수정으로
추가된 오류는 없으며 해당 전역 checker를 통과로 기록하지 않는다.

raw artifacts와 로그는 `Build/Obj/MaterialProductProbe/`, 종합 판정은 그 안의
`verification-summary.md`에 기록한다. DXIL/SPIR-V 컴파일은 두 backend를 검사하지만
actual PSO/GPU runtime은 D3D12다. Vulkan runtime 완료로 표시하지 않는다.

다음 구현 순서:

제품 Model 정점 형식의 GPU skin/world 입력과 완료 검증 후 GPU packet 게시 경계는
[MaterialGraphMeshSurface.md](MaterialGraphMeshSurface.md)에 구현·검증했다.
큰 메시 triangle 분할과 UV/world frame 보간 후 정확한 샘플 재질/IBL 평가,
명시적 footprint LOD도 추가했다.
[MaterialGraphRasterSurface.md](MaterialGraphRasterSurface.md)에 depth/MRT 기반 가시 픽셀과
fine UV derivative를 수집하는 독립 RHI RenderGraph 패스를 추가했다. 동일 graph의 재질 평가·IBL 준비/선언/병렬 기록은 [MaterialGraphPassRecording.md](MaterialGraphPassRecording.md)에 연결했다.
current-pose mesh 생산과 Core/Layered의 read-only shared depth·opaque draw winner 확장은
[MaterialGraphSharedDepth.md](MaterialGraphSharedDepth.md)에 추가했다. 독립 RHI 범위이며 기존
Scene/GBuffer의 depth·masked coverage를 설치한 것으로 세지 않는다.
[MaterialGraphSceneInput.md](MaterialGraphSceneInput.md)에 producer proxy/delta와 실제 draw pool/per-view
CPU 입력 밀봉을 연결했다. [MaterialGraphSceneHost.md](MaterialGraphSceneHost.md)에 실제
GBuffer·legacy/Masked 공유 depth·직접광/HDR 합성의 host를 추가했다.
[MaterialGraphSceneLookup.md](MaterialGraphSceneLookup.md)에 정확한 픽셀 입력 비교·IBL 재사용과
실제 제출 뒤 캐시 게시·payload 예산을 확장했다.
[MaterialGraphTextureFootprints.md](MaterialGraphTextureFootprints.md)에 실제 Scene의 이미지별
Vector·크기·mip 수에 따른 독립 LOD와 단일 텍스처 제한 해제를 추가했다.
[MaterialGraphSceneGeneration.md](MaterialGraphSceneGeneration.md)에 generation별 Slang 검증·DX12/Vulkan
native PSO 준비와 실제 graph batch 티켓 성공 후 슬롯 게시를 추가했다. pending/실패·abort·stale
결과는 같은 epoch/view/Material의 마지막 정상 instance·coverage를 유지한다. 현재 geometry는
새 입력을 사용한다. Vulkan은 native PSO 준비·기본 GPU draw까지 검증했다.
Vulkan 전체 Scene 합성의 후속 실행 증거는
[MaterialGraphProductIntegration.md](MaterialGraphProductIntegration.md)가 소유한다.
[MaterialGraphSceneSubsurface.md](MaterialGraphSceneSubsurface.md)에 실제 Scene의
재질별 SSS source/profile·draw owner mask·world-distance dipole gather·Special HDR 합성을 추가했다.
가시 표면의 유한한 근사이며 transmission/refraction·Volume의 설치 완료로 세지 않는다.
[MaterialGraphSceneRefraction.md](MaterialGraphSceneRefraction.md)에 후속 단일 표면의
transmission/refraction을 설치했다. 가장 가까운 투과 표면의 뒤쪽 불투명 HDR/depth를 읽고,
glass 적분·rough radiance convolution·환경 fallback 뒤 최종 depth/owner/HDR을 갱신한다.
[MaterialGraphSceneVolume.md](MaterialGraphSceneVolume.md)에 닫힌 균질 매질의 계수 생성,
깊이로 제한한 카메라 ray와 굴절 ray의 흡수·발광·단일 산란을 연결했다.
일반 alpha Blended queue는 이 transport 경로에 포함하지 않는다.

MAT-7 종료 범위는 다음 세 묶음으로 고정한다.

1. 제품 render path: LX shadow caster/Decal lobe 연결.
   SSS·transmission/refraction·Volume은 위 문서의 유한한 근사 범위로 구현한다.
2. 자동 cook/package·Player: 현재 host/compiler 의존성으로 직접 검증하고 graph 폐포를
   build/packaging이 자동 수집한다. packaged 실행에서도 cooked Scene host를 사용한다.
3. 제품 통합 검증: 실제 Editor Scene/Game·교체·저장 후 재개방과 수명,
   native Vulkan 전체 Scene 합성을 확인한다.

2026-09-29 첫 묶음은 [MaterialGraphSceneShadowDecal.md](MaterialGraphSceneShadowDecal.md),
두 번째 묶음은 [MaterialGraphSceneCook.md](MaterialGraphSceneCook.md)의 구현·검증으로 완료했다.
자동 쿠킹의 반복 바이트 일치·실패 복구와 Debug/Release 실제 packaged Player의 cooked Scene
ready·화면 게시 24회·Scene specialization compile 0회·텍스트 파서 호출 0회를 확인했다.
세 번째 묶음도 [MaterialGraphProductIntegration.md](MaterialGraphProductIntegration.md)의
full CreatorEditor Debug/Release 각 1,430개 검사·11개 capture와 native Vulkan 6개 Scene
경로로 완료했다. Editor는 저장/재개방·Undo/Redo·실패 복구·Play/Stop·삭제/복구와 실제
owner·픽셀을 검사했다. Vulkan은 Core/Layered·Shadow/Decal·SSS·Refraction·Volume과
encrypted cooked Scene을 readback·종료까지 validation/encoder drop 0개로 검사했다.
**2026-09-29 고정한 세 종료 묶음을 모두 완료했으며 MAT-7은 done이다.**

초기 CS 전체 비동기화·전역 cache 최적화·환경 MIS/수렴·named UV 확대는 별도 개선이다.
현재 캐시의 cold/카메라 이동·입력 MRT·메모리·dense Scene 비용 및 최종 성능 수용은 MAT-9가
소유한다. 입증된 제품 오류의 수정은 해당 통합 검증에 포함하지만, 최적화 항목을 MAT-7
필수 잔여로 계속 추가하지 않는다. reflected b2·독립 sampler root/table은 이미 설치했다.

Blender rendered grid/route parity·Standard 성능 수용은 MAT-9, artist preview·표시는
MAT-8, Editor canvas/HTTP는 LX-3/LX-3H가 계속 소유한다.
