# PBR 배선 안정화 계획 (PHASE 4)

**신설 2026-09-03 · 갱신 2026-09-06 · 10슬라이스 18일 · W2/W4/W5/W6 완료 · W0/W1/W3/W7 진행 · 실장면 기준 캡처 실행, W9 acceptance 미완료**

이 계획은 현재 제품 렌더 경로의 `.slang`·머테리얼·렌더러 배선 결함만 닫는다.
Blender형 Material Graph와 Principled 확장은
[`BlenderMaterialGraphPlan.md`](BlenderMaterialGraphPlan.md), **PHASE 4.25**가 소유한다.
RenderGraph·일반 Custom Pass·그림자·reflection probe·후처리는 **PHASE 4.75**로 보낸다.

좌표계 변환 자체는 Blender 시각 동등성의 범위가 아니지만, 현재 제품 결함인 UV set/transform,
sampler/mip, tangent basis와 non-uniform scale normal transform은 이 페이즈에서 수정한다.

---

## 1. 2026-09-03 소스 감사 결과

아래는 착수 전 감사 기록이다. 2026-09-06 구현·검증으로 해소한 항목은 §6~§12에 구분한다.

| 우선 | 확인한 현재 상태 | 제품 위험 | 소유 슬라이스 |
|---|---|---|---|
| P0 | 제품 GBuffer·Deferred는 `GBuffer.slang`·`Deferred.slang`을 가지지만 Forward 제품 진입점은 `ForwardShade.hlsl`에 남아 있다 | Deferred/Forward BRDF·IBL·재질 의미가 서로 다른 언어와 구현에서 드리프트 | `PBR-W2` |
| P0 | `GBuffer.slang`은 `t0..t3` 네 슬롯만 사용하고 `aoMap` 슬롯이 없어 occlusion을 1로 고정한다 | importer/Material에 있는 AO·`occlusionStrength`가 실제 조명에 도달하지 않음 | `PBR-W5` |
| P0 | `alphaCutoff`는 property block 존재 판정용 sentinel로 쓰이지만 pixel discard가 없다 | glTF `MASK`가 Opaque처럼 렌더되고 경계·깊이·그림자가 잘못됨 | `PBR-W4` |
| P0 | Vulkan/DX12의 neutral texture와 selftest 판정이 같은 논리 기본값·종료 코드 계약으로 잠기지 않았다 | backend별 ORM/검정 픽셀 차이와 거짓 초록 가능 | `PBR-W3` |
| P1 | `doubleSided`·`emissiveStrength`, constant-only emission, emissive texture 색공간 계약이 제품 binding까지 완결되지 않았다 | 검정 재질·과소/과다 발광·Blender/glTF와 다른 결과 | `PBR-W4`, `PBR-W6` |
| P1 | UV0 고정 sampling과 단일 sampler 가정이 남아 있고 UV set/transform/wrap/filter/mip 계약이 없다 | 정상 텍스처도 배치·축척·선명도가 자산과 다르게 보임 | `PBR-W7` |
| P1 | world의 3×3을 normal/tangent에 직접 곱한다 | non-uniform scale에서 normal과 highlight가 틀어짐 | `PBR-W7` |
| P1 | flat property/고정 texture table과 descriptor batch가 material generation보다 약한 신원으로 재사용될 여지가 있다 | 전체 mesh가 검거나 색이 바뀌는 간헐적 플리커를 fail-closed로 가두지 못함 | `PBR-W8` |
| P1 | GBuffer의 diffuse/metalRough/normal/emissive가 모두 고정 폭 포맷이고 material texture 입력도 4개로 고정돼 있다 | 대역폭 낭비와 AO/향후 재질 입력 확장 충돌 | `PBR-W5`, `PBR-W8` |
| 완료 대기 | `useNormalMap`은 저작 material snapshot→scene snapshot→GBuffer/Forward instance까지 전달하도록 수정했고 Debug x64 빌드는 통과했다 | 실제 Gunner/primitive 런타임 장면 판정은 아직 하지 않음 | `PBR-W1`, `PBR-W9` |

이 표는 정적 감사와 이미 수행한 빌드 결과다. 런타임에서 플리커의 단일 원인을 확정했다는 뜻이
아니며, `PBR-W8`에서 generation/descriptor 일치성 위반을 먼저 검출하고 `PBR-W9`에서 실제 장면으로
판정한다.

---

## 2. 목표 제품 경로

```text
typed Material generation
    -> immutable MaterialInputs + texture/sampler table
    -> shared native Slang material evaluation
    -> GBuffer 또는 Forward route
    -> shared BRDF/IBL inputs
```

- 제품 Standard PBR 진입점은 native `.slang` 한 계통만 사용한다.
- HLSL은 이행 fixture 또는 다른 독립 셰이더에만 남길 수 있고 제품 PBR 정본이 될 수 없다.
- material generation, texture/sampler table, descriptor generation, PSO key를 한 snapshot으로
  밀봉한다. 일부만 새 세대로 섞이면 draw를 생략하고 원인을 기록한다.
- 현재 glTF metallic-roughness 의미를 정확히 전달하는 것이 목표다. Principled lobe를 이
  페이즈에 섞지 않는다.

---

## 3. 실행 순서와 공수

`◐`는 구현 또는 조사 일부 완료이나 런타임 acceptance가 남았음을 뜻한다.

| ID | 내용 | 상태 | 선행 | 일 |
|---|---|---|---|---:|
| `PBR-W0` | 감사 정본·Gunner/primitive capture·strict gate | ◐ | PHASE 3.75 | 2 |
| `PBR-W1` | normal-map 저작 유무 snapshot 단일화 | ◐ | — | 1 |
| `PBR-W2` | GBuffer/Deferred/Forward native Slang 제품 진입점·공용 현행 평가 | ✓ | W0 | 2.5 |
| `PBR-W3` | backend neutral resource·binding·종료 코드 동등성 | ◐ | W0 | 1 |
| `PBR-W4` | OPAQUE/MASK/BLEND·alpha cutoff·double-sided/cull | ✓ | W2 | 2 |
| `PBR-W5` | AO 소비·고정 4 texture slot 제거·GBuffer packing 검토 | ✓ | W2, W3 | 2.5 |
| `PBR-W6` | emissive factor/strength·constant-only emission·색공간 | ✓ | W2 | 1.5 |
| `PBR-W7` | UV set/transform/sampler/mip·normal/tangent 변환 | ◐ | W2 | 2 |
| `PBR-W8` | material/descriptor/PSO generation 원자 밀봉·플리커 fail-closed | · | W3~W7 | 2 |
| `PBR-W9` | DX12/Vulkan 실장면·장시간·재임포트 회귀와 cutover | · | W8 | 1.5 |
| **합계** |  |  |  | **18** |

`PBR-W0`은 정적 감사 절반, `PBR-W1`은 코드·빌드 절반을 기성으로 센다. 둘 다 `PBR-W9`의
실장면 acceptance 전에는 완료가 아니다. W3는 neutral resource와 strict exit 구현을 0.5일
기성으로 반영한다. W7의 normal/tangent와 UV 선택·변환 구현·GPU 검증은 합계 1일 기성이다.
W2/W4/W5/W6 완료 8.5일 + 진행 기성 3일이며 잔여는 6.5일이다.

---

## 4. 슬라이스별 완료 조건

### PBR-W0 — 관측 기준선

- Gunner helmet/armor, Prim cube/sphere/cylinder, alpha mask, AO, emissive-only, non-uniform scale
  fixture를 같은 카메라·광원·HDRI로 고정한다.
- pre-tone linear HDR, GBuffer attachments, material/texture/sampler/descriptor generation,
  PSO key를 한 artifact로 남긴다.
- 콘솔/회귀 명령은 실패 수가 1 이상이면 프로세스 종료 코드도 실패여야 한다.

### PBR-W2~W3 — 언어·backend 단일화

- Forward 제품 PBR도 `.slang`을 정본으로 사용한다.
- GBuffer/Deferred/Forward가 중복 BRDF·IBL 수식을 각자 소유하지 않는다.
- neutral base color/normal/ORM/emissive/AO의 논리 값은 backend와 무관하며 fixture가 직접
  숫자로 검증한다.

### PBR-W4~W7 — 재질 의미

- `MASK`는 color/depth/shadow에서 같은 cutoff를 사용하고 `BLEND`와 구분된다.
- `doubleSided`는 cull과 뒷면 normal 처리를 함께 결정한다.
- AO는 별도 semantic으로 `lerp(1, ao, occlusionStrength)`에 도달한다. ORM의 R을 저작
  근거 없이 AO로 간주하지 않는다.
- texture가 없어도 emissive factor/strength가 0이 아니면 constant emission이 살아 있다.
- base color/emissive는 색 데이터, normal/metal/rough/AO는 비색 데이터로 업로드한다.
- UV set/transform과 sampler state가 material snapshot에 포함되며 mip chain이 실제 생성·소비된다.
- normal은 inverse-transpose, tangent/bitangent는 선형 변환을 사용하고 handedness를 보존한다.

### PBR-W8~W9 — 플리커와 제품 cutover

- frame 안에서 material/texture/sampler/descriptor/PSO generation이 섞이지 않는다.
- reload/재임포트 실패 시 마지막 정상 generation을 유지하고 부분 게시하지 않는다.
- 10분 회전·카메라 이동·재임포트 중 검정/변색 frame 0, validation error 0.
- DX12/Vulkan 모두 Gunner/primitive golden 허용 오차를 통과한다.
- 제품 PBR HLSL fallback, silent neutral substitution, 실패를 성공으로 반환하는 gate 0건.

---

## 5. 비범위와 후속 인계

- Blender Principled/OpenPBR, Material Graph, coat/sheen/transmission/SSS는 PHASE 4.25.
- local reflection probe, shadow atlas, AgX/auto exposure/bloom은 PHASE 4.75.
- RenderGraph scheduling, generic Pipeline/Pass Shader Graph, GPU-driven/DXR/DLSS는 PHASE 4.75.
- 모델 GUID/sidecar/importer identity를 다시 해석하지 않는다. PHASE 3.75의 typed generation만
  입력으로 받는다.

---

## 6. W0/W3 첫 구현 — 2026-09-06

### 구현

- `render.pbr.capture <새 절대 디렉터리> [game|editor]`는 요청 뒤 발행된 실제 제품 frame을
  기다린다. 임의의 GT `wait N`을 GPU 완료로 간주하지 않는다.
- 해당 frame의 GBuffer 4장, depth, pre-tone HDR, 최종 display를 float32 원본으로 저장한다.
  `manifest.json`에는 camera, 선택 광원, HDRI 경로, model/mesh ID와 generation,
  ShaderMeta generation/permutation, property bytes, texture GUID/register/runtime identity,
  graph 통계, finite 판정과 GPU validation 메시지를 기록한다.
- Vulkan ORM neutral을 `(1,1,1,1)`로 수정했다. 누락된 MR texture는 authored metallic/roughness
  factor에 곱하는 중립값이어야 한다. 양 backend의 실제 white/ORM/black GPU readback을
  숫자로 검사하며, GBuffer fixture의 AO 미저작 기대값도 `occlusionStrength` 자체에서 `1`로 바로잡았다.
- 기존 DX12/Vulkan/livecheck의 56개 결과 판정은 실패 시 종료 코드 `7`을 남긴다.
  새 capture도 잘못된 경로·timeout·readback/write/validation 실패를 종료 코드로 전파한다.
- `verify-model-render-wiring.ps1`은 제품 `GBuffer.slang`을 정적 검사한다.

### 실행과 판정 범위

```powershell
pwsh Tools/regression/verify-pbr-wiring-baseline.ps1
```

새 스크립트는 기존 Editor가 없는 상태에서 실행한다. 각 backend를 별도 프로세스로 부팅해
`FT_Primitives`와 Gunner 배치 frame을 저장하고, 기존 설정 파일의 원본 bytes는 `finally`에서
복구한다. 실제 model draw·sealed property·attachment 크기·finite/depth/HDR·validation과
프로세스 종료 코드를 함께 검사한다. 양 backend 수치 비교 harness는 별도의 DX12 host에서
실행하며, 실패 뒤 성공해도 종료 코드가 유지되는 경우와 기존 capture 디렉터리 거부도 검사한다.

VS18/v145 CreatorEditor Debug x64 빌드와 cutover 동결 정적 검사는 통과했다. DX12/Vulkan
각각 primitive 8 draw, Gunner 포함 10 draw의 제품 capture를 저장했고 validation은 모두 0건이었다.
이는 화면 저장과 관측 경로의 검증이며 W9의 golden/장시간 판정이 아니다.

최종 `verify-pbr-wiring-baseline.ps1`은 종료 코드 0으로 통과했다. 제품 capture 4개,
`vk.gbuffer`/`vk.forward` 양 backend 수치 대조, 실패 후 성공 명령의 종료 코드 7 유지,
기존 출력 디렉터리 거부의 종료 코드 7을 확인했다. 모든 실행의 stderr는 비어 있었다.
로컬 결과는 `%TEMP%/creator-pbr-phase4/creator-pbr-a9d4563a41f045c684547851b710f3e1`에 남겼다.

동일 fixture의 관측 비교에서 primitive baseColor/metalRough/normal은 정확히 일치했다.
pre-tone HDR의 전체 RGBA RMSE는 primitive 0.001055, Gunner 0.001033이었다.
Gunner normal의 일부 경계 sample에는 큰 차이가 있어 최대 오차를 숨기지 않는다
(최대 0.867981, 전체 RMSE 0.000346). 이 값으로 W9 허용 오차를 사후 정의하거나 통과 처리하지 않는다.
이 관측 비교의 원본은 직전 capture 실행의
`%TEMP%/creator-pbr-phase4/creator-pbr-15ae1c77213a40058abb337d922b3e12/comparison.json`이다.

### 남은 완료 조건

- sampler identity, descriptor generation, resolved PSO key는 현재 draw snapshot에 없어
  manifest의 `missing`에 명시한다. material 식별자, 전체 tuning/asset fingerprint까지 포함한
  재현 계약도 후속 보강 대상이다.
- Gunner helmet/armor 근접 fixture와 alpha/AO/emissive-only/non-uniform scale acceptance,
  backend 간 golden 허용 오차, 10분 이동·회전·재임포트는 아직 완료하지 않았다.
- Vulkan host에서 DX12 비교 장치를 추가 생성하는 혼합 harness 실행 중 장치 리셋을 1회
  관측했다. 제품 Vulkan capture와 구분해서 기록하며, host 조합의 안정성이 해결됐다고 주장하지 않는다.
- 이 첫 구현 다음의 W2 결과는 §7에 기록한다. 이후 순서는 W4~W7 재질 의미, W8 원자 밀봉, W9 cutover다.


## 7. W2 native Slang 공용 평가 — 2026-09-06

### 구현

- Forward 제품 진입점 세 개를 `ForwardShade.slang`, `ForwardWater.slang`,
  `ForwardWind.slang`으로 전환했다. ShaderMeta source, bootstrap compiler 호출,
  model-render 정적 검사를 함께 갱신하고 소비자가 없어진 세 HLSL 파일을 제거했다.
- `Includes/MaterialEvaluation.slang`이 GBuffer/Forward의 base color·MR·emissive
  factor 곱과 tangent-space normal 평가를 소유한다. 기존 binding 번호, property block,
  keyword/model vertex/skinning 축, Water/Wind의 flow·emission 계산은 유지한다.
- `Includes/PbrSurface.slang`과 기존 `Ibl.slang`이 Deferred/Forward의 GGX 직접광,
  다중 산란 보상, IBL 평가를 공유한다. Forward의 거칠기 프레넬·다중 산란 누락을
  Deferred 기준으로 맞췄으므로 해당 Forward 조명 응답은 의도적으로 바뀐다.
- IBL 세트가 없으면 BRDF LUT를 읽지 않고 DFG=(1,0)을 사용한다. 이전 Deferred는
  null LUT의 (0,0)에서 직접광 보상을 크게 만들 수 있었다. 점광/스포트 감쇠와 분모
  하한도 공용 함수로 묶었다.
- 퇴화한 tangent는 GBuffer와 같은 기하 법선 fallback을 사용한다. 비균등 스케일
  inverse-transpose, alpha, AO semantic, constant emission 개선은 후속 W4~W7 소유다.

### 검증

- VS18/v145 `CreatorEditor` Debug x64 빌드 통과.
- `render.pbr.parity`: 실제 GBuffer→Deferred와 Forward reference 경로의 pre-tone HDR을
  32×32 타깃 중앙 16×16 RGB에서 비교한다. 금속성/거칠기 0·1, 중간값, 퇴화 tangent를
  무광원/방향광/점광/스포트/IBL/방향광+IBL로 조합한 36개 case를 각 backend에서 실행했다.
  경로 간 및 DX12/Vulkan 간 최대 채널 편차 0, validation 0.
  허용치는 절대 0.002 + 상대 0.5%이며 검정/유광 응답, 무 IBL 밝기 상한,
  퇴화 tangent의 기하 법선 보존을 별도 단정한다.
- 중립 DFG를 (0,0)으로 임시 오류 주입한 경우 두 경로가 같은 오류를 내더라도
  무 IBL 밝기 상한 검사가 실패하고 exit 7을 반환했다. 소스 원복 후 강화된 36-case
  검사는 다시 편차 0·validation 0·exit 0으로 통과했다.
- `verify-pbr-wiring-baseline.ps1` 통과: DX12/Vulkan 제품 primitive/Gunner 캡처,
  `vk.gbuffer`, `vk.forward`, `vk.deferred`, PBR parity, 음성 종료 코드 검사 포함.
  기존 GBuffer 48B property 및 Forward primary/Water/Wind packet·flow·texture owner·
  generation 교체 검사가 그대로 통과했다. 네 제품 캡처는 7 attachments, finite,
  validation 0이며 primitive 8/Gunner 10 draw다.
- 이전 W0 기준과 camera/light가 같은 네 제품 캡처의 GBuffer 4종과 depth는 바이트 일치.
  primitive HDR도 일치한다. Gunner HDR RGBA의 이전 기준 대비 RMSE는 DX12 0.001508,
  Vulkan 0.001068, 최대 편차는 0.052246/0.054688이었다. `Scene.LitColor` 캡처는 SSGI 등
  후속 합성을 포함하므로 이를 순수 Deferred BRDF 차이나 시각 동등성으로 단정하지 않는다.
- 근거: `%TEMP%/creator-pbr-phase4/build-w2-final.log`,
  `creator-pbr-ce582b4ba411420da605ecbe463699a3/`, `w2-final-checks/`.

W2의 native Slang/공용 평가 완료와 W9의 실장면 golden·시간 이력·장시간 안정성 판정은
별개다. 이번 제품 캡처에는 Forward draw가 없으며, Forward는 별도 GPU fixture와
기존 ShaderMeta/Water/Wind 검사를 근거로 한다. 다음 착수는 W4 alpha/cull 계약이다.


## 8. 2026-09-06 W4 — alpha mode와 양면 coverage

W4 구현과 아래 회귀를 완료했다. W5의 AO·texture table 배선이 다음 착수 범위다.

- `Opaque=0`, `Transparent=1`의 기존 값을 유지하고 `Masked=2`를 추가했다. importer,
  ModelDraft, material authoring/override, runtime generation, Material bridge가 MASK를 보존한다.
  `doubleSided`는 bool property와 runtime field로 왕복하며 false override도 유지한다.
- coverage 정책을 소유 snapshot에 밀봉한다. MASK의 cutoff와 base alpha는 reflection으로
  패킹한 property bytes에서 읽어 Shadow에도 전달한다. `alphaCutoff`의 CB sentinel 용도는
  명시적 `usePropertyBlock`으로 대체했다. 재질 CB prefix 48B는 유지하고 GBuffer instance는
  112B, Forward instance는 기존 144B, Shadow instance는 기존 80B다.
- 공용 `PbrCoverage.slang`이 OPAQUE/MASK 출력 alpha=1, MASK의
  `baseColorFactor.a * texture.a * COLOR_0.a >= cutoff`, BLEND alpha 보존을 처리한다.
  BLEND는 불투명 큐와 Shadow caster에서 제외한다.
- GBuffer/Forward/Shadow의 고정 cull은 None이며, 픽셀 셰이더가 인스턴스별 single-sided
  뒷면을 버린다. double-sided 뒷면은 그리면서 최종 normal을 반전한다. 이 방식은 서로 다른
  양면 정책의 인스턴싱을 허용하지만 hardware backface culling의 성능을 보장하지 않는다.
- Shadow를 native `Shadow.slang`으로 전환했다. UV0·선택적 COLOR alpha와 MASK base texture를
  소비하고 alpha texture별로 배치를 나눈다. OPAQUE에도 coverage pixel stage/UV 입력이 붙는다.
  SPIR-V location과 입력 선언 순서를 일치시켜 DX12/Vulkan 그림자 차이를 수정했다.
- Forward의 재사용 upload memory를 초기화해 legacy fixture의 미정의 flow 값이 UV를
  바꾸는 문제도 수정했다. 실제 소유 snapshot 검사는 legacy alpha/texture 값을 오염시켜
  소유 bytes와 texture owner의 사용을 확인한다.

### 캐시 이행

CEMC는 6으로 올렸다. 구 캐시는 MASK와 doubleSided 의미를 이미 잃었으므로 기존 bytes를
해석해 복원하지 않는다. 최신 AssetCooker의 `--author-model-asset`로 현재 14개 모델을 다시
게시했다. 14개 모델·310개 하위 자산 ID를 보존했고 source sidecar의 generation을 갱신했다.
기존 사용자 변경을 포함한 갱신 전 sidecar는 `%TEMP%/creator-pbr-phase4/w4-model-meta-before`에
보관했다. 다른 checkout에서도 새 AssetCooker로 모델 generation을 재생성해야 한다.

### 검증과 남은 범위

- VS18/v145 CreatorEditor Debug x64 및 AssetCooker 빌드 통과.
- `render.pbr.coverage`: 두 backend 각각 40-case. legacy/소유 material, 정적/항등 skin palette,
  alpha 0 OPAQUE, cutoff 미만/동일/0, alpha texture 구멍, single/double 뒷면, BLEND를 검사했다.
  GBuffer 색·깊이·법선, Forward 색, 첫 Shadow cascade coverage를 실제 GPU에서 읽었다.
  skin 분기는 GBuffer/Shadow에서 확인하며 애니메이션 변형 시각 acceptance는 W9에 남는다.
  백엔드 최대 편차 0, validation 0.
- `experiment.matseal`, `matcodec`, `matmigrate`, `cooked` 통과. 잘못된 mode, non-bool/중복
  doubleSided, 잘린 bytes, 비유한/범위 밖 cutoff를 거부한다. 기존 material migration fixture는
  현재 모델 신원 계약에 맞게 UUIDv4에서 UUIDv8로 바로잡았다.
- MASK clip 우회 주입은 GBuffer의 cutoff 미만 픽셀에서 실패하고 종료 코드 7을 반환했다.
  주입을 원복한 뒤 양 backend coverage가 통과한다.
- strict GUID 검사: invalid/duplicate 0, 하위 자산 310. 전체 model cook gate: 14 generation
  closure와 authoring transaction의 실패 주입 5개·collision 1개 통과.
- 현재 UV0/linear-wrap 계약 안의 coverage 일치다. UV set/transform/sampler/mip은 W7,
  실장면 golden·10분 회전·카메라 이동·재임포트 acceptance는 W9에 남는다.

근거: `%TEMP%/creator-pbr-phase4/`의 `build-w4-complete.log`, `build-w4-cooker.log`,
`w4-guid.log`, `w4-cook-all-short.log`, `w4-probe-ab62b2c2be984793bd66fe5b6d338df4`
(40-case 및 Shadow), `w4-probe-39ebcb68c9db4639bcbd21ced7e6f53c` (MASK 실패 주입).

전체 `verify-pbr-wiring-baseline.ps1`도 통과했다. W2 36-case HDR 비교, 기존 Shadow/GBuffer/
Forward/Deferred, material 계약, W4 40-case, strict 실패 종료와 capture 충돌 거부를 함께
실행했다. 실제 primitive·Gunner는 DX12/Vulkan 각각 7 attachment를 캡처했고 validation 0,
finite=true다. 산출물은 `creator-pbr-b9dc26e9f2544f5d87228edc4f1d6ae3`, 요약은
`w4-baseline.log`다. 이 캡처를 W9 golden 승인으로 세지 않는다.

최종 `w4-probe-0defc8b3eed84cff8f1e2d8961ca89b8`는 GBuffer 출력 alpha=1 단정도 포함해
40-case가 양 backend에서 통과했다(기대값 최대 오차 0, backend 편차 0, validation 0).
실장면 manifest에서도 모든 draw의 coverage 정책이 활성화됐으며 primitive 8개는 flags=1,
Gunner 포함 10개는 flags=1/9(OPAQUE single/double-sided)로 밀봉됐다.

`verify-model-render-wiring.ps1` 최종 재실행도 exit=0으로 통과했다(`w4-wiring-final.log`).
DX12 skinning, 두 backend의 Shadow/GBuffer/Forward, typed generation upload, SU 84B/64/68
및 4개 vertex mask PSO를 확인했다.


## 9. 2026-09-06 W5 — AO와 reflection texture table

W5 구현과 양 backend GPU fixture를 완료했다. 다음 착수는 W6의 emissive
factor/strength·constant-only emission·색공간 계약이다.

- `aoMap.R`과 `occlusionStrength`를 공용 `EvaluatePbrOcclusion`의
  `lerp(1, ao, strength)`로 평가한다. GBuffer는 기존 MetalRough.R에 쓰고
  Deferred/Forward는 IBL에만 적용한다. 직접광과 발광에는 곱하지 않는다.
- 누락 AO는 linear white로 중립 1이다. metallicRoughness.R은 AO로 추정하지 않는다.
  같은 이미지를 MR과 AO로 쓰려면 두 semantic에 각각 저작되어야 한다. importer의
  AO linear 분류와 기존 material/cache 데이터는 그대로 소비하므로 CEMC 버전은 유지한다.
- 공용 `MaterialTextureTable`이 reflection으로 descriptor 길이·owner 순서를 정한다.
  두 pass의 고정 4개 key/view 배열과 중복 검증을 제거했다. 재질 텍스처는 t16부터,
  프레임/IBL/instance/bone은 기존 t0..t15에 둔다. 현재 RHI의 space0와 단일 Texture2D
  계약 안에서 t127까지 허용하며 배열 텍스처와 다른 space는 거부한다.
- ShaderMeta는 셰이더에 선언한 모든 재질 texture를 포함해야 한다. 이름·register·space·
  owner 개수의 불일치와 중복을 거부한다. 비연속 register의 빈 칸은 null descriptor로
  초기화하고 기본 texture는 슬롯 번호 대신 property 의미로 고른다.
- 서로 다른 테이블 길이의 material variant도 candidate-first로 준비한다. PSO 전환 뒤
  frame/light/tile/IBL/palette를 다시 바인딩해 root layout 변경으로 생기는 미초기화를 막는다.
  sampler/UV/mip 확장과 descriptor generation 원자 밀봉은 W7/W8에 남는다.

### GBuffer packing 결정

AO는 기존 채널을 사용하므로 MRT 수와 포맷은 유지한다. 네 RGBA16Float(32B),
R32Uint bitmask(4B), D32 depth(4B)의 합은 pixel당 40B, 1920×1080에서 82,944,000B다.
이는 논리적인 타깃 저장량이며 압축·읽기·대역폭·실측 GPU 시간의 수치가 아니다.

MetalRough만 RGBA8Unorm으로 줄이면 저장량은 pixel당 4B(1080p 약 7.91MiB) 줄지만,
양자화가 roughness 하이라이트와 SSR에 전달된다. Decal도 이 타깃을 복사·혼합한다.
네 제품 캡처의 geometry 픽셀(MetalRough alpha=1)에 대한 CPU RGBA8 변환 추정에서
ORM RGB 최대 오차는 primitive 0.001863, Gunner 0.001961이고 RMSE는 약 0.00106이다.
AO owner가 없는 이 장면들의 AO는 모두 1이었다. 이 추정은 GPU 포맷 변경·시간 측정·
하이라이트 golden 비교가 아니므로 포맷 축소의 근거로 충분하지 않다. W5는 기존
채널의 AO/MR 독립성을 검증하고 포맷 유지로 마감하며, 축소는 별도 성능·정밀도 검증을
갖춘 최적화 변경으로 다룬다.

### 검증

- VS18/v145 CreatorEditor Debug x64 빌드 통과.
- `render.pbr.occlusion`: backend별 48-case, 매 case에 5/8/5 descriptor 테이블의
  세 draw를 배치했다. 여섯 texture·두 빈 칸·재배치한 base/emissive/AO register,
  뒤집은 ShaderMeta/owner 순서, 누락 schema 거부와 기존 variant 보존을 확인했다.
- AO 미지정·white·strength 0/0.5/1·별도 R=64 이미지·MR/AO 동일 이미지 명시 공유·
  MR.R=0만 있고 AO는 없는 경우를 무광원/방향광/점광/스포트/IBL/방향광+IBL에서 비교했다.
  GBuffer AO/R/M/alpha 값, 직접광·발광 불변, 중립 AO 동등, AO=0의 ambient 제거를 단정했다.
  Forward/Deferred 및 DX12/Vulkan 간 최대 HDR 채널 편차 0, validation 0.
- 제품 primitive/Gunner 네 캡처는 모든 draw에 t16 이상 AO binding이 밀봉됐다.
  이 장면들은 AO owner가 0개이므로 AO texture의 비중립 응답 근거는 위 GPU fixture다.
  제품 capture를 W9 golden·10분·재임포트 acceptance로 승격하지 않는다.

근거: `%TEMP%/creator-pbr-phase4/build-w5-complete.log`,
`w4-probe-ef773a70b5c2429aa7ce26b294263fcd/w5-ao.stdout.txt`,
`creator-pbr-8589be15c120456c8465a4c2eeec5afb/`, `w5-packing.json`.

`verify-pbr-wiring-baseline.ps1` 전체도 통과했다(`w5-baseline.log`). 실제 네 장면 캡처,
Shadow/GBuffer/Forward/Deferred, W2 36-case, W4 40-case, W5 48-case, material 계약 네 명령,
실패 종료 코드와 capture 충돌 거부를 함께 확인했다. 세 PBR fixture의 경로·backend 비교 최대 편차는 0이다.

AO 평가를 중립 1로 고정하는 오류를 임시 주입하면 strength=0.5 case의 GBuffer 값이
기대 0.5 대신 1이 되어 실패하고 exit 7을 반환했다. 원본 shader bytes를 복원했다.
근거: `w4-probe-f8e03088029a444cbe6739ca9cffbc72/w5-negative-ao.stdout.txt`.

최종 추가 검사에서 texture cache가 없는 숫자-only Forward fixture의 빈 table 바인딩을
복원했다. `dx12.forwardshade`의 일반/참조 16,384픽셀이 완전히 일치하고 Water/Wind·
다음 frame의 windTint 변경도 통과한다. 복원한 shader의 AO 48-case도 양 backend 편차 0이다.
해당 Forward 검사를 baseline script에 추가했다. 근거:
`w4-probe-1aca50c2103145a19affb833dbbd16db/w5-final-ao-forward-fixed.stdout.txt`.

`verify-model-render-wiring.ps1`도 최종 코드에서 exit 0으로 통과했다(`w5-model-wiring.log`).
typed generation upload 1/1, vertex mask 네 종류 PSO, SU 84B/64/68, DX12 GBuffer/skinning과
Vulkan Shadow/GBuffer/Forward, validation 0을 확인했다. 설정 파일은 원본 bytes로 복원했다.


## 10. 2026-09-06 W6 — 발광 색·강도·색공간

W6를 완료했다. 다음 착수는 W7의 UV set/transform·sampler/mip·비균등 스케일 법선 계약이다.

- 가져오기가 `emissiveStrength`를 표준 float property로 보존하고, ShaderMeta·저장/복원·
  소유 material bytes를 거쳐 공용 `EvaluatePbrMaterial`에 전달한다. 최종 선형 발광은
  `texture.rgb * emissive.rgb * emissiveStrength`이며 강도 1을 넘는 HDR 값을 보존한다.
- 소유 snapshot에서 없는 발광 texture는 white다. 기본 발광 색은 0, 기본 강도는 1이므로
  아무 값도 저작하지 않은 재질은 빛나지 않고, texture가 없는 상수 발광은 살아 있다.
  숫자만 공급하는 Forward 경로도 같은 규칙을 쓴다. 격리된 legacy draw fixture는 기존의
  texture-only/black fallback을 유지한다. Water/Wind 추가 발광도 평가된 발광 값에 비례한다.
- GBuffer/Forward/Water·Wind의 reflected b2는 각각 64/96/112B다. 기존 48B 표준 prefix와
  Water/Wind·flow offset을 유지하고 강도를 tail에 추가했다. GBuffer MRT 포맷은 바꾸지 않았다.
- source/cooked 외부 texture 로더가 TextureReference의 색공간을 받는다. 표준 legacy 재질과
  외부 model texture handle도 baseColor/emissive는 sRGB, normal/MR/AO는 linear로 복원한다.
  캐시는 전체 경로·압축 정책·색공간을 구분하며 같은 이름의 다른 파일이 충돌하지 않는다.
- `Texture::WithColorSpace`는 원본 픽셀을 변환하지 않고 sampling format을 정한다. 포맷이
  달라지면 CPU 픽셀 owner를 공유하는 별도 GPU 신원을 만들고, 같은 포맷이면 기존 owner를
  반환한다. RGBA/BGRA8·BC1·BC3의 sRGB/linear를 지원하며 float HDR 이미지는 선형으로 둔다.
  BC3 sRGB enum은 뒤에 추가해 기존 저장 format 번호를 유지했다. embedded generation은
  기존의 encoded bytes 보존·색공간 라벨 계약을 유지한다. 같은 embedded ID의 서로 다른
  색공간 참조를 authoring에서 거부하는 기존 제한을 완화하지 않았다.
- 이전 캐시는 발광 강도를 잃었으므로 CEMC7로 갱신했다. 모델 14개를 재게시하고 모델 ID와
  하위 자산 ID 310개를 보존했다. 이전 sidecar는 검증 작업 디렉터리에 보관했다.

### 검증

- VS18/v145 CreatorEditor 및 AssetCooker Debug x64 빌드 통과.
- `render.pbr.emission`: backend별 78-case. 13개 재질 조건을 무광원/방향광/점광/스포트/
  IBL/방향광+IBL과 조합했다. 매 조건에서 5/8/5 texture table을 사용하는 세 draw를 그린다.
  미지정·texture만 지정·상수 발광·강도 0/1/8/32·black/white·비중립 linear/sRGB·
  sRGB→linear 재사용·0 성분·BC3 sRGB를 검사한다.
- GBuffer 발광을 CPU 예상식과 대조하고, 두 HDR 경로에서 발광이 조명에 더해지며 AO=0에도
  감쇠하지 않는지 검사한다. 최대 경로 편차는 DX12/Vulkan 모두 0.000976562,
  backend 간 편차 0, validation 0이다. GBuffer half 저장의 반올림 허용 범위 안이다.
- 발광 강도 곱셈 제거 오류를 주입하면 case 3의 R이 기대 2 대신 0.25가 되어 exit 7로
  실패한다. shader는 원본 bytes로 복원했고 이후 전체 baseline을 통과했다.
- 실제 외부 texture 로드에서 색공간·encoded bytes 보존·별도 GPU 신원·동일 경로/역할 캐시
  재사용·같은 파일명 격리를 검사했다. material resolve 합성 44/44, 실사 12/12 통과.
  importer 강도 8 보존과 CEMC7 왕복도 검사했다.
- `verify-pbr-wiring-baseline.ps1` 전체 통과: 제품 캡처 네 개, Shadow/GBuffer/Forward/Deferred,
  숫자-only Forward/Water/Wind, W2 36-case·W4 40-case·W5 48-case·W6 78-case,
  재질 계약 다섯 명령, 실패 종료 코드와 capture 충돌 거부를 포함한다.
- 제품 primitive/Gunner는 각각 8/10개 draw이며 b2 64B·강도 1·발광 색 0·발광 RGB 최대 0을
  확인했다. 발광의 비중립 응답 근거는 GPU fixture다. 이 캡처를 W9 golden·10분 acceptance로
  승격하지 않는다.
- strict GUID: meta 243·하위 자산 310, invalid/duplicate/missing 0.
  cook-all: 모델 14개 generation closure와 authoring transaction의 실패 주입 5개·충돌 1개 통과.
  `verify-model-render-wiring.ps1` 통과. Editor 설정은 원본 bytes로 복원했다.

근거: `%TEMP%/creator-pbr-phase4/build-w6-complete.log`, `build-w6-cooker.log`,
`w6-baseline.log`, `creator-pbr-8a8ffe4cbc194fd2804e2e670d1c097d/`,
`w6-product-emission.json`, `w6-negative.log`, `w6-guid.log`, `w6-cook-all.log`,
`republish-w6.log`, `w6-model-wiring.log`.


---

## 11. W7 첫 단위 — 비균등 스케일 normal/tangent, 2026-09-06

아래는 첫 단위 완료 시점의 기록이다. UV 후속 구현·검증은 §12에 기록한다.

W7은 진행 상태다. normal/tangent 변환과 GPU 검증을 0.5일 기성으로 반영한다.
UV set/transform·sampler/mip 전달은 다음 구현 단위이며 W8은 그 뒤에 착수한다.

### 구현

- GBuffer와 Forward/Water/Wind의 공용 Slang 평가에 `TransformPbrFrame`을 추가했다.
  normal은 역전치, tangent/bitangent는 선형 변환으로 처리한다. 픽셀 단계의 직교화와
  저작 tangent.w/bitangent의 handedness 판정은 기존 공용 TangentFrame을 사용한다.
- 스키닝은 가중 bone 행렬과 world를 먼저 합친다. 변형 전 normal/tangent/bitangent를
  한 번 변환하므로 비균등 bone 스케일과 음수 determinant에서도 종법선 방향을 보존한다.
  typed static/skin 경로와 기존 GBuffer legacy skin 경로에 적용했다.
- 행렬의 최대 절댓값으로 균등 크기를 제거해 작은 물체의 determinant와 tangent 길이가
  underflow/퇴화 판정에 걸리지 않게 했다. 정규화된 행렬의 determinant 절댓값이
  1e-8 이하이면 선형 변환한 기하 법선을 유한한 fallback으로 쓰고 tangent perturbation을
  끈다. 이것은 특이/거의 특이한 변환의 예외 처리이며 유일한 올바른 역전치라는 뜻은 아니다.
- material/instance ABI와 GBuffer 포맷을 유지했다. 저장 형식·CEMC·자산 재게시는 필요 없다.

### 검증

- VS18/v145 CreatorEditor Debug x64 빌드 통과.
- `render.pbr.transform`: DX12/Vulkan 각각 128-case. 8개 행렬 조건(항등, Z×2, X×2,
  Z→X 전단, Z 음수 스케일, 1e-5 균등 스케일, Z=0, Z=1e-10) × typed static/skin ×
  4개 normal-map/tangent 조건 × 방향광·방향광+IBL을 조합한다.
- skin은 두 bone의 가중 결과가 Z=-2가 되게 한다. normal-map 미사용, 양/음 handedness,
  0 tangent를 포함한다. 실제 제품 PSO/packed vertex/bone upload를 거치고 GBuffer 법선을
  별도의 평면 방정식으로 계산한 CPU 값과 대조한다. Forward/Deferred pre-tone HDR도 비교한다.
- 최대 HDR 경로 편차는 양 backend 모두 0.000244141, backend 간 편차는 0.0000305176,
  validation 오류 0이다. normal attachment의 허용 오차는 half 저장을 고려한 0.0015다.
- 선형 normal 변환 오류를 주입하면 case 4의 X가 기대 0.83205 대신 0.350586이 되어
  exit 7로 실패한다. Forward에서만 skin frame 합성을 누락하면 같은 조건의 HDR이
  Deferred 0 / Forward 0.107422로 갈려 exit 7로 실패한다. 두 shader 모두 원본 bytes로 복원했다.
- 전체 `verify-pbr-wiring-baseline.ps1` 통과: 두 backend의 primitive/Gunner 제품 캡처,
  기본 패스·Forward/Water/Wind, W2 36·W4 40·W5 48·W6 78·W7 128-case,
  재질 계약 다섯 명령과 실패 종료/캡처 충돌 검사를 포함한다.
- `verify-model-render-wiring.ps1` 통과: typed upload 1/1, 네 가지 vertex mask PSO,
  SU 84B 레이아웃과 bone offset 64/68, DX12 skinning 및 Vulkan 패스 연결을 확인했다.
  validation 오류 0, Editor 설정 원본 bytes 복원, `git diff --check` 통과.

### 다음 단위의 확인된 경계

- `ImportedScene::TextureSlot`의 UV set/offset/tiling/wrap 정보가 `SceneToModelDraft`의
  texture property 생성에서 보존되지 않는다. glTF sampler/texture transform의 가져오기와
  typed reference·저장/복원·material snapshot을 함께 연결해야 한다.
- 제품 셰이더의 UV0 고정 sampling, 재질별 sampler 연결, mip chain 생성·소비가 남아 있다.
  이 항목이 끝나기 전에는 W7을 완료로 표시하지 않는다. W9 golden·10분 회귀도 별도다.

근거: `%TEMP%/creator-pbr-phase4/build-w7.log`, `w7-transform.log`,
`w4-probe-73ac208fce504acd93c35c0db783d8e6/w7-transform.stdout.txt`,
`w7-negative.log`, `w7-forward-negative.log`, `w7-baseline.log`,
`creator-pbr-07159716eef64ad79f6d18937c993aea/`, `w7-model-wiring.log`.

---

## 12. W7 두 번째 단위 — UV0/UV1 선택·텍스처별 변환, 2026-09-06

W7은 진행 상태다. §11의 normal/tangent와 이번 UV 전달을 합쳐 기성 1일로 반영한다.
재질별 sampler와 mip 생성·소비가 남아 있으며, W8은 그 뒤에 착수한다.

### 구현

- `KHR_texture_transform`의 UV set override·offset·scale·rotation을 가져와 텍스처 참조에
  보존한다. 변환식은 `offset + rotation * scale * selected UV`이며 UV0/UV1을 지원한다.
  [glTF 확장 규약](https://github.com/KhronosGroup/glTF/tree/main/extensions/2.0/Khronos/KHR_texture_transform)을 따른다.
  같은 parser에서 `KHR_materials_emissive_strength`도 활성화하고 실제 glTF fixture로 확인했다.
- 좌표 값은 이미지/cache 신원과 분리된 texture reference에 속한다. source draft → CEMC8 →
  typed model material → 저작 재질 → immutable snapshot으로 전달한다. legacy 재질 저장/복원
  브리지도 값을 보존하며, UV 필드가 없는 기존 저작 데이터는 UV0·항등 변환으로 읽는다.
  UV2 이상과 유한하지 않은 값은 거부한다. 없는 UV1을 요구하는 draw도 거부한다.
- GBuffer/Forward의 b3에 실제 texture register 순서로 변환을 올리고, 표준 5개 texture가
  각자 좌표를 선택한다. reflection의 빈 slot도 같은 순서를 유지한다. 이미지와 숫자 재질이
  같아도 UV 값이 다르면 서로 다른 배치가 된다. Shadow는 baseColor 좌표를 instance에 싣는다.
- core/color/skin/color+skin 각각의 UV1 변형을 추가해 8개 typed vertex mask를 지원한다.
  GBuffer의 고정 4개 PSO 필드를 mask별 저장소로 바꾸고 전체 후보가 성공한 뒤 게시한다.
  속성 없는 bootstrap의 빈 layout 계약을 유지하고 공유 중인 PSO는 퇴거 후보에서 제외한다.
  UV1 normal texture의 누락 tangent 생성에는 UV1을
  사용한다. 저작 tangent는 유지하고, texture transform은 sampling 좌표에 적용한다.
- b2 재질 bytes와 GBuffer 포맷은 유지한다. CEMC는 7→8이며 모델 14개를 재생성했다.
  모델 ID와 하위 자산 ID 310개를 모두 보존했다.

### 검증

- VS18/v145 CreatorEditor와 AssetCooker Debug x64 빌드 통과.
- `render.pbr.uv`: DX12/Vulkan 각각 64-case. 8개 UV 조건 × core/color/skin/color+skin의
  UV1 변형 × 방향광·방향광+IBL이다. 서로 다른 UV0/UV1, 텍스처별 변환, 음수 scale,
  양/음 rotation과 5/8/5 texture table을 조합한다. 같은 이미지·숫자 재질·geometry에서
  좌표만 다른 draw를 함께 그려 배치 병합으로 값이 섞이지 않는지 확인한다.
- GBuffer baseColor/MR/AO/emission/normal을 CPU의 affine·bilinear-wrap 예상값과 대조했다.
  최대 HDR 경로 편차는 양 backend 모두 0.000244141, backend 간 편차 0, validation 0이다.
  GPU 결과 비교는 현재 linear-wrap·단일 mip 조건이며 sampler/mip 완료 근거가 아니다.
- 실제 glTF의 texture transform override와 emissive strength, UV1 tangent 생성 및 draft
  전달을 확인했다. material codec 51/51, migration 합성 24/24·실사 26/26,
  seal 35/35, cooked 합성 466/466을 통과했다. 기존 UV 미지정 데이터와 잘못된 좌표도 검사한다.
- `render.pbr.coverage`: 양 backend 40-case 중 owned 20개를 변환된 UV1으로 바꿨다.
  다른 UV0 값을 함께 넣어 선택 누락을 드러내며 GBuffer/Forward/Shadow의 MASK·양면·
  static/skin 경계를 확인했다. CPU 예상값 및 backend 편차 0, validation 0이다.
- 공용 shader에서 affine 변환을 항등으로 고정하면 첫 case의 AO가 기대 0.375 대신
  0.125가 되어 exit 7로 실패한다. shader는 원본 bytes로 복원했다.
- strict GUID: meta 243·하위 자산 310, invalid/duplicate/missing 0.
  cook-all: 모델 14개 generation closure와 authoring 실패 주입 5개·충돌 1개 통과.
- `verify-model-render-wiring.ps1` 통과: 8개 mask의 stride/속성 수/permutation key,
  SU 84B·bone offset 64/68, typed upload 1/1 및 양 backend 패스 연결을 확인했다.
  속성 없는 bootstrap·기존 ShaderMeta 갱신/퇴거 검사와 Vulkan validation 0을 포함한다.
- 전체 `verify-pbr-wiring-baseline.ps1` 통과: 양 backend의 primitive/Gunner 제품 캡처
  네 개, 기본 패스·Water/Wind, PBR 36·coverage 40·AO 48·emission 78·normal 128·UV 64-case,
  재질 계약 다섯 명령과 실패 종료/캡처 충돌 검사를 포함한다. 최종 발광 경로 편차는
  0.00195312로 half 정밀도 허용 범위이며 backend 편차는 0이다. normal/UV 경로 편차는
  각각 0.000244141이고 validation 오류는 없다. W9 golden·장시간 acceptance는 별도다.
- Editor 설정 원본 bytes 복원과 `git diff --check` 통과. 대시보드 활성 행·공수 합계도
  확인했다(PHASE 4: 10행·18일·완료 8.5·기성 3·잔여 6.5, 통합: 54행·267.5일·잔여 252).

근거: `%TEMP%/creator-pbr-phase4/build-w7uv-complete.log`, `build-w7uv-cooker-final.log`,
`build-w7uv-bootstrap.log`, `build-w7uv-layout-test.log`,
`w4-probe-31cc685efb894f0083a07fa66b702696/w7uv-render.stdout.txt`, `w7uv-negative.log`, `w7uv-baseline.log`,
`republish-w7uv.log`, `w7uv-guid.log`, `w7uv-cook-all.log`, `w7uv-model-wiring.log`,
`creator-pbr-69abb4570ba04ada944c8bfe8310a521/`(최종 baseline 전체 stdout·capture),
`w7uv-doc-check.log`.
