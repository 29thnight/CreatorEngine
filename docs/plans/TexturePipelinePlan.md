# 텍스처 파이프라인 — 임포트 · cook · 런타임 소비

작성: 2026-09-04 · 방향 개정: 2026-10-09

상태: **구현 중 — T0~T2, 실행 검증 전**

대시보드: **PHASE 12**

대상: 기존 `Engine/RenderEngine`, `Engine/SceneRuntime`, `Engine/AssetPipeline`,
`Engine/Experiment/Cooked`, `Editor/EngineEntry`, `Tools/AssetCooker`

관련 문서: `TextureCodecBoundaryDesign.md`, `BuildPipelinePlan.md`(PHASE 12.5),
`MaterialPipelinePlan.md`, `SerializationPlan.md`, `ModelAssetBigBangCutoverPlan.md`,
`EngineLayerSeparationPlan.md`

---

## 1. 현재 결정과 범위

**DirectXTex를 엔진이 직접 사용하는 유일한 이미지 처리 의존성으로 유지한다.**
디코드·리사이즈·밉 생성·압축·DDS 입출력·PNG 기록은 DirectXTex로 처리한다.
색공간·노멀맵·압축·밉·최대 크기와 cooked 자산의 신원 및 소비 정책은 엔진이 소유한다.
wrap/filter는 참조별 `TextureSampler`가 소유하며 image recipe/cache key에 포함하지 않는다.

- `stb_image` / `stb_image_write` 직접 사용을 제거한다. ImGui 내부에 포함된 stb
  폰트·사각형 배치 구현은 이 결정의 대상이 아니다
- stb 이관, `bc7enc_rdo`, 자체 DDS 파서, 실사용 없는 `TextureCodec.*` 프로젝트를
  만들지 않는다. 기존 프로젝트와 코덱 경계를 사용한다
- 런타임 밉 생성은 이미 구현돼 있다. **밉 기능 신설이 아니라 cook 정책 및 산출물로
  옮기는 일**이다. 기존 런타임 경로와의 정합성을 확인한다
- T0~T2는 구현 중이다. 현재 변경의 빌드·테스트·컴파일러·셰이더·바이너리·벤치마크는
  실행하지 않는다. 코드·문서 변경과 정적 검토를 실행 검증 통과로 표시하지 않는다
- T3는 실제 모바일/Web 타깃과 형식 요구가 정해질 때만 연다. WIC의 플랫폼 제약이
  존재한다는 이유만으로 지금 대체 라이브러리나 빈 프로젝트를 추가하지 않는다

목표는 **cook 시점에 처리하고, cooked 런타임 경로는 GPU-ready 바이트를 읽는 것**이다.
에디터의 원본 미리보기·Terrain 저작 데이터 등 기존 소스 경로는 구분해 유지한다.
T2 일부를 연결했다고 게임 런타임 전체의 코덱 링크가 0이 됐다고 주장하지 않는다.

```
저작 이미지 → .meta importSettings → cook(디코드·정책·밉·압축)
                                           ↓
                                  GPU-ready artifact
                                           ↓
                       cooked 런타임: 검증 → 서브리소스 업로드
```

## 2. 역사적 기준선 — 현재 상태나 새 검증 결과가 아님

아래 수치와 코퍼스는 **2026-09-04 기록**이다. 이후 코드·자산 변경에 따른 현재 값은
측정하지 않았다. 당시의 “GenerateMipMaps 호출 0건”은 현재 상태 설명에서 철회한다.

### 2.1 당시의 파이프라인

- `TextureCookProducer`는 GUID·내용 해시·manifest 계약을 갖춘 원본 pass-through였다
- 모델 임베디드 텍스처 generation은 `textures/<TextureId>.png` 원본을 저장했다.
  메모리의 RGBA8 픽셀과 디스크 PNG artifact를 혼동하지 않는다
- PHASE 3.75에서 이관한 `textures-read+sha+decode` 비용은 당시 generation 로드의
  66~80%였다. 512² 1장 2.6~3.6 ms, Gunner 6장 46 ms, scene 69장 551 ms로
  기록됐고 실행별 ±30% 변동이 있었다. 이 수치는 현재 회귀 감시 통과를 뜻하지 않는다
- 당시 예산 검사 `verify-model-cutover-budget` 및 archive는 이후 은퇴했다는
  대시보드 이력이 있다. 살아 있는 실행 게이트로 간주하지 않는다

### 2.2 당시 코퍼스 (2026-09-04)

| 종류 | 기록 | 주의 |
|---|---|---|
| PNG | 583 | 저작 Assets 99 · generation 411 · 아이콘 등 73; 모두 8비트 |
| HDR | 19 | 4096×2048, RGBA32F 장당 128 MB |
| DDS | 1 | blueNoise.dds, BC3, 밉 8단 |
| TGA/JPEG/BMP/EXR | 0 | 현재 자산 수를 뜻하지 않음 |

별도의 당시 PNG 디코더 대조는 685장으로 기록됐다(583장 집계와 같은 범위가 아님).
DirectXTex/stb RGBA8 대조는 일치했고 HDR 19장은 RGBE 해석 차이가 있었다.
**그 대조는 종료된 라이브러리 선택 조사다. 신규 왕복 검사의 통과 증거나 향후 게이트가 아니다.**

### 2.3 당시 성능과 계산값 (2026-09-04)

| 축 | 당시 값 | 성격 |
|---|---|---|
| Release PNG 99장 읽기 | 0.58 ms/장 | 실측 |
| 디코드 | 3.52 ms/장 | 실측 |
| 밉 생성 | 8.52 ms/장 | 실측 |
| BC1 압축 | 22.1 ms/장 | 실측 |
| cook 합계 | 34.1 ms/장 | 실측 |
| 런타임 로드 | 5.34 ms/장 | 실측 |
| RGBA8 무밉 510장 | 420.4 MB | VRAM 계산 |
| BC1+밉 510장 | 70.1 MB | VRAM 계산 |
| 알파 BC3/불투명 BC1+밉 | 117.0 MB | VRAM 계산 |

이 수치로 현재 압축기가 병목이라고 단정하거나 다른 압축기 도입을 승인하지 않는다.

## 3. 기존 경계와 보존 계약

`TextureCodecBoundaryDesign.md`가 세운 불투명 픽셀 소유권과 `TextureImageView`,
RHI 포맷·블록 피치 어휘를 사용한다. DirectXTex 타입을 새 런타임 공개 표면으로 확산하지
않고, GPU-ready 서브리소스의 포맷·치수·rowPitch·slicePitch·밉/배열 순서를 검증한다.

### Terrain 데이터는 색 이미지가 아니다

- 높이 PNG는 float 비트 31..24, 23..16, 15..8, 7..0을 순서대로 RGBA8에 저장한다
- **알파도 데이터다.** 감마 변환, premultiply/unpremultiply, 불투명 판정에 따른
  알파 제거, BGRA/RGBA 혼동을 허용하지 않는다
- PNG 저장 형식을 RGBA8 또는 gray8로 명시한다. 높이는 정규화된 RGBA8 바이트를
  행 피치에 맞춰 읽고 float 비트로 복원한다
- splat의 `uint8_t(clamp(weight, 0, 1) * 255)` 절삭과 `/255.0f` 복원은 유지한다
- 컬러 brush의 회색 변환은 기존 정수 식 `(77*R + 150*G + 29*B) >> 8`을 유지한다.
  라이브러리의 다른 휘도·감마 정책으로 조용히 바꾸지 않는다

## 4. 라이브러리 및 산출물 정책

| 역할 | 구현 |
|---|---|
| PNG/JPEG/BMP 디코드 및 PNG 기록 | DirectXTex WIC 경로 |
| HDR/TGA/DDS | DirectXTex 기존 로더 |
| 리사이즈·밉·BC 압축 | DirectXTex |
| import 정책·키·manifest·cooked 검증 | 엔진 자체 코드 |
| 모바일/Web | 실제 타깃 확정 후 별도 판단 |

DDS 저장·읽기가 필요하면 DirectXTex의 DDS API를 사용한다. 엔진 artifact의 버전·신원·
검증 래퍼는 이미지 디코더나 DDS 파서를 직접 만드는 명분이 아니다. source bytes,
import settings, target policy, format version이 cooked 결과 식별에 반영돼야 한다.

## 5. 구현 슬라이스

### T0 — 임포트 설정과 저작 연결 (구현 중)

- 기존 `.meta`의 `importSettings`에 색공간, 압축, 밉, 최대 크기,
  노멀맵 정책을 추가하고 round-trip한다. wrap/filter는 참조별 `TextureSampler`에 남긴다
- 필드가 없는 기존 자산은 명시적 기본값으로 읽는다. 확장자·timestamp 등 기존 필드와
  다른 자산의 설정을 보존한다
- 편집은 에디터의 소유 경계를 통해 저장하고 변경된 설정을 재cook/재로드에 반영한다
- 기본값, material slot 추론 및 명시 설정의 우선순위를 일관되게 적용한다

### T1a — DirectXTex cook 트랜스코딩 (구현 중)

- 기존 `TextureCookProducer`의 pass-through를 GPU-ready 산출물 생성으로 전환한다
- 디코드 → 정책 적용/리사이즈 → 밉 생성 → 압축 → artifact 기록 순서를 구현한다
- 압축 실패·지원하지 않는 포맷·선택한 밉 정책과 다른 chain을 성공으로 게시하지 않는다.
  `PreserveAuthored`의 유효한 부분 mip chain은 그대로 보존한다. `GenerateFull`은
  전체 chain을 생성해야 하며 잘못된 metadata·피치·개수는 어느 정책에서도 거부한다
- artifact 버전 및 producer 식별자를 갱신하고 구버전/정책 불일치 산출물을 거부한다
- 모델 임베디드 텍스처도 같은 GPU-ready 표현과 검증 정책으로 연결한다
- 새 코덱 프로젝트나 추상화의 선행 설치 없이 기존 프로젝트 경계를 유지한다

### T2 — cooked 런타임 소비 연결 (구현 중)

- 독립 텍스처와 모델 generation 소비자가 GPU-ready artifact를 검증하고 업로드한다
- cooked 자산의 오류를 원본 디코드 fallback으로 숨기지 않는다. 에디터 원본 경로와
  cooked runtime 경로를 명시적으로 구분한다
- 포맷·치수·밉·배열·서브리소스 크기 및 manifest/hash 정합성을 확인한다
- 참조별 `TextureSampler`의 wrap/filter를 보존한다. image recipe 및 image cache와
  분리하며 sampler 차이로 동일 이미지의 cook/디코드를 반복하지 않는다
- 이 경로에서 디코드·압축·밉 생성이 재실행되지 않는 것은 중간 단계다.
  **T2 전체 완료에는 Player의 실제 소스 편입·링크에서 이미지 처리 의존성을 분리하는
  경계 작업도 필요하다.** 별도의 source/editor/Terrain 경로가 남는 동안 코덱 0이나
  T2 완료를 선언하지 않는다. 이 경계는 미완료 조건으로 유지한다

### T1b — 품질 및 추가 포맷 판단 (품질 측정 보류)

BC5/BC7의 구조적 형식·백엔드 업로드·노멀 Z 복원 지원은 T0~T2 구현에 포함해 진행한다.
해당 형식의 품질 튜닝·시간·메모리 수용 측정은 미실행이며 별도 판단으로 남긴다.
BC6H는 실제 필요를 확인한 뒤 기존 DirectXTex 안에서 판단한다. DirectXTex 청산,
다른 압축기 도입, 자체 DDS 파서는 범위가 아니다.

### T3 — 모바일/Web (착수 조건 미충족)

실제 타깃·툴체인·GPU 형식·호스트 이미지 입력 요구가 확정되면 정책을 결정한다.
그 전에는 ASTC/Basis 프로젝트·의존성·미사용 등록 코드를 추가하지 않는다.

## 6. 소유와 의존성

- 이미지 처리 구현은 기존 코덱 경계와 authoring 소유 위치에 둔다
- Terrain 파일 쓰기는 `EditorAssetDatabase`가 소유하고 런타임 Terrain은 읽기만 한다
- 엔진 직접 stb 사용이 모두 사라진 뒤에만 루트 vcpkg의 직접 `stb` 의존을 제거한다.
  외부 라이브러리 내부 구현이나 전이 의존을 억지로 제거하지 않는다
- **Player의 실제 소스 편입·링크 경계에서 이미지 처리 의존 제거는 T2의 열린 완료
  조건이다.** decode-free 소비만으로 T2를 닫지 않는다. 기존 프로젝트에서 실제 소비를
  분리하며, 아직 존재하지 않는 `TextureCodec.*` 프로젝트나 코덱 0개를 달성 상태로 쓰지 않는다

## 7. 검증 계획 — 이번 변경에서는 실행하지 않음

| 축 | 향후 실행 시 확인할 계약 |
|---|---|
| T0 | 기존 sidecar 기본값 · 설정 보존 · invalid enum/range 거부 · 정책 변경 시 cache key 변화 |
| T1 | 실제 PNG/HDR/DDS cook · 밉 정책별 치수/피치 · PreserveAuthored 부분 chain 보존 · 손상 artifact 거부 |
| T2 | 독립/모델 cooked 실제 소비 · 소스 없는 패키지 · 손상/version/hash 불일치 거부 |
| PNG 정본 왕복 | DirectXTex RGBA8 → 명시 RGBA PNG → RGBA8 바이트 일치, 알파 포함 |
| HDR 정본 왕복 | DirectXTex HDR → float DDS → 재로드 float 비트 일치 |
| Terrain | float bits/alpha 전수 왕복 · gray8 절삭 · 컬러/회색 brush 변환 일치 |
| 업로드 | 기존 `vk.texturecodec` 및 DX12 픽셀 검증의 실제 가용성과 결과 확인 |
| 의존성 | 엔진 직접 stb include/심볼 0 · ImGui 내부 헤더 불변 |

기존 `assets.decodeab` / `assets.decodeabhdr` 이름을 호환상 남긴 경우에도 결과와 설명은
**DirectXTex 정본 왕복**으로 바꾸고, stb A/B 또는 독립 디코더 검증이라고 부르지 않는다.
동일한 디코더의 왕복은 외부 디코더와의 정확도 대조가 아니며 품질 판정도 아니다.
새로 작성한 검사 스크립트·프로브는 실행 전 상태로 보고한다. 변이 검증도 실행하지 않는다.

## 8. 완료 조건과 미검증 항목

T0~T2의 완료는 설정 저작부터 실제 cooked 소비까지 닫힌 경로, Terrain 바이트 보존,
구버전·손상·설정 불일치 거부, Player의 실제 소스/링크 코덱 경계 제거, 그리고 허가된
환경에서의 실행 검증으로 판단한다. decode-free 소비 단계만으로 전체 T2 완료는 아니다.

2026-09-04에 제안했던 런타임 <1 ms/장, VRAM <120 MB, cook <50 ms/장,
저장→화면 +50 ms는 **역사적 목표**다. 현재 플랫폼·코퍼스에서 다시 측정하기 전에는
달성 여부도 압축기 교체 시 개선 예측도 쓰지 않는다.

현재 미검증: 빌드/링크, 코덱 왕복, Terrain/brush 패리티, GPU 업로드, 모델·독립 텍스처
패키지 소비, 재cook/재로드 지연, 디스크 크기, 품질 및 성능. 이번 작업의 실행 금지 조건을
지키며 검증 대기 상태로 남긴다.

## 9. 하지 않는 것

- stb 이관·bc7enc/ISPC 도입·자체 DDS 파서
- speculative 코덱 프로젝트, 모바일/Web 선행 구현
- 텍스처 스트리밍 및 범위 밖 렌더러 재설계
- 과거 수치를 현재 측정이나 새 검사의 통과로 재사용
- 빌드·테스트·컴파일러·셰이더·바이너리·벤치마크 실행
