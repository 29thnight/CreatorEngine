# PBR 배선 안정화 계획 (PHASE 4)

**신설 2026-09-03 · 10슬라이스 18일 · 구현 진행 중 · 런타임 검증 미실행**

이 계획은 현재 제품 렌더 경로의 `.slang`·머테리얼·렌더러 배선 결함만 닫는다.
Blender형 Material Graph와 Principled 확장은
[`BlenderMaterialGraphPlan.md`](BlenderMaterialGraphPlan.md), **PHASE 4.25**가 소유한다.
RenderGraph·일반 Custom Pass·그림자·reflection probe·후처리는 **PHASE 4.75**로 보낸다.

좌표계 변환 자체는 Blender 시각 동등성의 범위가 아니지만, 현재 제품 결함인 UV set/transform,
sampler/mip, tangent basis와 non-uniform scale normal transform은 이 페이즈에서 수정한다.

---

## 1. 2026-09-03 소스 감사 결과

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
| `PBR-W2` | GBuffer/Deferred/Forward native Slang 제품 진입점·공용 현행 평가 | · | W0 | 2.5 |
| `PBR-W3` | backend neutral resource·binding·종료 코드 동등성 | · | W0 | 1 |
| `PBR-W4` | OPAQUE/MASK/BLEND·alpha cutoff·double-sided/cull | · | W2 | 2 |
| `PBR-W5` | AO 소비·고정 4 texture slot 제거·GBuffer packing 검토 | · | W2, W3 | 2.5 |
| `PBR-W6` | emissive factor/strength·constant-only emission·색공간 | · | W2 | 1.5 |
| `PBR-W7` | UV set/transform/sampler/mip·inverse-transpose normal/tangent | · | W2 | 2 |
| `PBR-W8` | material/descriptor/PSO generation 원자 밀봉·플리커 fail-closed | · | W3~W7 | 2 |
| `PBR-W9` | DX12/Vulkan 실장면·장시간·재임포트 회귀와 cutover | · | W8 | 1.5 |
| **합계** |  |  |  | **18** |

`PBR-W0`은 정적 감사 절반, `PBR-W1`은 코드·빌드 절반을 기성으로 센다. 둘 다 `PBR-W9`의
실장면 acceptance 전에는 완료가 아니다.

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
- normal/tangent basis는 inverse-transpose와 handedness를 보존한다.

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
