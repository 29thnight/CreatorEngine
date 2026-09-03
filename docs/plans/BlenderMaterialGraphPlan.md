# Blender형 PBR·Material Graph 계획 (PHASE 4.25)

**신설 2026-09-03 · 10슬라이스 34일 · 미착수 · 선행 PHASE 4**

아트 팀이 Blender에서 만든 재질 의도를 CreatorEngine에서 같은 방식으로 이해하고 예측할 수
있게 만드는 계획이다. 목표는 Blender 자체의 모든 렌더 기능 복제가 아니라, **Blender 5.1.1
Principled BSDF/OpenPBR 계열의 재질 구성과 pre-tone linear HDR 결과**를 게임 엔진 비용 모델
안에서 일치시키는 것이다.

PHASE 4가 현재 glTF PBR 배선을 안정화한 뒤 시작한다. RenderGraph·그림자·reflection probe·
후처리와 generic Custom Pass authoring은 PHASE 4.75가 소유한다.

---

## 1. 고정한 비교 범위

### 포함

- Blender 5.1.1 Material Preview/EEVEE의 Principled 재질 의미.
- Base Color, Metallic, Roughness, IOR/Specular IOR Level, Normal, Alpha,
  Emission Color/Strength.
- Coat, Sheen, Anisotropy, Iridescence, Transmission, Subsurface와 Volume 입력.
- 같은 mesh·UV·texture·HDRI·light·camera에서 tone mapping 전 linear HDR 비교.
- Material Graph 저장/재개방, typed node/pin, deterministic Slang codegen.
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

---

## 3. 최종 구조

```text
.shadergraph (domain=material)
    -> typed Graph IR
    -> constant fold / dead-lobe elimination / feature extraction
    -> generated MaterialInputs + MaterialFeatureMask
    -> shared Slang EvaluateMaterial
    -> PrincipledSurface
         ├─ Standard : Deferred
         ├─ Layered  : specialized Deferred 또는 Forward
         └─ Special  : Forward (transmission/subsurface/volume)
```

핵심 규칙:

1. `PrincipledSurface`는 재질 평가 결과의 유일한 논리 ABI다.
2. `MaterialFeatureMask`는 graph를 훑어 얻는 정적 feature 집합이다. artist가 pass/register를
   직접 고르지 않는다.
3. Deferred와 Forward는 같은 `EvaluateMaterial`·BRDF·IBL 모듈을 소비한다. route 변경이
   색 변화가 되어서는 안 된다.
4. graph topology와 feature permutation은 cook 때 고정한다. runtime instance는 값과 texture만
   override한다.
5. 무한 uber shader 하나를 만들지 않는다. coarse tier permutation + specialization + cook-time
   constant folding으로 variant 폭과 분기 비용을 제한한다.

---

## 4. 아트 팀용 비용 계약

| Tier | 기본 route | 재질 기능 | 저작 경험 |
|---|---|---|---|
| Standard | Deferred | base/MR/normal/IOR/specular/emission/mask | 기본값. 가장 싼 green 배지 |
| Layered | specialized Deferred 또는 Forward | coat/sheen/aniso/iridescence | yellow 배지와 비용 증가 이유 표시 |
| Special | Forward | transmission/subsurface/volume | red 배지, 겹침/화면 점유 비용 경고 |

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
| `MAT-0` | Blender 5.1.1 reference scene·pre-tone HDR golden 고정 | · | PHASE 4 W9 | 2 |
| `MAT-1` | `PrincipledSurface`·`MaterialFeatureMask` 공용 Slang ABI | · | MAT-0 | 4 |
| `MAT-2` | typed Graph IR·domain·schema migration·round-trip | · | MAT-1 | 4 |
| `MAT-3` | core Principled 의미·기본값·IOR/specular/emission/alpha | · | MAT-1 | 4 |
| `MAT-4` | coat·sheen·anisotropy·iridescence layered lobe | · | MAT-3 | 4 |
| `MAT-5` | transmission·subsurface·volume와 Special route | · | MAT-3 | 4 |
| `MAT-6` | material-domain graph→deterministic Slang codegen·diagnostic | · | MAT-2, MAT-3 | 4 |
| `MAT-7` | Standard/Layered/Special 자동 route·cook specialization | · | MAT-4~MAT-6 | 3 |
| `MAT-8` | artist Inspector·preview·cost badge·unsupported 설명 | · | MAT-2, MAT-7 | 3 |
| `MAT-9` | Blender material grid/furnace·route parity·성능 gate | · | MAT-7, MAT-8 | 2 |
| **합계** |  |  |  | **34** |

구 PHASE 4의 `SRP-3` 공용 graph 기반과 단일 PBR 레인의 material 의미·lobe·codegen 몫을
이 열 개 슬라이스가 대체한다. 옛 PBR ID는 새 작업과 병행하지 않는다.

---

## 6. 완료 기준

- `.shadergraph(domain=material)` 저장→닫기→재개방 뒤 node/pin/connection/layout/Blackboard,
  default, color-space intent와 subgraph가 보존된다.
- unknown node와 schema migration 실패는 graph와 마지막 정상 compiled generation을 보존한다.
- Blender reference의 core·layered·special material grid가 pre-tone linear HDR 허용 오차를 통과한다.
- 같은 지원 feature의 Deferred/Forward route 교차 비교가 허용 오차를 통과한다.
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

- compiled material graph generation.
- `PrincipledSurface` ABI와 `MaterialFeatureMask`.
- 자동 선택된 Standard/Layered/Special route.
- backend-neutral material resource table과 variant/cost metadata.

PHASE 4.75의 Pipeline Asset이나 Custom Pass는 이 계약을 소비할 수 있지만, Principled 의미,
Material Graph schema 또는 artist 비용 tier를 다시 정의하지 않는다.
