# PBR 배선 안정화 계획 (PHASE 4)

**신설 2026-09-03 · 갱신 2026-09-14 · 10슬라이스 18일 · W1~W6 완료(W3는 §16 · W1은 §17) · W0/W7 진행(W7 sampler는 §19) · W8/W9 구현 착지·부분 실측(§14·§15)**

> **W8/W9 현재 상태 한 줄.** 빌드 exit 0 · `render.pbr.seal` 42/42 · 제품 캡처 W8 단정
> 양쪽 backend PASS · soak 109/109(dx12 1분). 그러나 **cutover 아님**: 배선 게이트가
> 끝까지 간 적이 없고(§15 — `verify-experiment-contract.ps1` 링크 부패, W9 이전부터),
> 교차 백엔드 픽셀 판정은 GBuffer 다섯 장으로 좁혔으며(시각 고정 불가),
> **W8 핵심 수정을 자극하는 fixture가 없어 그 수정은 아직 증명되지 않았다.**

> 2026-09-06 원격 CLI 통합: `render.pbr.*` 검사는 `--commandlet` 또는
> `--commandlet-script`로 실행하고 JSONL terminal 결과로 판정한다. capture 결과는 실제
> 프레임 캡처 완료 뒤에 기록한다. 현재 실패 종료 코드는 `4`이며, 아래의 `7`은 통합 전 실행 기록이다.

> **2026-09-14 인용 정정 (W8 착수 전 실측).** 아래 §6~§13은 그때의 실행 기록이고 지금도
> 그대로 재현되지 않는다. 읽는 사람이 헛돌지 않도록 두 가지를 여기 적는다.
>
> - **`verify-model-render-wiring.ps1`은 더 이상 없다.** `0ec60573`(2026-09-12,
>   "닫힌·완료 계획 소속 Commandlet 68개 은퇴")에서 삭제됐다. §6·§8~§13이 이 게이트의
>   통과를 근거로 들지만 지금 그 이름으로 돌 것은 없다. 제품 `GBuffer.slang` 정적 검사가
>   다시 필요하면 새로 세워야 한다.
> - **실패 종료 코드는 `4`다.** `CommandSession.cpp`의 표가 `Failed`를 4로 옮기며,
>   게이트 스크립트도 4를 기대한다. 저장소의 어느 `.ps1`에도 `ExpectedExit 7`은 없다.
> - **RMSE 수치(§6)는 게이트가 아니었다.** 그 값을 계산하는 코드가 저장소에 없었다 —
>   손으로 한 번 잰 값이다. W9에서 `render.pbr.compare`가 그 자리를 대신한다.

이 계획은 현재 제품 렌더 경로의 `.slang`·머테리얼·렌더러 배선 결함만 닫는다.
Blender형 Material Graph와 Principled 확장은
[`BlenderMaterialGraphPlan.md`](BlenderMaterialGraphPlan.md), **PHASE 4.25**가 소유한다.
RenderGraph·일반 Custom Pass·그림자·reflection probe·후처리는 **PHASE 4.75**로 보낸다.

좌표계 변환 자체는 Blender 시각 동등성의 범위가 아니지만, 현재 제품 결함인 UV set/transform,
sampler/mip, tangent basis와 non-uniform scale normal transform은 이 페이즈에서 수정한다.

---

## 1. 2026-09-03 소스 감사 결과

아래는 착수 전 감사 기록이다. 2026-09-06 구현·검증으로 해소한 항목은 §6~§13에 구분한다.

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
| 완료(§17) | `useNormalMap`은 저작 material snapshot→scene snapshot→GBuffer/Forward instance까지 전달되고, 2026-09-14 유도를 한 함수로 접은 뒤 실장면 캡처로 판정한다 | draw별 픽셀 분리는 여전히 못 잰다(캡처에 draw별 영역이 없다 — W0) | `PBR-W1` 완료 · `PBR-W0`, `PBR-W9` |

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
| `PBR-W1` | normal-map 저작 유무 snapshot 단일화 | ✓ | — | 1 |
| `PBR-W2` | GBuffer/Deferred/Forward native Slang 제품 진입점·공용 현행 평가 | ✓ | W0 | 2.5 |
| `PBR-W3` | backend neutral resource·binding·종료 코드 동등성 | ✓ | W0 | 1 |
| `PBR-W4` | OPAQUE/MASK/BLEND·alpha cutoff·double-sided/cull | ✓ | W2 | 2 |
| `PBR-W5` | AO 소비·고정 4 texture slot 제거·GBuffer packing 검토 | ✓ | W2, W3 | 2.5 |
| `PBR-W6` | emissive factor/strength·constant-only emission·색공간 | ✓ | W2 | 1.5 |
| `PBR-W7` | UV set/transform/sampler/mip·normal/tangent 변환 | ◐ | W2 | 2 |
| `PBR-W8` | material/descriptor/PSO generation 원자 밀봉·플리커 fail-closed | · | W3~W7 | 2 |
| `PBR-W9` | DX12/Vulkan 실장면·장시간·재임포트 회귀와 cutover | · | W8 | 1.5 |
| **합계** |  |  |  | **18** |

`PBR-W0`은 정적 감사와 2026-09-14 manifest 축(§18)을 기성으로 센다 — fixture 여덟 중
셋만 서 있어 완료가 아니다. W3는 중립 상수 단일 출처화와 양 팔 변이 증명으로 닫았고
(§16), W1은 유도를 한 함수로 접고 저장소 소유 fixture로 실장면 판정을 세워 닫았다
(§17). W7의 normal/tangent·UV 선택/변환·mip 구현과 검증은 합계 1.5일 기성이었고,
sampler 단위가 §19로 착지해 2일 기성이 됐다. W1/W2/W3/W4/W5/W6 완료 10.5일 +
진행 기성 3.75일이며 잔여는 3.75일이다. W7에서 **주장하지 않는 축**은 재질 안 슬롯별
sampler 분기와 Anisotropic 둘이다(§19).

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
- RenderGraph scheduling, generic Pipeline/Pass Shader Graph, GPU-driven/DXR는 PHASE 4.75.
- 모션 벡터, Temporal Upscaling, Frame Generation은 PHASE 4.5.
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

---

## 13. W7 세 번째 단위 — mip 생성·소비, 2026-09-06

재질별 wrap/filter sampler 전달은 다음 단위다. 이번 단위는 mip 체인이 없는 재질 텍스처의
생성과 기존 체인의 보존·업로드·샘플링을 다룬다. CEMC8과 자산 ID는 변경하지 않는다.

### 구현

- `Texture::WithMipChain`은 이미 mip이 있는 이미지(부분 체인 포함)와 1×1 owner를 재사용한다.
  그 외에는 1×1까지 체인을 생성하고 별도 GPU cache 신원을 부여한다. 원본 owner를 변경하지
  않으며 mip 0의 픽셀과 BC 블록을 그대로 복사한다. BC1/BC3는 추가 레벨만 다시 압축한다.
- 외부 Standard/typed 재질은 색공간을 선택한 뒤 체인을 만들고 경로·압축·색공간별 캐시에
  보관한다. source와 cooked artifact 경로 모두 이 창구를 사용한다. 내장 텍스처는
  `ModelAssetGeneration` 적재 중 같은 순서로 생성해 immutable generation과 upload descriptor에 담는다.
  색공간을 지정하지 않는 generic legacy texture 로드는 기존 계약을 유지한다.
- sRGB RGB는 선형 공간에서 축소한 뒤 다시 인코딩하고 alpha는 독립적으로 평균한다.
  non-color data와 HDR은 선형으로 처리한다. DirectXTex의 non-WIC 경로를 사용하며,
  기본 필터는 2의 거듭제곱 크기에 box, 그 외 크기에 linear다.
  [공식 필터 규약](https://github.com/microsoft/DirectXTex/wiki/Filter-Flags)과
  [mip 생성 API](https://github.com/microsoft/DirectXTex/wiki/GenerateMipMaps)를 기준으로 확인했다.
- DX12/Vulkan texture cache와 GBuffer/Forward/Shadow SRV는 이미 전체 mip 범위를 전달한다.
  이 경로를 유지하고 실제 픽셀 검사로 소비를 검증한다. 재질 숫자 bytes·vertex schema·
  모델 저장 형식을 바꾸지 않으므로 모델 재게시와 ID 재생성은 필요하지 않다.
- `render.pbr.mip`을 Commandlet 검사로 등록하고 PBR 회귀에 포함했다. 저작 mip의 서로 다른
  색으로 LOD 0~4 및 중간 0.5 단계 선택을 검사한다. 5/8/5 texture table과 RGB/alpha가 다른
  RGBA/BGRA·sRGB·half/float HDR·BC1/BC3 등 10개 포맷을 함께 확인한다.

### 검증

- VS18/v145 CreatorEditor와 AssetCooker Debug x64 빌드 통과.
- `render.pbr.mip`: 양 backend 각각 30-case 통과. baseColor/MR/AO/emission을 CPU 예상값과
  비교하고 Forward/Deferred pre-tone HDR을 비교했다. 최대 경로 편차는 양 backend 모두
  0.00390625로 기존 half 정밀도 비교 허용식 안이며, backend 편차 0·validation 0이다.
- CPU에서 sRGB/linear RGB 평균·독립 alpha·HDR 비클램프, mip 0 bytes와 BC 블록 보존,
  1×1/저작 부분 체인 재사용, NPOT·세로 1×8·배열·큐브의 단계 수와 원소 순서를 검사했다.
  동명 파일을 두 절대 경로에 두고 실제 `DataSystem`의 색공간별 생성 순서·cache 재사용/분리도 확인했다.
- `render.pbr.coverage`: 양 backend 각각 48-case 통과. 추가 8개 조건에서 생성한 마지막
  mip의 alpha를 사용해 MASK 임계값 양쪽을 검사한다. legacy/owned·static/skin의
  GBuffer/Forward/Shadow 판정과 CPU 예상값 편차 0·backend 편차 0·validation 0이다.
- `verify-experiment-model-cook-all.ps1`: 모델 14개·generation 14개·하위 ID 310개의 폐포,
  authoring 실패 주입 5개·충돌 1개 통과.
- **별도 기존 실패:** strict GUID는 `ImmProbe.prefab.meta`의 tracked-meta-policy 위반 1개로
  실패했다. 해당 파일은 이번 작업 전 `709eafe5`에서 이미 추적됐고 `.gitignore` 예외가 없다.
  GUID 자체 invalid/duplicate/missing은 0(meta 239·하위 ID 310). W0 strict gate는 미통과다.
- GBuffer emissive sampling을 mip 0으로 강제하면 첫 case의 중간 band가 0.023438 대신
  0.015625로 나와 `render.pbr.mip.failed`·exit 4로 실패한다. 검사 후 셰이더 원본 bytes를 복원했다.
- 전체 `verify-pbr-wiring-baseline.ps1` 통과: 양 backend primitive/Gunner 제품 캡처 네 개,
  기본 패스와 Water/Wind, 독립 계약 12개 묶음, 재질 해석/이행/cooked,
  PBR 36·coverage 48·emission 78·normal 128·UV 64·mip 30·AO 48-case 및 실패 종료/캡처 충돌이다.
  이 실행의 normal backend 편차는 0.0000305176으로 허용 범위이며 나머지 수치 검사의
  backend 편차는 0이다. validation 오류는 없고 W9 golden·장시간 acceptance는 미실시다.
- `verify-model-render-wiring.ps1` 통과: 모델 14개의 generation corpus에 내장 texture mip
  존재·mip/array subresource 폐포 단정을 추가해 실행했다. 8개 vertex mask·SU 84B/64/68,
  typed upload 1/1·DX12/Vulkan pass 연결·Vulkan validation 0을 확인했다.
- Editor 설정과 오류 주입 셰이더를 원본 bytes로 복원했다. 이번 변경 파일의
  `git diff --check`와 dashboard JavaScript/활성 행·공수 합계 검사를 통과했다.
  PHASE 4는 10행·18일·완료 8.5·기성 3.5·잔여 6, 통합은 54행·267.5일·잔여 251.5다.

근거: `%TEMP%/creator-pbr-phase4/build-w7mip-coverage.log`, `build-w7mip-cooker-final.log`,
`w7mip-fixed/`, `w7mip-coverage/`, `w7mip-negative.log`, `w7mip-baseline.log`,
`w7mip-cook-all.log`, `w7mip-guid.log`(기존 정책 위반), `w7mip-model-wiring.log`,
`w7mip-doc-check.log`, `%TEMP%/creator-pbr-9bf1769aa7b644338d9a712087f83678/`(전체 회귀).

---

## 14. W8 — material/descriptor/PSO generation 원자 밀봉, 2026-09-14

### 착수 전 실측이 뒤집은 것

계획서 §1은 W8을 "flat property/고정 texture table과 descriptor batch가 material
generation보다 약한 신원으로 재사용될 **여지**"라고 적었다. 소스를 다시 읽으니 여지가
아니라 **실재하는 결함 둘**이었고, 둘 다 "가끔 재질이 틀리다 / 가끔 검다"의 모양을 가진다.

1. **밀봉 중복 제거 키가 legacy `Material*` 주소 하나였다.**
   `SealGBufferMaterials`/`SealForwardMaterials`가 `unordered_map<const Material*, ...>`로
   같은 주소의 draw를 합쳤다. 그런데 `DataSystem::Materials`는 이름으로 캐시한 **같은
   객체**를 여러 MeshRenderer에 돌려주고, 인스턴스 override(`MaterialInstance`)는 렌더러마다
   다르다. 주소가 같다는 이유로 먼저 밀봉된 스냅샷을 뒤의 draw가 그대로 받았고, 그 순간
   override가 통째로 사라진다. 값이 아니라 주소를 신원으로 쓴 것이 원인이다.
   또 `materialSource`가 없는 draw는 전부 `&defaultMaterial` 한 주소로 합쳐졌다.

2. **인코더가 버린 명령이 어디에도 세어지지 않았다.**
   DX12 인코더는 놓인 PSO 핸들(`Resolve`가 무효), 만료된 descriptor 버전, 주소 0인 버퍼를
   만나면 **조용히 `return`** 한다. Vulkan은 `NoteUnimplemented` 하나로 뭉뚱그린다. 그
   사건은 곧 "이 draw가 화면에서 사라진다"인데 수가 0이라 증상만 있고 증거가 없었다.
   GBuffer의 `Record` 루프도 실패마다 `continue`라 머테리얼 하나가 통째로 빠져도 프레임은
   성공으로 보고됐다(Forward는 `return false`로 fail-closed — 두 패스가 비대칭이었다).

### 구현

- **값 신원을 만들었다.** `EnhancedMaterialSealIdentity`(sealHash·authoredDigest·
  authoredRevision·modelGeneration·sceneEpoch·frameId)를 두 draw snapshot에 싣는다.
  `sealHash`는 스냅샷 값 전체의 FNV-1a digest이고 **프레임과 무관하다** — 값이 같은가와
  이번 프레임 것인가를 나눠 물을 수 있어야 하므로 frameId를 digest에 섞지 않는다.
  `-0.0`/`+0.0`과 NaN은 정규화한다.
- **중복 제거 키를 값으로 바꿨다.** `digest(주소, 저작 값 digest, model generation,
  ShaderMeta handle)`. 저작 값이 같으면 여전히 합쳐지고, override가 다르면 갈린다.
  `MaterialInstance::Revision`을 프록시에서 `PooledDraw`까지 날라 seal 신원에 싣는다 —
  전에는 프록시까지만 오고 렌더 스냅샷에는 없었다.
- **패스에 장부를 뒀다.** `EnhancedDrawSealLedger`가 프레임마다 ① 이 snapshot이 이번
  프레임 것인가(`Accept`) ② 같은 값이 같은 PSO·같은 texture/sampler 묶음으로 그려졌는가
  (`Observe`)를 판정한다. 위반이면 **그 draw를 생략하고 이유를 남긴다** — 계획 §2의
  처방 그대로이며, 부분 게시보다 빠진 그림이 낫다(빠진 것은 셀 수 있고 섞인 것은 못 센다).
  도장이 없는 snapshot(격리 fixture)은 staleness 축을 재지 않고 `unstamped`로만 센다.
- **Record 단계의 조용한 누락 일곱 자리에 이유를 붙였다.** pipeline·geometry·
  materialConstants·coordinates·textureTable·bindings·instances. 병렬 기록이라 문자열이
  아니라 원자 계수만 한다.
- **DX12 인코더에 drop 계수를 넣었다.** Vulkan의 `NoteUnimplemented`와 같은 자리이며,
  두 backend의 수를 `IRHIParallelCommandPool::DrainEncoderDrops` 한 이름으로 모은다.
- **capture manifest의 `missing` 셋을 실제 값으로 바꿨다.** W0이 "sampler identity·
  descriptor generation·resolved PSO key는 draw snapshot에 없다"고 적어 두었던 자리다.
  이제 draw마다 seal을, 프레임마다 `sealLedger`(패스별 counters·bindings·sampler 신원·
  encoder drop)를 싣는다.

### 검증

- `render.pbr.seal` — 값 digest 9종 변이, 저작 digest 6종(override·keyword·blend·값 타입·
  texture 색공간), 장부 14종(프레임 신원·값 변경·PSO 혼합·배치 혼합·누락 계수·Begin 초기화),
  sampler 신원 4종. GPU를 켜지 않는다 — W8이 닫는 것은 픽셀이 아니라 신원이다.
- `verify-pbr-wiring-baseline.ps1`이 실장면 캡처마다 ① 모든 draw가 이번 프레임 도장을
  받았는지 ② 패스별 위반 0 ③ encoder drop 0 ④ 업로드 실패로 흰색을 대신 낸 횟수 0을
  단정한다.

### 남은 것

- 재질별 wrap/filter sampler 전달은 여전히 W7의 남은 단위다. 지금은 패스가 고정 sampler
  하나를 걸며, 장부는 **무엇을 걸었는지**를 값으로 남긴다. 재질별 sampler가 들어오면
  이 자리가 그대로 재질 축이 된다.
- 장부의 이빨은 변이로 증명해야 한다(빌드 뒤 실행). 아래 §15의 변이 목록에 함께 적었다.

---

## 15. W9 — 실장면·장시간·재임포트 회귀와 cutover, 2026-09-14

### 착수 전 실측이 뒤집은 것

- **`verify-pbr-wiring-baseline.ps1`은 `run-all.ps1`에 물려 있지 않았다.** 저장소 전체에서
  이 게이트를 부르는 것은 계획 문서뿐이었다. PBR 픽셀·캡처 축 전체가 도는 세트 밖이었다.
- **캡처의 float32 원본을 읽는 코드가 하나도 없었다.** 게이트는 파일 **크기**만 쟀다.
  §6의 RMSE는 손으로 한 번 잰 값이고 회귀를 잡을 수 없다. 생산만 있고 소비가 0이었다.
- **golden 자산도 비교 코드도 없다.** 저장소의 `*golden*`은 전부 텍스트(CLI 파서·레지스트리·
  UI 레이아웃)다. 이미지 golden은 존재한 적이 없다.
- **장시간 하네스가 없다.** `soak`/`장시간` 계열 검색 결과 0건. 재임포트 축은 있었지만
  PBR 픽셀과 연결돼 있지 않았다.

### 구현

- **`render.pbr.compare <left> <right> [out.json]`** — 두 제품 캡처의 attachment 7장을
  열어 최대 절대 편차·RMSE·허용치 초과 표본 수를 낸다. 허용식은 기존 parity와 같다
  (절대 0.002 + 상대 0.5%) — 새 자를 만들면 두 검사가 다른 말을 한다. 두 캡처의 backend가
  같으면 거부한다(대조군은 독립 유도를 가져야 한다). 결과는 통과해도 JSON으로 남긴다.
- **`render.pbr.sealstatus`** — 라이브 프레임의 세대 진단을 **수**로 낸다. 사람이 읽는
  상태 문장을 게이트가 파싱하게 두면 문장을 다듬는 순간 게이트가 조용히 아무것도 재지
  않게 된다. 이 명령은 재기만 하고 판정하지 않는다.
- **`verify-pbr-soak.ps1`** — 회전·이동·주기적 재임포트를 섞어 표본을 모으고, 매 표본에서
  seal 위반 0 · encoder drop 0 · 업로드 실패 0 · `drawCount > 0` · frameId 전진을 단정한다.
  기본 1분(게이트), acceptance는 `-Minutes 10`.
  이 검사가 재는 것은 "10분간 검은 프레임이 없었다"가 아니라 **검은 프레임을 만드는
  기계장치가 한 번도 돌지 않았다**이다. 매 프레임 픽셀을 읽는 것은 감당할 수 없고, 눈으로
  보는 것은 게이트가 아니다.
- **`run-all.ps1`에 두 스텝을 넣었다** — "PBR 제품 배선·세대 밀봉"과 "PBR 장시간 세대 밀봉".
  `$Exe` 경로에서 구성을 뽑는다(Release exe로 돌릴 때 조용히 Debug 산출물을 찾지 않도록).
- **silent neutral substitution 축을 열었다.** 두 texture cache에 `GetUploadFailureCount()`를
  두고 캡처 manifest에 싣는다. 저작으로 없는 슬롯의 중립값(AO 미저작 등)은 세지 않는다 —
  세는 것은 **업로드가 실패해서 흰색으로 덮은** 횟수다.

### 실행 결과 (2026-09-14, Debug x64)

빌드 exit 0(오류 0). 아래는 전부 실제로 돈 수다.

- **`render.pbr.seal`** — 42 케이스, 실패 0.
- **제품 캡처 W8 단정** — dx12·vulkan 양쪽 PASS. 매 draw가 도장을 갖고 이 프레임·이 epoch
  것이며, 패스별 위반 0 · unstamped 0 · samplerIdentity 유효 · encoder drop 0 ·
  업로드 실패 0.
- **`verify-pbr-soak.ps1 -Backend dx12`** — 109 표본, 109 전부 프레임 전진, 위반 0.

### 교차 백엔드 픽셀 — 게이트의 질문을 좁혔다

두 백엔드 캡처를 실제로 맞대자 **GBuffer 다섯 장은 초과 0**이었다(`normal`만 max 0.000244,
초과 0). 조명 합성 이후 두 장은 넘었다. 그런데 대조를 잡아 보니 **같은 백엔드끼리도**
시각만 벌어지면 넘었다.

| 비교 | Δt | `preToneHdr` 초과 | max | rmse |
|---|---|---|---|---|
| dx12 vs dx12 | 0.27s | 19,490 / 3,916,416 | 0.043 | 0.00072 |
| dx12 vs dx12 | 3.11s | 57,977 / 3,916,416 | 0.039 | 0.00127 |
| dx12 vs vulkan | 5.68s | 241,936 / 3,916,416 | 0.119 | 0.00421 |

원인은 켜져 있는 시간 구동·시간축 누적 효과다 — 움직이는 구름 그림자
(`shadow.cloudMoveSpeed`), 볼류메트릭 포그의 직전 프레임 혼합
(`mPreviousFrameBlendFactor`), SSGI 누적. 그리고 **캡처는 시뮬레이션 시각을 고정할 수단이
없다**(`time.*` 명령이 존재하지 않는다). 두 캡처는 다른 시각의 서로 다른 그림이고, 그것을
픽셀로 맞대는 것은 애초에 성립하지 않는 질문이었다.

그래서 **허용치를 늘려 초록으로 만들지 않았다.** 그러면 이 두 장에 대해 게이트가 아무것도
재지 않으면서 재는 척하게 된다. `preToneHdr`·`display`는 판정에서 빼되 수는 매 실행
남긴다(`gated:false`, 로그에 `[측정만 · 시각 고정 불가]`). 시각을 고정할 수 있게 되면 이
자리를 판정으로 되돌리는 것이 **W9의 남은 단위**다.

시간차 3.11s가 58k인데 5.68s 교차가 242k이므로 **시간만으로 다 설명되지 않는 잔차**가
있다. 다만 이 하네스로는 그 잔차를 분리할 수 없다 — 분리하려면 시각 고정이 먼저다.

### 변이 증명 — 둘은 섰고, 하나는 설 자리가 없었다

- **② `Stamp` 호출 제거 → 잡았다.** 게이트가 `verify-pbr-wiring-baseline.ps1:114`
  "Draw seal missing or from another frame"으로 붉었다. 붉은 이유까지 맞다.
- **⑤ attachment 픽셀 교란 → 잡았다.** `baseColor` 1,000 표본을 +0.05 밀자 정확히
  `exceeded 1000/3916416 · max 0.050000`, 종료 코드 4. 심은 것만 정확히 잡았고 측정 전용
  두 장은 실패시키지 않았다.
- **① 주소 키 복원 → 게이트는 통과했다. 그런데 이것은 "눈멀었다"가 아니다.**
  변이 뒤에도 `dx12-gunner`는 draw 10 / distinct sealHash 10 / bindings 10 으로 기준과
  같았다 — 10개 draw가 각자 다른 `Material*`이라 **주소 키만으로도 갈린다**. 즉
  **이 fixture는 그 결함을 자극하지 못한다.** W8이 고친 결함(하나의 `Material`을 공유하는
  두 renderer가 서로 다른 `MaterialInstance` override를 갖는 경우)을 재현하는 fixture가
  저장소에 없다. **W8의 핵심 수정은 아직 증명되지 않았고**, 그것을 증명하려면 그 fixture를
  먼저 만들어야 한다 — 이것이 남은 단위다.
- **③ `Accept` 강제 true · ④ `NoteDropped` 제거는 돌리지 않았다.** ④는 애초에 게이트가
  못 잡는 변이로 적어 두었고(잡는 것은 소스 대조뿐), ③은 ②가 같은 경로의 이빨을 이미
  보였다.

### 실측이 드러낸 것 (계획에 없던 것)

- **저작 digest 축이 fixture마다 비어 있다.** `FT_Primitives`는 8 draw 전부
  `authoredDigest = 0`(= `proxy->m_authoredMaterial`이 null). `Gunner_F_Mythic`은 distinct 3종
  중 2종이 non-zero라 축이 살아 있고, 그 값은 **두 백엔드에서 동일**했다.
  `authoredRevision`은 두 fixture 모두 전부 0 — 이 축은 아직 아무것도 나르지 않는다.
- **`sealHash`는 프로세스 지역값이다.** 같은 씬·같은 코드인데 실행마다 전부 달라진다
  (texture owner의 `m_assetId`가 런타임 발급 GUID라 digest에 섞인다). 프레임 안 신원으로는
  옳지만 **실행 간 golden으로 쓰면 안 된다.** 반대로 `authoredDigest`는 실행 간 안정적이었다.
- **이 게이트는 W9 이전부터 붉었다.** `verify-pbr-wiring-baseline.ps1:218`이 부르는
  `verify-experiment-contract.ps1`이 링크 단계에서 죽는다 — 독립 probe의 라이브러리 목록이
  `SceneRuntime`의 실제 의존과 어긋나 있다. 이 호출은 HEAD에 이미 있었고 W9가 건드리지
  않았다. 즉 **PBR 배선 게이트가 끝까지 간 적이 없다.** 세 층(`nethost` · `EngineDiagnostics`
  · FMOD)을 채워 미해결 90 → 49로 줄였고 나머지(`PhysicX::*` · `GameInputInitialize`)는
  별건으로 남겼다.

### 아직 하지 않은 것 (정직하게)

- **10분 acceptance를 돌리지 않았다.** 위 soak은 게이트용 1분이다.
- **`vulkan` soak을 돌리지 않았다.** dx12만 쟀다.
- **Release 구성으로 돌리지 않았다.** 위 수는 전부 Debug다.
- **cutover는 하지 않았다.** 게이트가 끝까지 초록인 적이 없으므로 판단할 근거가 없다.

---

## 16. W3 — neutral 의 단일 출처화, 2026-09-14

### 착수 전 실측이 뒤집은 것

W3 는 `◐` 로 서 있었고 남은 것이 "neutral 값 맞추기"인 줄 알았다. 실제로 재 보니
**값은 이미 양 백엔드가 같았다**. §6 이 Vulkan ORM 을 `(1,1,1,1)` 로 고친 뒤로 두
숫자는 어긋난 적이 없다.

그런데도 조건이 닫히지 않는 이유는 값이 아니라 **숫자가 두 벌이라는 구조**였다.
착수 시점의 리터럴은 7 자리였다.

| 파일 | 자리 | 값 |
|---|---:|---|
| `DX12TextureCache.cpp` | 3 (`CreateWhiteTexture` · `GetBlackTexture` · `GetOrmNeutralTexture`) | 흰·검정·ORM |
| `VulkanRenderServices.cpp` | 4 (`GetOrUpload` 안 **흰색 3 회** · `GetBlackTexture` · `GetOrmNeutralTexture`) | 흰·검정·ORM |

이 구조가 실제로 한 번 갈렸다. 예전 셰이더는 금속을 `orm.b + metallic` 으로 **더했고**
그때는 B=0 이 중립이었다. `a2e5ecdc` 가 결합을 곱셈으로 바꾸면서 전제가 뒤집혔는데
상수가 따라가지 않았고, 그 뒤로 ORM 텍스처가 없는 재질은 저작한 metallic 과 무관하게
전부 비금속으로 그려졌다. **두 벌이면 언젠가 갈린다**는 것이 W3 의 진짜 조건이다.

### 구현

- `IRenderTextureCache.h` 에 `RHINeutralTexel::kWhite`/`kBlack`/`kOrmNeutral` 을
  `inline constexpr` 로 두어 **정본을 하나로** 만들었다. 값이 그 값인 이유(ORM 드리프트
  경위 포함)도 선언부에 같이 적었다 — 값과 이유가 떨어져 있으면 다음 사람이 또 옮긴다.
- 위 7 자리를 전부 그 상수 참조로 바꿨다. 두 백엔드 `.cpp` 에 중립 숫자가 **0 자리** 남았다.

### 여덟 번째 자리 — 일부러 합치지 않았다

전수 grep 이 처음 센 7 자리 밖에서 하나를 더 찾아냈다.
`EnhancedSceneRendererLiveDX12Adapter.cpp:364` 의 `Fog.CloudNeutral` 이다.

숫자는 `kWhite` 와 같지만 **뜻이 다르다** — 여기의 흰색은 "구름 그림자 없음"(포그가
곱하는 가시도 1)이고 저쪽은 PBR 재질 슬롯의 중립이다. 한 상수를 공유시키면 한쪽
규약이 바뀔 때 다른 쪽이 조용히 따라간다. 게다가 이 경로는 DX12 전용이라 백엔드
사이에 갈릴 짝 자체가 없다. 합치지 않은 이유를 그 자리에 주석으로 남겼다.

### 다섯 축이 실제로 어떻게 닫히는가

§4 의 W3 조건은 중립을 **다섯 축**(base color·normal·ORM·emissive·AO)으로 적어 두었는데
fixture 는 텍셀 셋만 읽는다. 나머지가 빠진 것이 아니라 **축마다 닫히는 기제가 다르다**.

| 축 | 미저작일 때 | 무엇이 백엔드 무관을 보장하는가 |
|---|---|---|
| base color | 흰 텍셀 | `RHINeutralTexel::kWhite` 단일 상수 + fixture 가 GPU readback 으로 검사 |
| ORM | `(1,1,1,1)` | `kOrmNeutral` 단일 상수 + fixture readback |
| emissive | 현행 흰 텍셀 · legacy 검정 | `kWhite`/`kBlack` 단일 상수 + fixture 가 검정 readback |
| AO | 흰 텍셀 | 같은 `kWhite` 경로(`MaterialTextureTable.h:205` 의 else 분기) |
| **normal** | **샘플되지 않는다** | 텍셀이 아니라 **분기**다 — 아래 |

normal 만 성질이 다르다. 흰색은 tangent-space 중립이 아니다(`(1,1,1)` 을 풀면
`(0.577, 0.577, 0.577)` 이지 표면 법선이 아니다). 그래서 셰이더가 값으로 때우지 않고
플래그로 가른다 — `GBuffer.slang:238`·`ForwardShade.slang:419` 의 `useNormalMap` 이
거짓이면 `normalSample` 을 **읽지 않고** 기하 법선을 그대로 쓴다. 바인딩된 흰 텍셀은
그 프레임에 아무 뜻도 갖지 않는다.

이 분기가 백엔드와 무관한 근거는 두 겹이다. 플래그를 세우는 코드가
`Material.cpp:269` → snapshot → `MaterialTextureTable.h` 로 **백엔드 중립 계층에만**
있고(`RHI/DX12`·`RHI/Vulkan` 어디에도 사본이 없다), 소비하는 셰이더가 W2 이후
양 백엔드 공용 `.slang` 정본 하나다.

슬롯별 대체 정책 자체도 `MaterialTextureTable.h:183-208` 의 `Upload` **한 함수**에만
있다. 값이 하나이고 정책이 하나이므로, 이 조건은 "두 구현의 숫자가 같다"가 아니라
"구현이 하나다"로 닫힌다.

### 검증

```powershell
pwsh Tools/regression/verify-pbr-wiring-baseline.ps1
```

값 축을 재는 것은 `ValidatePbrTextureDefaults`(`VulkanGeometryPassTest.cpp:490`)다. 흰·ORM·
검정 1×1 을 실제로 GPU 에 올려 readback 으로 되읽고 채널마다 `1e-6` 안에서 기댓값과
맞춘다. 기댓값은 상수를 참조하지 않고 테스트가 **따로 적은 숫자**다 — 상수를 읽어 오면
동어반복이 되어 "중립이 0 으로 바뀌었다" 같은 회귀를 못 잡는다.

★ 이 검사가 **두 백엔드를 다 돈다**는 사실이 이름에 안 드러나 있다. `RunVulkanGBufferTest`
(= `vk.gbuffer`)가 DX12 와 Vulkan 을 각각 부팅해 같은 `CaptureGBufferBackend` 에 넣고
(`VulkanGeometryPassTest.cpp:2280`·`2342`), 그 함수의 첫 줄이 `ValidatePbrTextureDefaults`
다. 반대로 `dx12.gbuffer` 는 중립값을 보지 않는다 — 이름으로 고르면 틀린다.
게이트에는 `verify-pbr-wiring-baseline.ps1:213` 이 `vk.gbuffer` 로 물려 있다.

종료 코드 동등성 축은 §6 에서 이미 닫혔다(실패 수 ≥ 1 이면 종료 코드 7).

### 변이 증명 — 두 팔을 따로 쳤다

초록인 검사는 이빨이 있는지 알 수 없다. 변이를 심기 전에 **발현 관측값을 먼저 적었다**.

**변이 A — 공유 상수를 건드린다.** `kOrmNeutral` 의 B 를 255 → 0 으로 되돌렸다
(`a2e5ecdc` 이전 상태의 재현이다). 예고한 관측값은 "texture 1 channel 2 가 expected 1,
actual 0 으로 붉는다".

```
[1/4] DX12 기준 캡처 실패: PBR default texture 1 channel 2: expected 1.000000, actual 0.000000
EXIT=4 · vk.gbuffer -> failed
```

예고한 자리에서 예고한 문장으로 붉었다. 이 이빨의 출처는 **기댓값이 상수를 참조하지
않는다**는 점이다 — `ValidatePbrTextureDefaults` 는 `i == 2 && channel != 3 ? 0.f : 1.f` 로
숫자를 따로 적는다. 상수를 읽어 왔다면 동어반복이 되어 이 변이를 그대로 통과시켰을 것이다.

**변이 A 만으로는 절반이다.** 검사가 DX12 를 먼저 돌기 때문에 `[1/4]` 에서 멈췄고,
Vulkan 팔은 **자극조차 되지 않았다**. 여기서 멈추고 "양 백엔드 증명"이라고 적으면
③(자극 못 함)을 ①(잡았다)로 적는 것이다.

**변이 B — Vulkan 호출 자리만 어긋낸다.** `GetOrmNeutralTexture` 의 Vulkan 구현이
`kOrmNeutral` 대신 `kBlack` 을 넘기게 했다. DX12 는 건드리지 않았다. 예고한 관측값은
"`[1/4]` 은 통과하고, ORM 의 R 이 0 으로 붉는다".

```
[1/4] DX12 기준 Material property→reflection b2→PSO→5 MRT · targeted next-use 통과
PBR default texture 1 channel 0: expected 1.000000, actual 0.000000
EXIT=4 · vk.gbuffer -> failed
```

둘 다 맞았다. DX12 팔이 초록인 채로 Vulkan 팔만 붉었으므로, 이 검사가 두 백엔드를
각각 본다는 것이 실행으로 증명됐다.

(변이 B 실행에서 `[3/4]` 비교 줄이 편차 100% 로 찍힌다. 실패한 Vulkan 캡처를 비교가
계속 읽어서인데, 판정 자체는 `passed = captured && ...` 로 `captured` 를 곱하므로
거짓 초록이 아니다. 진단 출력일 뿐이다.)

### 실측이 드러낸 것 (계획에 없던 것)

- **동시 세션이 같은 트리를 빌드하면 다섯 가지로 깨진다.** 이번에 전부 겪었다 —
  `LNK1168`/`LNK1104`(에디터나 상대 링크가 `CreatorEditor.runtime.dll` 점유),
  `C1041`(두 CL 이 같은 `Editor.pdb`), `LNK1181`(상대가 추가 중인 obj 부재),
  `LNK4076`(`.ilk` 가 남의 손에). 프로세스를 죽이지 않고 **dll 잠금이 풀리는 순간을
  노려 직렬화**하면 지나간다. 주의: MSBuild 프로세스 수는 대기 신호로 쓸 수 없다 —
  node reuse 로 유휴 노드가 15 분 남아 "MSBuild 없음"은 거의 오지 않는다.
- **변이 원복은 mtime 을 올려야 한다.** 바이트를 되돌려 놓아도 mtime 이 그대로면
  재빌드가 일어나지 않아, 소스는 옳은데 게이트가 변이 바이너리를 잰다.
- 소스 왕복은 **Latin-1 바이트 치환**으로 했다. 이 트리에 CP949 파일이 섞여 있어
  텍스트로 읽고 쓰면 한글 주석이 깨진다.

---

## 17. W1 — normal-map 저작 유무의 실장면 판정, 2026-09-14

### 착수 전 실측이 뒤집은 것

W1 의 배선은 진작 끝나 있었다. §1 의 표가 남겨 둔 것은 한 줄이다 — "실제
Gunner/primitive 런타임 장면 판정은 아직 하지 않음". 그래서 이 슬라이스의 일은
코드를 더 잇는 것이 아니라 **재는 것**이었다.

재 보니 정본은 하나가 아니라 **유도가 둘**이었다. `SealSource` 로 가는 길이 둘이고
각자 다른 곳에서 같은 사실을 뽑는다.

| 경로 | 어디서 뽑나 |
|---|---|
| `BuildSealSourceFromLegacy` (`ExperimentMaterialSealing.cpp:41`) | `legacy.m_materialInfo.m_useNormalMap` |
| `BuildSealSourceFromAuthored` (`:167`) | resolver 가 `normalMap` owner 를 실제로 줬는가 |

주석은 둘이 같은 뜻이라고 적고 있지만, 그것은 **적어 둔 약속이지 강제된 것이 아니다**
— W3 에서 방금 닫은 것과 같은 모양이다([[§16]]). 라이브 경로는 저작 쪽을 먼저 쓰고
실패할 때만 legacy 로 내려간다(`EnhancedSceneRenderer.cpp:3061-3080`).

곁가지로 둘을 더 확인했다. `m_useNormalMap` 은 **직렬화되지 않는다**(`reflect()` 에
없다) — 그래서 디스크에서 어긋난 값이 들어올 길은 없다. writer 는 `UseTextureMap`
과 `ResetTextureRuntime` 둘뿐이고 둘 다 owner 표와 함께 움직인다.

★ **격리 자가 검사는 이 축을 재울 수 없다.** `EnhancedSceneRendererSelfTest.cpp:3201`
이 `m_materialInfo.m_useNormalMap` 으로 `EnhancedDrawItem.useNormalMap` 을 채우는데,
그것은 단정이 아니라 **스냅샷이 없는 경로의 폴백 채널을 채우는 생산자**다
(`EnhancedGBufferPass.cpp:439` 가 "snapshot 이 없는 격리 fixture 호환 경계" 라고
적은 그 자리). 즉 격리 fixture 는 스냅샷 경로를 한 번도 타지 않으므로, 제품 정본이
스냅샷이라는 사실을 격리로는 확인할 수 없다 — W1 의 판정이 실장면이어야 하는 이유가
이것이다.

### fixture — 저장소가 소유해야 했다

대조쌍을 만들 자산이 **저장소에 없었다**. 추적되는 모델은 `Prim_*` 9 개뿐이고 전수
확인 결과 `normalTexture` 가 전부 0 건이다. 노멀맵을 가진 것은
`Gunner_F_Mythic.glb` 인데 `.gitignore` 의 `/Dynamic_CPP/Assets/Models/*` 에 막혀
**추적 밖**이다.

★ **같은 이유로 이 게이트의 Gunner 캡처 축은 지금 이 기계 전용이다.**
`verify-pbr-wiring-baseline.ps1` 이 `model.loadcached` 로 그 파일을 여는데, clean
checkout 에는 파일이 없다. W1 이 만든 결함이 아니라 전부터 있던 것이고, 여기서
드러났으므로 적어 둔다.

그래서 `Tools/regression/fixtures/pbr-normal-pair/` 를 저장소가 직접 소유하게 했다
(`imgui-ini`·`gltf-multifile` 과 같은 규약).

- 쿼드 2 개 · 재질 2 개를 **한 mesh 의 두 primitive** 로 둔다. 자산을 둘로 나누면
  카메라·광원·프레임·씬 epoch 이 달라질 수 있어 차이의 원인을 가릴 수 없다. 한 노드
  아래 두면 **변인이 재질 하나**로 좁혀진다. `POSITION`/`NORMAL`/`TEXCOORD_0`
  accessor 까지 공유하고 인덱스만 가른다.
- 노멀맵을 평평한 `(128,128,255)` 로 두지 않았다. 그 값은 디코드하면 `(0,0,1)` 이라
  **안 물린 것과 같은 픽셀**이 나와서, "노멀맵이 안 물렸다" 는 회귀가 통과한다.
  좌우를 +X/−X 로 기울여 두었다.

### 구현

- `render.pbr.normalpair <capture-dir>` (`PbrNormalPair.cpp`). 캡처 manifest 의 draw
  마다 `useNormalMap` 과 `normalMap` 슬롯의 `authored` 를 맞댄다. 둘은 **다른 곳에서
  유도된 같은 사실**이라 어긋나면 정본이 둘이라는 뜻이다.
- 픽셀이 아니라 manifest 를 읽는 이유: 캡처는 attachment 를 통째로 남기지 draw 별
  영역을 남기지 않아 "노멀맵 있는 draw 의 픽셀" 만 떼어낼 수단이 아직 없다(draw 별
  영역은 W0 의 남은 항목이다).
- 게이트(`verify-pbr-wiring-baseline.ps1`)의 backend 별 회차에 세 번째 캡처로 붙였다.
  fixture 가 `model.load` 로 자산 트리에 복사되므로 회차 앞과 `finally` 에서 지운다 —
  게이트가 자산 트리에 잔해를 남기면 다음 실행의 전제가 달라진다.

### 빈 집합을 통과시키지 않는다

★ 이 검사의 본문(`useNormalMap == authored`)은 **대조가 없으면 공짜로 참이다**.
fixture 가 빠지거나 배치가 실패해도 전부 0 이면 불일치가 0 이라 초록이 된다. 그래서
"노멀맵 있는 draw 와 없는 draw 가 둘 다 있는가" 를 먼저 묻고, 아니면 실패시킨다.

실물로 확인했다 — fixture 없이 `FT_Primitives` 만 찍은 캡처에 물리면
`draw 8 · useNormalMap 1/0 = 0/8 · 불일치 0` 을 내면서도 **FAIL** 한다
("대조쌍이 없다"). 불일치가 0 인 채로 붉어지는 것이 이 방어의 요점이다.

### 변이 증명 — 예고와 어긋난 숫자가 새 사실을 줬다

심기 전에 관측값을 적었다: 저작 경로의 유도(`ExperimentMaterialSealing.cpp:177`)를
반전시키면 `useNormalMap 1/0` 이 `1/9 → 9/1` 로 뒤집히고 **불일치 10** 이 되어야 한다.
그대로면 결론은 ③(자극 못 함)이고, 그때는 legacy 쪽을 쳐야 한다.

실제로 나온 것:

```
normal pair — draw 10 · useNormalMap 1/0 = 1/9 · 슬롯 없음 0 · 불일치 2
  draw 8: useNormalMap=0 인데 normalMap authored=true
  draw 9: useNormalMap=1 인데 normalMap authored=false
[CLI] render.pbr.normalpair FAIL
```

**잡혔고, 붉어진 자리도 맞다** — 대조는 `1/9` 로 그대로 섰으므로 대조 가드가 아니라
격리하려던 **불일치 단정**이 잡았다. 복원 뒤 같은 시나리오는 `불일치 0 · PASS ·
exit 0 · stderr 없음` 이고 자산 트리 잔해도 0 이다.

★ **그런데 예고한 10 이 아니라 2 였다.** 저작 경로를 통째로 반전시켰는데 뒤집힌 것은
**fixture 의 두 draw 뿐**이고 `FT_Primitives` 의 나머지 여덟은 꿈쩍하지 않았다.
뜻은 하나다 — **그 여덟은 저작 경로를 타지 않는다**(legacy 폴백을 타거나
`authoredMaterialSource` 자체가 없다). 즉 한 프레임 안에서 두 유도가 **실제로 동시에
돌고 있다**. W1 이 "단일화" 를 조건으로 적은 근거가 가설이 아니라 관측이 됐다.

예고와 실제의 **차이**가 이것을 드러냈다. 숫자를 적어 두지 않았다면 "붉었으니 됐다"
로 넘어가 이 비대칭을 못 봤을 것이다([[mutation-passed-because-fixture-cannot-trigger]]).

### 유도를 접었다

변이가 "두 경로가 한 프레임에 공존한다" 를 보인 이상 강제력만 세우고 두는 것은
반쪽이다. 그래서 물음을 한 함수로 옮겼다 — `DeriveUseNormalMap(source.textures)`
(`ExperimentMaterialSealing.cpp`). 두 builder 가 그것만 부른다.

접는 비용이 싼 이유는 legacy builder 가 **바로 위에서 `source.textures` 를 이미
채우기 때문**이다(`meta.properties` 순회 → `legacy.GetTextureMapShared(desc.name)`).
`m_materialInfo.m_useNormalMap` 을 한 번 더 읽을 이유가 없었다. 두 값은 `UseTextureMap`
과 `ResetTextureRuntime` 이 함께 움직여 오늘 일치하므로, 접어도 **값이 변하지
않는다** — 값이 같으면 seal 해시도 같으므로 W8 장부에 파문이 없다. 그 "변하지 않음"
을 실장면 캡처와 `render.pbr.seal` 로 확인했다.

### 남은 것 (정직하게)

- **draw 별 픽셀 분리는 못 쟀다.** 캡처에 draw 별 영역이 없다(W0 의 남은 항목).
  지금 판정은 "플래그가 정본 하나에서 왔는가" 이지 "그 플래그가 픽셀을 바꿨는가"
  가 아니다. fixture 의 노멀맵을 평평하지 않게 만들어 둔 것은 그 판정이 가능해질 때를
  위한 것이다.
- **Gunner 축은 여전히 이 기계 전용이다.** 위에 적은 추적 밖 문제는 W1 이 고치지
  않았다.

---

## 18. W0 — draw별 신원 세 축을 캡처에 싣는다, 2026-09-14

### 착수 전 실측이 뒤집은 것

W0 의 남은 항목은 "draw-level sampler identity · descriptor generation · resolved PSO
key 를 capture manifest 에" 였다. 재 보니 **셋 중 둘은 이미 있었다**.

캡처의 `sealLedger.<pass>.bindings` 가 `sealHash` 마다 `pipelineId`(= resolved PSO
key)와 `samplerIdentity` 를 적고 있고, `draws[].seal.hash` 로 조인하면 draw 별 값이
나온다. 실제 캡처로 확인했다 — draw 10 · distinct sealHash 10 · 조인 10/10, 누락 0.

값을 draw 항목에 복사하지 않은 것은 설계다. **"같은 밀봉을 공유하는 draw 는 같은
바인딩을 쓴다"가 W8 의 불변식**이라, draw 마다 복사하면 그 불변식이 표에서 사라진다.

그래서 이 슬라이스가 할 일은 셋을 다 새로 싣는 것이 아니라 (a) 없는 하나를 만들고
(b) 있는 것이 **실제로 닿는지를 단정**하는 것이었다.

### descriptor generation 은 제품에 없었다

`EnhancedDrawSealLedger::Binding::descriptorVersion` 은 **writer 가 0 인 죽은 칸**이었다
— 선언만 있고 아무도 채우지 않았으며, `operator==` 에도 없고, 캡처가 방출하지도
않았다.

값 자체는 있었다. `RHIDescriptorVersionHandle::ToToken()` 은 백엔드 중립이고 양쪽
recycler 가 `m_activeVersion` 을 들고 있다. 문제는 **꺼내는 어휘가 DX12 에만** 있었다는
것이다 — `DX12DescriptorRecycler::GetCurrentVersionToken()` 은 진작 있었는데 Vulkan
쪽에는 없었고, 중립 인터페이스(`IRenderDeviceServices`)에는 이 축의 낱말이 아예 없었다.
어휘 구멍이 백엔드 비대칭을 만든 자리다.

- `VulkanDescriptorPoolRecycler::GetCurrentVersionToken()` 을 더해 DX12 와 짝을 맞췄다.
- `IRenderDeviceServices::GetDescriptorVersionToken()` 으로 중립 계층에 올렸다.
  구현은 셋이다(DX12 · Vulkan · 자가 검사 대역). 대역은 recycler 가 없으므로 0 을
  돌려주고, **0 의 뜻을 인터페이스가 규정한다** — "기록 중인 버전 없음".
- 두 패스(`EnhancedGBufferPass` · `EnhancedForwardPass`)의 바인딩 기록이 그것을 적는다.
- 캡처가 `descriptorVersion` 을 방출한다.

★ **폭을 넓힌 것이 결정적이었다.** 칸은 `uint32` 였는데 토큰은 `generation << 32 | slot`
이다. 실측값이 `17179869185`(= `0x4_0000_0001`, generation 4 · slot 0)이므로 옛 폭
그대로였다면 윗 32비트가 잘려 **`1` 로 보였을 것이다** — 죽어 있던 칸이라 아무도
그것을 몰랐다.

### 단정하는 것과 재기만 하는 것을 갈랐다

게이트(`Assert-Capture`)에 더한 단정은 둘이다.

1. **조인이 성립한다** — 모든 draw 의 `seal.hash` 가 **자기 라우트의** 장부에서
   바인딩을 찾는다. 조인되지 않는 draw 는 manifest 에 있어도 신원이 없는 draw 다.
   라우트를 나눠 찾는 이유는, 두 장부를 합쳐 놓고 찾으면 gbuffer draw 가 forward
   장부의 항목에 붙어도 통과하기 때문이다.
2. **세 축이 0 이 아니다** — 0 은 "기록되지 않았다"이고 값이 아니다.
   `descriptorVersion` 이 방금까지 writer 0 이었으므로 그 시절과 반드시 구분되어야 한다.

★ **변이 폭은 재기만 하고 판정하지 않는다.** 실측은 `distinct pso 1~2 · sampler 1 ·
descriptorVersion 1` 이다. sampler 가 1 인 것은 지금 pass-global 이라 **옳다**(재질별로
가르는 것은 W7 이다). 여기서 "1 보다 커야 한다" 로 단정하면 W7 착수 전까지 이 게이트가
도는 세트에 있을 수 없다. 그래서 수는 매 실행 출력에 남기고 판정에서는 뺐다.

`descriptorVersion` 을 `operator==`(= 바인딩 신원)에 넣지 않은 것도 같은 이유다.
한 프레임에 기록이 둘 이상이면 같은 밀봉이 서로 다른 버전에서 잘릴 수 있고, 그때
신원으로 쓰면 거짓 충돌이 된다. 실측은 프레임당 하나를 가리키지만(캡처 셋 전부
distinct 1), **재 놓고 판정은 미룬다**.

### 변이 증명

예고: DX12 의 토큰 접근자가 0 을 돌려주면 **첫 캡처(`dx12-primitives`)에서**
`descriptorVersion=0` 으로 던지고, 변이 폭을 적는 식별 줄은 **찍히기 전에** 멈춘다.

```
verify-pbr-wiring-baseline.ps1:164
  gbuffer binding has descriptorVersion=0 for seal 5568184615101585538:
  ...\dx12-primitives
MUT_GATE_EXIT=1
```

예고한 자리에서 예고한 문장으로 붉었다. 복원 뒤 세 캡처 전부 조인 성립이고
식별 줄은 `distinct pso 1~2 · sampler 1 · descriptorVersion 1` 이다.

### 남은 것 (정직하게)

- **라우트별 조인은 데이터로 증명되지 않았다.** 이 fixture 들의 draw 는 전부
  `gbuffer` 라우트라 `forward` 장부가 비어 있다. 라우트를 나눠 찾는 것은 옳은
  설계지만, "합쳐 찾으면 통과했을 회귀" 를 실제로 자극한 적은 없다 — forward 로
  가는 draw(= `Transparent` 재질)가 있는 fixture 가 생겨야 선다.
- **W0 의 fixture 축은 그대로 남았다.** 이 슬라이스는 manifest 축만 닫았다.
  §4 가 적은 여덟 중 서 있는 것은 여전히 셋이고(primitives · Gunner · normal
  대조쌍), alpha mask · AO · emissive-only · 비균등 스케일은 없다. 그중 AO 와
  emissive 의 원본은 추적 밖 폴더에 있어 자산 소유 결정이 먼저다(§17 의 Gunner
  문제와 같은 뿌리).
- **descriptorVersion 은 아직 신원이 아니다.** 위에 적은 이유로 재기만 한다.

### 요구를 바꿨다 — "추적한다" 가 아니라 "조용히 통과하지 않는다"

§17·§18 이 fixture 추적 문제를 두 번 적었는데, 그 요구 자체가 과했다. 실측:
이 저장소의 CI(`.github/workflows/build.yml`)는 GPU 없는 `windows-2022` 에서 돌고
회귀 스크립트를 **하나만** 부른다(`verify-msbuild-tool-architecture.ps1`).
PBR 게이트는 CI 에서 돈 적이 없다. 그렇다면 라이선스 의무가 딸린 수 MB 외부 자산을
저장소에 넣는 것은 나쁜 거래다 — 배포되는 것은 검증 **결과**이지 fixture 가 아니다.

그래서 조건을 다시 적는다.

> ~~fixture 를 추적한다~~ → **게이트는 fixture 가 없는 축을 절대 PASS 로 보고하지 않는다**

추적은 그것을 보장하는 한 방법일 뿐이다. 다만 기여자가 다섯이므로 "로컬" 이 한 대가
아니고, 그래서 **없을 때 무슨 일이 일어나는지**는 반드시 정해져 있어야 한다.

### 축 회계

- 축마다 `ran` / `skipped: <이유>` 를 남긴다. 요약의 PASS 줄은 **돈 축만 이름을
  부르고**, 건너뛴 축이 있으면 "이 PASS 는 그 축에 대해 아무 말도 하지 않는다" 를
  덧붙인다.
- Gunner 캡처를 하드 의존에서 **선언된 선택 축**으로 바꿨다. 자산이 없으면 명령
  목록에서 빠지고 캡처 기대 수가 3 에서 2 로 줄며, 축 회계에 이유가 남는다.
- 교차 backend 비교도 **양쪽이 다 가진 축만** 맞댄다(한쪽에만 있는 축을 맞대면
  없는 디렉터리를 연다).
- 크기가 사실상 0 인 손수 만든 fixture 는 계속 저장소가 소유한다
  (`pbr-normal-pair` 18K · `gltf-multifile` 18K). 이것들은 자산이 아니라 **결함의
  재현체**이고 라이선스 의무도 없다.

★ **축 회계는 실패해도 찍는다.** 처음에는 성공 경로에만 두었는데, 이 게이트는 지금
뒤쪽 `experiment contract` 에서 기존 결함으로 멈추므로 회계가 영영 보이지 않았다.
무엇을 쟀는지는 실패했을 때야말로 알아야 한다 — `finally` 로 옮겼다.

### 증명

자산을 옮기지 않고 **게이트 쪽 경로를 없는 파일로 바꿔** 돌렸다. 에디터가 도는 중에
자산을 옮기면 watcher 가 `.meta` 를 지워 GUID 가 고아가 될 수 있다.

```
dx12 product capture PASS (primitives, normal-pair): ...
── 축 회계 ──
  dx12/primitives          ran
  dx12/gunner              skipped: .../Gunner_F_Mythic.glb 없음 (추적 밖 자산)
  dx12/normal-pair         ran
```

PASS 줄이 Gunner 를 부르지 않고, 이유가 남고, 캡처 수 기대도 따라 줄었다.

### fixture 둘을 더 채웠다 — 외부 자산 없이

`Tools/regression/fixtures/pbr-alpha-mask/` (504바이트 + 생성기). 쿼드 셋 · 재질 셋
(`OPAQUE` · `MASK` cutoff 0.5 · `BLEND`)이 **같은 baseColor 텍스처**를 쓰고, 그 알파가
사분면마다 0 / 0.25 / 0.75 / 1.0 이다.

★ 알파를 0/1 로만 두지 않은 이유: 그러면 cutoff 가 0.1 이든 0.9 든 결과가 같아
**cutoff 를 아예 읽지 않는 회귀가 통과한다.** 0.25 와 0.75 로 0.5 를 사이에 둔다.

**비균등 스케일 축은 자산이 아니라 씬 변환이라 공짜다** — 같은 캡처에
`object.transform ... 1.7 0.6 1.0` 로 겸한다. fixture 를 따로 만들 일이 아니었다.

### 이 fixture 가 예상 밖으로 연 것 — forward 라우트

실측: draw 11 중 **forward 1 · gbuffer 10**. `BLEND` 재질이 forward 로 간다.
coverageFlags 는 셋으로 갈린다(`1` · `11` · `5`), PSO 도 셋이다.

이것이 중요한 이유는 alpha 가 아니다. §18 의 draw↔바인딩 조인은 **라우트별로**
찾도록 짰는데, 여기 전까지 모든 fixture 의 draw 가 gbuffer 라 **forward 장부가 늘
비어 있었다** — 그 분기는 실행된 적이 없는 죽은 코드였고, §18 은 그 사실을
"자극된 적 없다" 로 적어 두었다.

변이로 확인했다. 조인을 라우트 무시(늘 `gbuffer` 장부에서 찾기)로 바꾸고 돌리니:

```
  draw identity: ... (dx12-primitives)     ← 통과
  draw identity: ... (dx12-gunner)         ← 통과
  draw identity: ... (dx12-normalpair)     ← 통과
  Draw seal 1046041037998211809 has no forward binding (신원 없는 draw)  ← dx12-alphamodes
```

앞의 셋은 전부 gbuffer 라 라우트를 무시해도 **구별되지 않는다**. 네 번째가 잡았다.
이 fixture 가 없었다면 그 변이는 조용히 통과했을 것이다.

### W0 의 fixture 축 현황

| fixture | 상태 |
|---|---|
| primitives | 섰다(저장소 소유) |
| Gunner helmet/armor | 섰다 — 단 **추적 밖 자산**이라 선언된 선택 축이다 |
| normal 대조쌍 | 섰다(저장소 소유 · §17) |
| alpha mask | 섰다(저장소 소유) |
| 비균등 스케일 | 섰다(씬 변환, 자산 아님) |
| AO | **없다** — 원본이 추적 밖 폴더에 있고 아직 배선하지 않았다 |
| emissive-only | **없다** — 위와 같다 |

★ AO 와 emissive 는 "건너뜀" 이 아니라 **"아직 없음"** 이다. 둘은 다르다 — 건너뛴
축은 게이트가 알고 이유를 적지만, 없는 축은 게이트가 모른다. 지금 축 회계에 그
둘은 나타나지 않으며, 이 표가 그 자리를 대신한다.

## 19. W7 네 번째 단위 — sampler 가 재질을 따른다, 2026-09-14

W7의 남은 단위다(normal/tangent는 §11, UV는 §12, mip 생성·소비는 §13에서 닫혔다).
6단 수직 슬라이스라 착수 전에 각 단이 어디까지 있는지 먼저 쟀다.

### 착수 전 실측 — 6단

| 단 | 실측 |
|---|---|
| 임포트 IR `TextureSlot` | `wrapU`/`wrapV`가 **이미 있다** — 그런데 **쓰는 자 0** |
| glTF 임포터 | `texture.samplerIndex`를 안 읽는다. `slot()`이 wrap을 안 채운다 |
| 저작 IR `TextureReference` | 샘플러 필드 없음 |
| 쿠킹 `CookedModelCodec` | `coordinates` 4필드만 직렬화 |
| 스냅샷 `EnhancedMaterialTextureBinding` | 자리 없음 |
| 패스 | `Initialize`에서 하나 만들어 root table(3/7)에 고정 |
| 장부 | `binding.samplerIdentity = m_samplerIdentity`(패스 전역) |
| `.shadermeta` | 샘플러 개념 **전무**(`ShaderPropertyType::Texture2D`만) |

### 발견 둘

**① `wrapU`/`wrapV`는 쓰는 자가 없는데 stable key에 들어 있었다.**
읽는 자가 딱 하나 — `ModelStableKeys.cpp:104`가 모델 지문에 넣는다. 쓰는 자가
0이니 **항상 기본값**이고 지문 기여 엔트로피가 0이었다. W0의 `descriptorVersion`과
같은 모양이되 더 나쁘다 — **신원에 참여하는 것처럼 보이는** 필드였다.

**② 임포터의 텍스처 캐시 키가 축을 통째로 지운다.**
`GltfImporter.cpp`의 `ResolveTexture`가 `imageIndex` 단독으로 캐싱한다. glTF에서
`texture = { source, sampler }`라 같은 이미지를 sampler만 달리해 참조하는 것이
정상인데(Khronos `TextureSettingsTest`는 image 3개를 texture 9개가 나눠 쓴다),
이미지 키로 접으면 9가 3이 된다.

★ **그러나 이 캐시를 고치는 것이 답이 아니다.** 샘플러는 *참조*의 성질이지
*이미지*의 성질이 아니므로, 샘플러를 `ImportedTexture`가 아니라 `TextureSlot`에
실으면 캐시는 그대로 두어도 옳다. 그리고 그 자리는 `coordinates`가 이미 쓰고 있는
자리다 — 실측이 설계를 정했다.

### RHI 어휘 구멍 둘 — 하나만 메운다

```
RHIFilterMode  : Point, Linear         ← Anisotropic 없음
RHIAddressMode : Wrap, Clamp, Border   ← Mirror 없음
```

`MIRRORED_REPEAT`(33648)에는 대체할 값이 없어 `RHIAddressMode::Mirror`를 더했다.
`Anisotropic`은 자극할 fixture가 없어 **더하지 않고 구멍으로만 기록한다**.

★ **변환표 셋이 모두 `default:` 낙하였다.** 열거자를 더해도 경고 없이 DX12는
`WRAP`, Vulkan은 `CLAMP_TO_EDGE`로 **서로 다르게** 접혔을 것이다. Editor는 /W0라
C4061/C4062에 기댈 수 없으므로(`nodiscard`가 힘을 잃는 것과 같은 이유), 표마다
`static_assert(kRHIAddressModeCount == 4)`를 두어 다음 추가가 컴파일에서 걸리게 했다.

### 설계가 기계적인 이유 — `coordinates`가 완주 레일이다

```
GltfImporter.slot()  →  TextureSlot.uvSet/offset/tiling/rotation
  → SceneToModelDraft:519  →  TextureReference.coordinates
    → CookedModelCodec(쓰기/읽기)  →  ExperimentMaterialSealing:330
      → binding.coordinates  →  MaterialKey.coordinates  →  패스
```

`coordinates`가 나오는 자리마다 옆에 `sampler`를 놓았다. `MaterialKey`에 들어가니
**배치가 샘플러별로 저절로 갈리고**, 그래서 draw마다 자기 테이블을 걸 수 있다 —
셰이더도 루트 시그니처도 `.shadermeta`도 건드리지 않는다.

★ **`.shadermeta` 스키마 결정: 더하지 않는다.** shadermeta는 셰이더가 *선언*하는
것을 적고, 샘플러는 재질이 *참조*와 함께 나르는 것이다. 여기 넣으면 같은 사실을
두 곳에 적게 된다.

### 착지

| 자리 | 무엇을 했나 |
|---|---|
| `RHIPipelineLayout.h` | `RHIAddressMode::Mirror` + `kRHIAddressModeCount` |
| 변환표 3곳 | Mirror 케이스 + 수 static_assert |
| `ImportedScene.h` | `TextureFilter` + `TextureSlot.filter`/`mipFilter` |
| `GltfImporter.cpp` | `ApplyGltfSampler` — samplerIndex를 읽어 slot에 싣는다 |
| `Assets/TextureSampler.h` | `assets::TextureSampler`(신설) |
| `ModelData.h` | `TextureReference.sampler` |
| `SceneToModelDraft.cpp` | import 어휘 → RHI 어휘 변환 **단일 지점** |
| `CookedModelFormat.h` | `kFormatVersion` 8 → 9 |
| `AuthoredMaterialDigest.h` | 샘플러만 다른 재질이 같은 지문을 갖지 않게 |
| `EnhancedRenderPass.h` | `binding.sampler` |
| `MaterialTextureTable.h` | `EffectiveSampler` |
| 두 패스 | `MaterialKey.sampler` · `SamplerTableFor` 캐시 · 배치별 `SetSamplers` |
| 장부 | `samplerIdentity`를 **이 배치가 걸 것**에서 |

`GetSamplerIdentity()`는 이제 **패스의 폴백** 신원이다 — 이름이 그 뜻을 담지 못해
양쪽 선언에 그 사실을 적어 두었다. 프레임이 실제로 건 것들은 장부 바인딩에 있다.

### fixture — `Tools/regression/fixtures/pbr-sampler/`

손으로 만들었다(6.5K). Khronos `TextureSettingsTest`가 이 축의 정본 자산이지만
83K·CC-BY(표시 의무)·`Dynamic_CPP/Assets/Models/*`가 ignore라, §18의 판단대로
**게이트 fixture는 직접 만들고** Khronos 것은 눈으로 보는 용도로 추적 밖에 둔다.

이미지 **하나**를 texture 3개가 sampler 3종(REPEAT / CLAMP_S / MIRROR+Point)으로
참조한다. UV를 **0..2**로 둔 이유는 `[0,1]` 안에서는 wrap이 무엇이든 결과가 같아
**wrap을 아예 읽지 않는 회귀가 통과하기** 때문이고, 이미지를 사분면마다 다른 색으로
그린 이유는 좌우 대칭이면 **MIRROR와 REPEAT가 같은 그림이 되기** 때문이다.

### 자극하지 못하는 축 — 주장하지 않는다

- **재질 안 슬롯별 분기.** 셰이더에 `gSampler : register(s0)` 하나뿐이라 표현할 수
  없고, `EffectiveSampler`가 "가장 낮은 레지스터가 이긴다"로 못 박았다. 저장소
  자산에 이 분기가 **0건**이다(`.gltf` 3종 · 텍스처 둘 이상인 재질 2건 · 분기 0).
- **Anisotropic.** 위 참조.

### 게이트

`Assert-SamplerModes`가 캡처의 `sealLedger.*.bindings[].samplerIdentity`에서
**서로 다른 값이 2 이상**인지, 그리고 **0이 섞이지 않았는지**를 묻는다. W7 이전에는
이 수가 어느 캡처에서든 항상 1이었고, §18의 게이트는 그 수를 **세기만 하고 판정하지
않았다** — 변이를 만들 슬라이스가 바로 여기였기 때문이다. 축 회계에 `<api>/sampler`가
추가된다.

### 배선을 다 잇고도 축이 죽어 있었다 — 그리고 장부가 옳았다

첫 실행에서 `distinct samplerIdentity` 가 **1** 이었다. 홉을 셋 더 찾아 채웠는데도
1이었다. 빠진 홉은 실재했다:

| 놓친 홉 | 무엇이 없었나 |
|---|---|
| `assets::ModelMaterialTexture` | 필드 자체가 없어 저작→모델자산 경계에서 소실 |
| `ExperimentMaterialMigration` (모델자산→스냅샷) | 복귀에서 소실 |
| `MaterialAuthoringCodec` | `.material` 저작 저장/읽기에서 소실 |

★ 저작 코덱은 **기본값이 아닐 때만** 키를 적는다. 이 코덱은 미지 키를 fail-closed 로
거부하므로, 무조건 적으면 기존 `.material` 전부가 다음 저장에서 키 넷을 얻고 옛 리더가
새 파일을 못 읽는다.

그런데 셋을 다 채우고도 여전히 1이었다. **진짜 원인은 배선이 아니었다.**
`EnhancedMaterialSealHash.h` 의 `AppendTextureBinding` 이 `coordinates` 는 접고
`sampler` 는 안 접었다. 그래서 wrap 만 다른 재질 셋이 **같은 `sealHash`** 를 갖고,
W8 의 불변식("같은 seal 은 같은 바인딩")이 **옳게 발동해** draw 둘을 버렸다 —
`bindingConflict 2 · skipped 2 · lastReason "같은 seal에 서로 다른 texture/sampler
배치가 그려졌다"`. 살아남은 하나만 장부에 남으니 distinct 가 1로 보인 것이다.

★ 그래서 게이트가 distinct 만 세면 부족하다. `Assert-SamplerModes` 는
`bindingConflict`·`skipped` 를 함께 묻는다 — 그 둘이 "축이 살았다"와 "축이 충돌해
죽었다"를 가른다. 이번에 그 숫자가 원인을 한 번에 가리켰다.

### 실측 (2026-09-14)

빌드 exit 0 · 오류 0. dx12 **축 다섯 전부 PASS**:

```
draw identity: distinct pso=1 sampler=1 descriptorVersion=1 (dx12-primitives)
draw identity: distinct pso=2 sampler=1 descriptorVersion=1 (dx12-gunner)
draw identity: distinct pso=2 sampler=1 descriptorVersion=1 (dx12-normalpair)
draw identity: distinct pso=3 sampler=1 descriptorVersion=1 (dx12-alphamodes)
draw identity: distinct pso=3 sampler=3 descriptorVersion=1 (dx12-samplermodes)
dx12 product capture PASS (primitives, gunner, normal-pair,
                            alpha-modes+nonuniform-scale, sampler)
```

vulkan 도 같다 — `vulkan-samplermodes` 가 `draws 18 · bindings 18 · distinct 3 ·
validation 0 · 충돌/생략 0` 이고, **세 신원 값이 dx12 와 같다**
(`2677747970098898095` · `2987389514037885135` · `13823872880576982764`).

### 쿠킹 버전 올림의 후속 — 재임포트와 쐐기 하나

`kFormatVersion` 8→9 로 기존 캐시가 전부 fail-closed 거부된다(설계된 동작). 모델 17종을
재임포트했고, 이는 선례가 있는 정규 절차다(`4106b5c7` "Prim 코퍼스 재임포트로
generation 을 갱신한다").

★ 그 과정에서 **`Prim_Cube` 하나만 결정적으로 임포트에 실패**했다. 내용은 다른
primitive 와 구조가 같았고 다른 것은 **상태**였다 — sidecar 는 `generation: 7` 인데
Library 에 8~15 가 남아 있어, `prior+1 = 8` 을 요구하는 `publish.preflight` 가 영구히
거부했다. `.meta` 는 추적되고 `Library/` 는 ignore 라, sidecar 가 git 으로 되돌려지는
동안 파생 Library 가 앞서 나가면 그 자산은 다시는 임포트되지 않는다. 코드는 옳게
fail-closed 고 상태만 쐐기로 박힌 것이다. sidecar 보다 큰 고아 디렉터리 여덟을 지워
풀었다(파생물이라 재임포트로 복구된다). 전수 점검 결과 쐐기는 17종 중 이 1건뿐이었다.

### 게이트가 끝까지 못 가는 이유 둘 (W7 밖)

1. **vulkan 스카이박스 기동 창.** `vulkan-primitives` 가 GPU 검증으로 실패한다 —
   `gCubeMap`(Set 0, Binding 100)이 기록되지 않은 셋으로 그려진다. 경고는 앞쪽
   구간에만 몰리고 그 뒤로 멎어, 캡처 1(frame 2040)만 그 창 안이고 캡처 2~5
   (2076 이후)는 전부 통과한다. 기제는 `VulkanEncoder::SetBindings` 가 이미지 뷰
   생성에 실패하면 pending 을 넣지 않고 돌아가는데 `Draw` 는 그대로 진행하는 것이다.
   **sampler 와 무관하다** — 경고는 sampler fixture 적재 전에 나고, sampler 축이
   갈리는 캡처들은 모두 통과한다. 별건으로 넘겼다.
2. **`verify-experiment-contract.ps1` 링크 부패** — §15 가 적어 둔 기존 결함이다.

게이트의 `wait` 를 늘려 1번의 창을 피하지 않았다. 그러면 재는 척만 하게 된다.
