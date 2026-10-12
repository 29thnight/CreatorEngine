# 텍스처 파이프라인 — 임포트 · cook · 런타임 소비

작성: 2026-09-04 · 방향 개정: 2026-10-09

상태: **수용 단위 완료 13/20일 — 65%; T0~T2 잔여 검증 진행; T1b 대기; T3 중단**

후속 실행 결과 정리: 2026-10-10 KST. PR 병합 날짜와 과거 기준선 날짜는 별도로 보존한다.

대시보드: **PHASE 12**

대상: 기존 `Engine/RenderEngine`, `Engine/SceneRuntime`,
`Engine/RenderEngine/Experiment/Cooked`, `Editor/EngineEntry`, `Tools/AssetCooker`, `BuildTool`

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
- T0~T2 구현은 PR #166으로 master에 병합됐다. 해당 PR에서는 빌드·테스트·컴파일러·셰이더·바이너리·벤치마크를
  실행하지 않았다. 코드·문서 변경과 정적 검토를 실행 검증 통과로 표시하지 않는다
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
- PNG 입력을 RGBA8 또는 gray8로 명시한다. Windows PNG 인코더가 지원하는
  straight BGRA8 target으로 저장하고 읽을 때 FORCE_RGB로 RGBA 순서를 복원한다. 높이는 정규화된 RGBA8 바이트를
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

### T0 — 임포트 설정과 저작 연결 (구현 적용 · 부분 검증)

- 기존 `.meta`의 `importSettings`에 색공간, 압축, 밉, 최대 크기,
  노멀맵 정책을 추가하고 round-trip한다. wrap/filter는 참조별 `TextureSampler`에 남긴다
- 필드가 없는 기존 자산은 명시적 기본값으로 읽는다. 확장자·timestamp 등 기존 필드와
  다른 자산의 설정을 보존한다
- 편집은 에디터의 소유 경계를 통해 저장하고 변경된 설정을 재cook/재로드에 반영한다
- 기본값, material slot 추론 및 명시 설정의 우선순위를 일관되게 적용한다


### 이번 구현의 정책과 남는 범위

- **독립 텍스처 기본값:** `.meta`에 설정이 없으면 `Source / None / PreserveAuthored`다.
  `None`은 추가 압축을 하지 않는다는 의미로 원본 DDS 블록은 유지한다. 기존 재질의
  런타임 compress/mip 요청으로 cooked 바이트를 다시 가공하지 않는다. 따라서 기존
  역할별 자동 BC1·밉 생성과 VRAM/필터링 결과가 같다고 주장하지 않는다. 원하는 정책을
  임포트 설정으로 명시하고 재cook해야 한다
- **모델 임베디드:** 단일 역할은 역할 기반 색공간·밉 기본값을 추론하고 명시 설정이
  이를 덮어쓴다. 동일 이미지의 혼합 역할은 중립·무가공 표현을 공유하거나, 이미 필터링된
  서로 충돌하는 정책을 명확한 오류로 거부한다. 첫 역할의 정책으로 나머지를 조용히 처리하지 않는다
- **변형의 한계:** 현재 카탈로그의 한 논리 텍스처는 하나의 authored cook 정책을 갖는다.
  같은 이미지의 서로 다른 필터·압축 결과를 역할별로 동시에 선택하는 다중 artifact
  참조 확장은 이 변경의 완료 범위가 아니다. 같은 유효 산출물의 decoded payload 공유는 유지한다
- **설정 반영:** 기존 job 시스템에서 소유한 입력으로 재cook하고 최신 요청만 새 세대로
  게시한다. 설정 편집은 공통 저장 대기열에 자동 반영한다. 새 description/image 준비 후
  열린 씬의 ImageComponent·SpriteRenderer·SpriteSheet·Decal·재질 참조를 자동 교체한다.
  씬을 다시 열지 않으며 미저장 엔티티/UI 값과 Undo 이력을 유지한다. 세대 교체 전
  프레임의 소유는 유지한다. 현재 실행 수용은 §16과 후속 검증 기록에서 별도 판정한다
- **버전:** CECT texture representation/schema는 2다. AssetSet의 CEMF manifest 3과
  다른 계약이다. 기존 원본 pass-through artifact와 receipt는 다시 cook해야 한다
- **BC5 그래프:** 표준 재질과 생성 재질 그래프의 노멀 복원 및 private encoding uniform을
  함께 다룬다. 그래프 compiler 3 / program artifact 5로 이전 캐시를 무효화한다.
  private word 외의 immutable uniform 비교는 유지한다
- **Player 경계:** DirectXTex 소스 처리는 Editor·AssetCooker 등의 오프라인 프로젝트만
  컴파일하도록 분리한다. 공유 런타임은 기존 AssetAuthoringPort의 중립 콜백을 사용하며
  Player에는 소스 이미지 디코더를 등록하지 않는다. 실제 바이너리의 링크 결과는 미검증이다
- **Terrain 패키지:** 기존 저작 파일과 구분한 cooked terrain v2가 높이 float, gray8 splat,
  레이어 정보·참조 및 GPU-ready diffuse를 보관한다. 새 패키지에는 cooked 표현을 넣고
  Player는 이미지 코덱 없이 읽어야 한다. 원본 PNG 소비를 남긴 채 링크만 제거하지 않는다
  중첩 CECT와 파생 CPU 작업 집합(최대 1 GiB)을 할당 전에 검사하며, 같은 reader를
  producer 검증에도 사용해 런타임이 항상 거부할 대형 지형을 게시하지 않는다
- **기본 blue noise:** 기존 BC3 DDS의 8개 authored mip를 그대로 CECT로 옮겨
  `Resources/VolumetricFog/blueNoise.cetex`에 배포한다. Player는 이 cooked 리소스만
  소비한다. 새 Editor 개발 환경에서 아직 리소스가 없으면 설치된 authoring 콜백을
  통해 기존 worker에서 원본 DDS를 읽을 수 있으며, Player에는 그 fallback이 없다
- **Decal:** 기존 이름 필드는 오프라인에서 정확한 `Textures/<filename>.meta` 신원으로
  낮춘다. 런타임은 비동기 typed 요청과 이전 owner 유지 정책을 쓰며, BC5 노멀과
  native sRGB/명시 Linear 입력은 별도 셰이더 비트로 해석한다
- **입력 지원 차이:** PSD/PIC/PNM은 설치된 WIC 코덱에 의존한다. HDR/JPEG brush의
  이전 stb 디코더 대비 픽셀 일치는 미검증이다. DirectXTex 왕복은 독립 디코더와의
  패리티를 증명하지 않는다

### T1a — DirectXTex cook 트랜스코딩 (구현 적용 · 부분 검증)

- 기존 `TextureCookProducer`의 pass-through를 GPU-ready 산출물 생성으로 전환한다
- 디코드 → 정책 적용/리사이즈 → 밉 생성 → 압축 → artifact 기록 순서를 구현한다
- 압축 실패·지원하지 않는 포맷·선택한 밉 정책과 다른 chain을 성공으로 게시하지 않는다.
  `PreserveAuthored`의 유효한 부분 mip chain은 그대로 보존한다. `GenerateFull`은
  기존 authored level의 바이트를 유지하며 누락된 tail을 추가해 전체 chain을 만든다.
  잘못된 metadata·피치·개수는 어느 정책에서도 거부한다
- artifact 버전 및 producer 식별자를 갱신하고 구버전/정책 불일치 산출물을 거부한다
- 모델 임베디드 텍스처도 같은 GPU-ready 표현과 검증 정책으로 연결한다
- 새 코덱 프로젝트나 추상화의 선행 설치 없이 기존 프로젝트 경계를 유지한다

### T2 — cooked 런타임 소비 연결 (구현 적용 · 부분 검증)

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

### T1b — 품질 및 추가 포맷 판단 (품질·성능 측정 대기)

BC5/BC7의 구조적 형식·백엔드 업로드·노멀 Z 복원 지원은 T0~T2 구현에 포함해 진행한다.
해당 형식의 품질 튜닝·시간·메모리 수용 측정은 미실행이며 별도 판단으로 남긴다.
BC6H는 실제 필요를 확인한 뒤 기존 DirectXTex 안에서 판단한다. DirectXTex 청산,
다른 압축기 도입, 자체 DDS 파서는 범위가 아니다.

현재 desktop Vulkan 타깃은 장치 선택 시 `textureCompressionBC`를 요구하고 생성 시
활성화한다. 지원하지 않는 장치는 명시적 진단으로 거부하며 runtime 압축 해제를 하지
않는다. DX12와 공유하는 BC artifact의 최상위 폭·높이는 4의 배수여야 한다.
이 계약을 향후 모바일/Web의 공통 RHI 요구로 확대하지 않는다.

### T3 — 모바일/Web (제품 타깃 부재로 중단)

실제 타깃·툴체인·GPU 형식·호스트 이미지 입력 요구가 확정되면 정책을 결정한다.
그 전에는 ASTC/Basis 프로젝트·의존성·미사용 등록 코드를 추가하지 않는다.

## 6. 소유와 의존성

- 이미지 처리 구현은 기존 코덱 경계와 authoring 소유 위치에 둔다
- Terrain 파일 쓰기는 `EditorAssetDatabase`가 소유하고 런타임 Terrain은 읽기만 한다
- 엔진 직접 `stb_image`/`stb_image_write` 소비는 제거하지만 FontAsset의 `stb_truetype` 용도로
  루트 vcpkg의 `stb` 패키지 의존은 유지한다.
  외부 라이브러리 내부 구현이나 전이 의존을 억지로 제거하지 않는다
- **Player의 실제 소스 편입·링크 경계에서 이미지 처리 의존 제거는 T2의 열린 완료
  조건이다.** decode-free 소비만으로 T2를 닫지 않는다. 기존 프로젝트에서 실제 소비를
  분리하며, 아직 존재하지 않는 `TextureCodec.*` 프로젝트나 코덱 0개를 달성 상태로 쓰지 않는다

## 7. 검증 계획 — PR #166 미실행 기록과 후속 실행 증거 분리

| 축 | 향후 실행 시 확인할 계약 |
|---|---|
| T0 | 기존 sidecar 기본값 · 설정 보존 · invalid enum/range 거부 · 정책 변경 시 cache key 변화 |
| T1 | 실제 PNG/HDR/DDS cook · 밉 정책별 치수/피치 · PreserveAuthored 부분 chain 보존 · 손상 artifact 거부 |
| T2 | 독립/모델 cooked 실제 소비 · 소스 없는 패키지 · 손상/version/hash 불일치 거부 |
| PNG 정본 왕복 | DirectXTex RGBA8 → 명시 RGBA PNG → RGBA8 바이트 일치, 알파 포함 |
| HDR 정본 왕복 | DirectXTex HDR → float DDS → 재로드 float 비트 일치 |
| Terrain | float bits/alpha 전수 왕복 · gray8 절삭 · 컬러/회색 brush 변환 일치 |
| 업로드 | 기존 `vk.texturecodec` 및 DX12 픽셀 검증의 실제 가용성과 결과 확인 |
| 의존성 | 엔진 직접 stb_image/stb_image_write include/심볼 0 · ImGui 내부 헤더 및 FontAsset stb_truetype 유지 |

기존 `assets.decodeab` / `assets.decodeabhdr` 이름을 호환상 남긴 경우에도 결과와 설명은
**DirectXTex 정본 왕복**으로 바꾸고, stb A/B 또는 독립 디코더 검증이라고 부르지 않는다.
동일한 디코더의 왕복은 외부 디코더와의 정확도 대조가 아니며 품질 판정도 아니다.
PR #166 게시 당시에는 새 검사 스크립트·프로브와 변이 검증을 실행하지 않았다.
2026-10-09 후속 실행 결과는 §10과 `docs/analysis/TexturePipelineValidation20261009.md`에서 구분한다.

## 8. 완료 조건과 미검증 항목

T0~T2의 완료는 설정 저작부터 실제 cooked 소비까지 닫힌 경로, Terrain 바이트 보존,
구버전·손상·설정 불일치 거부, Player의 실제 소스/링크 코덱 경계 제거, 그리고 허가된
환경에서의 실행 검증으로 판단한다. decode-free 소비 단계만으로 전체 T2 완료는 아니다.

2026-09-04에 제안했던 런타임 <1 ms/장, VRAM <120 MB, cook <50 ms/장,
저장→화면 +50 ms는 **역사적 목표**다. 현재 플랫폼·코퍼스에서 다시 측정하기 전에는
달성 여부도 압축기 교체 시 개선 예측도 쓰지 않는다.

PR #166 당시에는 빌드/실행을 검증하지 않았다. 후속 실행에서 확인한 빌드·정본 왕복·
texcook/Terrain 계약·모델 export·제한된 GPU 소비는 §10에 기록한다. 전체 수용은 재cook/
재로드 실패 복구·참조 수명·실제 Shipping 경계·새 포맷 GPU 소비·소스 없는 Player 경로와
현재 품질/성능 측정이 닫힐 때까지 진행 중이다.

## 9. 하지 않는 것

- stb 이관·bc7enc/ISPC 도입·자체 DDS 파서
- speculative 코덱 프로젝트, 모바일/Web 선행 구현
- 텍스처 스트리밍 및 범위 밖 렌더러 재설계
- 과거 수치를 현재 측정이나 새 검사의 통과로 재사용
- PR #166에서는 빌드·테스트·컴파일러·셰이더·바이너리·벤치마크를 실행하지 않았음

## 10. 2026-10-09 병합 반영과 상태 판정

근거: [PR #166](https://github.com/29thnight/CreatorEngine/pull/166), master 병합 커밋
`117b729a60acf9663dd6f41fc9b71f4ce6f60e00` (2026-10-09 20:53 KST).
PR 설명의 Draft/master 미병합 문구는 게시 당시 기록이며 현재 GitHub 상태는 MERGED다.
후속 사용자 지시에 따라 해당 소스와 필요한 선행 의존성을 로컬 작업 트리에 적용했다.
기존 RG8/GCCE 수정은 보존했으며 HEAD/index 변경과 commit/push는 하지 않았다.

| 슬라이스 | 대시보드 상태 | 남은 조건 |
|---|---|---|
| T0 / T1a / T2 | 진행 중 — 구현 적용 및 부분 실행 검증 | 아래 ImageComponent 종단 통과 범위를 제외한 SpriteRenderer/모델 등 소비자, Shipping GPU package, 새 포맷의 소스 없는 DX12/Vulkan 실제 소비, Inspector 조작 |
| T1b | 대기 | 현재 코퍼스의 BC5/BC7 품질·시간·메모리 측정; BC6H 실제 필요 판단 |
| T3 | 중단 | 실제 모바일/Web 타깃·툴체인·GPU 형식·호스트 이미지 입력 요구 확정 시 재개 |

이 시점에는 T0~T2의 수용 공수를 `earnedDays: 0`으로 유지했다. 후속 §20에서
통과한 한정 수용 단위를 분리해 계상한다. 기존 3/8/5일 및 T1b 4일 추정은
유지하되 검증 잔여 공수로 재측정한 값은 아니다. T3의 과거 10일 추정은 중단 이력으로
보존하고 활성 규모에서 제외한다. DirectXTex 청산·stb 이관·bc7enc·자체 DDS 파서
방향은 철회했다. 자동 컴포넌트 live rebind와 역할별 다중 artifact 참조는 별도 잔여
범위이며 PR #166 구현 완료로 표시하지 않는다. 문서 일관성 검사와 엔진 수용 검증을 구분한다.

후속 실행 증거: Debug/Release texcook 각각 기존 138/138 및 PHASE 12 53/53 통과,
Debug/Release 실제 프로젝트 PNG 각 102/102 및 HDR 각 19/19 정본 왕복, DX12/Vulkan 기존 네 형식
업로드 다이제스트 일치, PNG/JPEG 모델 CECT2 export 및 generation/신원 보존·구버전/
모호성 거부, BC5 normal 포함 material codegen 22 cases/15244 checks/3960 GPU checks,
BuildTool 142 checks 및 texture import 정적 5 tests 통과. Debug/Release encrypted cooked
Scene DX12 probe 각 24 frames/57259 checks/validation=0, 반복 cook 동일 및 실패 6종
출력 보존 통과. Debug/Release 실제 source-free Player fixture는 각각 display=2659/2833,
promotions=120/textParserCalls=0/패키지 hash 보존 통과. Release-Shipping Player 실제 build/link와
loader, codec imports/RenderEngine decoder 정의/staged codec files 각 0 확인.
Shipping GPU package·새 BC5/BC7 전체 GPU·현재 전체 자산
코퍼스 수용을 이 결과로 대신하지 않는다. 상세 로그·빌드 결과·발견한 수정·남은 조건은
[실행 검증 기록](../analysis/TexturePipelineValidation20261009.md)에 유지한다.

## 11. 2026-10-10 Editor 임포트 수명 후속 검증

실제 `EditorAssetDatabase`와 `DataSystem`을 연결한 격리 검사기를 추가했다.
설정 저장과 비동기 cook, 게시 경계, 실패 시 정상 세대 보존, 후속 요청 우선 적용,
캐시에서 제거한 이전 이미지의 정확한 산출물 재로딩, 이름 변경 중 GUID/sidecar 및
기존 참조 보존을 검사한다. ImGui 표시와 실제 씬 reload는 실행하지 않는다.
Debug/Release 각각 짧은·긴 프로젝트 경로를 5회씩 실행해 총 20회 × 36개 단정이
통과했다. 입력 소스 9개 hash의 drift는 0이다. 실제 Editor host도 두 구성 모두 재빌드했다.

긴 프로젝트 경로에서 generation 폴더와 산출물 경로가 같은 자산 GUID를 중복해
기록하던 문제를 재현했다. 고유 generation 폴더와 GUID 산출물 경로를 유지하면서
중복 폴더 단계를 제거했다. 기존 세대의 저장된 원본 경로는 그대로 사용할 수 있다.
임의 길이의 Windows 경로 지원을 달성한 것으로 확대하지 않는다.
추가로 cook 도중 rename을 막던 Windows 파일 공유 위반을 수정했다. producer의
최초 읽기와 cook 뒤 독립 디스크 재읽기 모두 rename/save를 허용하며 크기·hash·
stamp/revision 검사를 유지한다. 기존 정상 세대 보존 계약은 변경하지 않는다.

실행 구성·검사 수·원본 hash와 잔여 범위는
[Editor 임포트 수명 검증 기록](../analysis/TextureImportLifecycleValidation20261010.md)에 기록한다.
T0/T1a/T2의 전체 수용 상태와 `earnedDays: 0`, T1b 대기 및 T3 중단은 유지한다.

## 12. 2026-10-10 실제 Editor 씬 재로드 후속 검증

현재 Debug/Release 실제 Editor의 `scene.open_async` 및 일반 프레임 경로에서
각각 Ready 2건과 Failed 3건을 확인했다. 실패는 씬 파일 누락, 문서 손상,
필수 texture 누락이다. 각 실패 뒤 미저장 엔티티를 포함한 씬 저장 결과와
entity handle/sceneId 보존을 확인했다. 유효한 저장 씬 재시도는 새 sceneId로
교체되며 저장되지 않은 엔티티는 남지 않는다. 검증 대상 소스 5개 drift는 0이다.

작은 4×4 PNG 의존성을 사용한 실제 명령 경로 검사이며 Inspector 버튼/확인창/
취소/파일 선택 조작, 직전 cooked generation 게시부터 씬 재로드까지 한 프로세스의
종단 검증, GPU 화면 픽셀·전체 포맷·모델 수용을 대신하지 않는다.
상세 근거는 [씬 재로드 검증 기록](../analysis/TextureSceneReloadValidation20261010.md)에 유지한다.
전체 phase 상태와 수용 공수는 변경하지 않는다.

## 13. 2026-10-10 ImageComponent 재임포트→씬 재로드 종단 수용

실제 Debug/Release Editor의 한 프로세스에서 typed 설정 저장·cook·게시부터
ImageComponent 저장/역직렬화·씬 프레임 교체까지 연결했다. 4×4→2×2 재임포트 뒤
열린 컴포넌트는 4×4 owner를 유지하고, 저장 씬 재로드 뒤에는 새 2×2 owner를
사용한다. cook 실패와 없는 씬 재로드 실패는 현재 씬/owner를 보존하며,
원본 복구 후 4×4 재게시도 열린 2×2 컴포넌트를 자동 재바인딩하지 않는다.

cooked description의 빈 이름/경로를 ImageComponent가 빈 texturePaths로 저장하던
제품 결함을 실제로 재현하고 수정했다. 기존 경로가 없는 cooked 자산은 GUID로
등록된 참조 경로를 찾아 저장하며, 불가능하면 빈 참조를 기록하지 않는다.
loose/임시 생성 텍스처의 기존 처리와 immutable artifact 소유는 유지한다.

이 수용은 작은 ImageComponent fixture의 CPU description/image view 및 참조 identity다.
Inspector 직접 입력·GPU 픽셀·전체 포맷·모델·품질/성능 수용을 대신하지 않는다.
정적 검색에서 같은 이름 fallback을 사용하는 SpriteRenderer를 확인했으며
해당 cooked 저작/저장/재로드 수용은 T2의 다음 소비자 검사로 유지한다.
[종단 실행 기록](../analysis/TextureReimportSceneValidation20261010.md)에 상세 근거를 둔다.
T0/T1a/T2 진행 중 및 earnedDays 0, T1b 대기, T3 중단은 유지한다.

## 14. 2026-10-10 SpriteRenderer 종단 소비 후속 수용

SpriteRenderer도 cooked description의 빈 이름/경로를 빈 m_SpritePath로 저장하는
결함을 실제 Debug Editor에서 재현하고 수정했다. 기존 경로가 없는 cooked 자산은
GUID로 등록 경로를 찾아 저장하며, 찾지 못하면 기존 owner 교체 전에 거부한다.
기존 loose/임시 생성/null 처리와 immutable artifact 소유는 유지한다.

Debug/Release 실제 Editor의 같은 프로세스에서 ImageComponent와 SpriteRenderer의
4→2px 재임포트·게시, 기존 owner 유지, 저장 씬 재로드 후 각 role의 새 owner 연결,
씬 퇴역 후 이전 image view 보존, cook/reload 실패 보존 및 4px 원본 복구를 통과했다.
소스 9개 drift 0과 PNG 복구/runtime DLL hash 불변을 확인했다.
[SpriteRenderer 실행 기록](../analysis/TextureSpriteReimportValidation20261010.md)에
재현 fixture·빌드/실행 receipt 및 한계를 유지한다. §13의 다음 소비자 검사를 충족했다.

다음 활성 수용은 Inspector Save and Reimport/경고/취소/파일 선택 직접 입력이다.
GPU 픽셀·모든 포맷/모델·Shipping GPU·품질/성능/장시간 수용은 남긴다.
T0/T1a/T2 진행 중 및 earnedDays 0, T1b 대기, T3 중단은 유지한다.

## 15. 2026-10-10 Inspector 실제 버튼·취소·파일 선택 수용

동기화 후 Debug 실제 Editor의 별도 프로젝트에서 텍스처를 실제 클릭하고
최대 크기 + 버튼 0→2, Save and Reimport, Reload Saved Scene의 경고,
경고 Cancel과 native 파일 선택 취소, 저장 씬 선택·Open을 실제 입력으로 검증했다.
Save 전 metadata 0/후 2 및 GUID·원본 PNG 보존을 확인했다. 두 취소 경로는
미저장 엔티티를 유지하고 실제 저장 씬 재로드는 저장 엔티티만 복원했다.
[Inspector UI 실행 기록](../analysis/TextureInspectorUiValidation20261010.md)에
화면 근거·DLL hash·검증 한계를 유지한다. 이번 실행은 Debug의 작은 fixture 1회다.

Release UI 반복·키보드/붙여넣기 입력·잘못된 설정/cook 실패/준비 중 버튼 상태,
다중 창·배율 및 GPU 픽셀·모델/Shipping/전체 포맷·품질/성능 수용은 남긴다.
T0/T1a/T2 진행 중 및 earnedDays 0, T1b 대기, T3 중단은 유지한다.

## 16. 2026-10-10 리소스 편집 자동 처리 후속

사용자 승인 범위는 재임포트·현재 사용처 갱신 및 재질 그래프 Apply·Save와 프리팹 Apply다.
Inspector 임포트 설정과 렌더 프로필 편집은 패널 선택/표시와 독립적인 저장 대기열에
넘기며, 텍스처는 준비된 새 세대를 열린 씬의 기존 컴포넌트에 연결한다. 모델 원본/메타
편집은 현재 모델 세대를 MeshRenderer/Animator에 반영한다. 재질 그래프는 컴파일 성공한
편집을 자동 Apply·Save하고 프리팹 편집은 자동 저장 후 편집 씬을 제외한 인스턴스에 반영한다.
재질 shader source/include는 비동기 재컴파일 후 준비된 새 프로그램을 게시한다.

컴파일/cook 실패 또는 디스크 충돌은 이전 정상 리소스를 유지하며 오류를 보고한다.
종료 시 대기 설정을 저장하고 재질/셰이더 컴파일 소유를 정리한다.
§13~15의 명시적 버튼 실행 기록은 이전 동작에 대한 역사적 검증이며 현재 UX 수용과 구분한다.
[자동 처리 검증 기록](../analysis/AutomaticResourceEditingValidation20261010.md)에
최종 빌드와 실제 host 실행 수용을 기록한다. 전체 포맷/GPU 품질/성능 및 Shipping 수용은
기존 단계의 잔여 게이트로 유지하며 earnedDays는 올리지 않는다.

## 17. 2026-10-10 자동 처리 Inspector 실제 입력 수용

현재 Debug의 격리 프로젝트에서 숫자 키 최대 크기 2 입력→자동 저장·cook 및
씬 표시 반영, 선택 변경 후 자산별 설정 구분을 확인했다. 씬 재로드 없이 미저장
엔티티를 유지한다. Inspector를 닫은 상태의 외부 .meta 변경도 watcher로 반영됐다.
손상 PNG와 실제 Retry Import는 기존 씬을 보존하며, 원본 복구 후 자동 Ready로
복귀한다. Normal Map 없는 BC5는 진단하며 디스크 저장과 재시도를 거부했다.

[자동 Inspector UI 실행 기록](../analysis/TextureAutomaticInspectorUiValidation20261010.md)에
receipt·화면·실행 파일 hash 및 수용 경계를 기록한다. 선택/패널 검사에서 400ms 저장
대기 중 전환이나 종료 경합을 확인한 것으로 확대하지 않는다. Release UI·붙여넣기와
제어된 저장 대기 중 닫기/정상 종료는 후속 §18에서 수용했다. 디스크 충돌·다중 창/배율·
장시간/반복 경합·전체 소비자/GPU/품질 게이트는 유지한다.
전체 phase 상태와 earnedDays는 변경하지 않는다.

## 18. 2026-10-10 Release 자동 Inspector와 저장 대기 경합 수용

Release 실제 Ctrl+C/Ctrl+V가 실패하는 Win32 메시지의 스레드 간 modifier 전달 결함을
재현하고 수정했다. 최종 Release runtime에서 실제 값 2 복사·붙여넣기 후 Enter/저장/
씬 재로드 없이 자동 저장·cook·씬 표시 반영을 확인했다. 무효 BC5 저장/Retry 거부,
손상 PNG·Retry 시 정상 씬/미저장 엔티티 보존 및 원본 복구 후 자동 Ready도 통과했다.

실제 UI 편집으로 생성된 400ms 저장 대기를 관찰하여 패널 닫기와 정상 종료를 제어했다.
닫기 요청 age 0ms 및 종료 요청 age 7ms 모두 디스크는 이전 값이었다. 종료 Finalize에서
age 28ms 대기 설정을 저장했고, 재실행 Inspector 2와 씬 표시를 복원했다.
패널 닫기 적용 시각을 별도 계측하지 않았으며 각 1건의 제어된 요청은 장시간/반복
경합이나 native 마우스 종료의 시간 수용을 대신하지 않는다. Debug clipboard 수정
재검증·디스크 충돌·다중 창/배율·전체 소비자/GPU/품질 게이트는 남긴다.
이 중 유효 외부 설정의 제어된 저장 대기 충돌은 후속 §19에서 수용했다.

[Release UI 실행 기록](../analysis/TextureAutomaticInspectorReleaseValidation20261010.md)에
최종 DLL hash, 화면, 명령 receipt, 종료 로그 및 한계를 기록한다.
T0/T1a/T2 진행 중 및 earnedDays 0, T1b 대기, T3 중단은 유지한다.

## 19. 2026-10-10 저장 대기 중 외부 설정 충돌 수용

최종 Release 실제 Inspector 숫자 2 입력 후 저장 대기 age 8ms에 외부 설정 1을
주입했다. 일반 저장은 외부 bytes를 보존하고 대기 편집을 거부했다. 제어된 정상
종료도 age 8ms 주입 후 Finalize age 32ms flush에서 덮어쓰기를 거부해 값 1을 유지했다.
둘 다 원본 PNG/GUID·저장 씬 및 정상 종료/DX12 잔여 작업 0/0/0을 확인했다.

충돌 후 Inspector가 편집 값만 보여 저장 성공으로 오해할 수 있는 결함을 수정했다.
실제 오류 표시와 Reload Disk Settings 클릭으로 디스크를 쓰지 않고 값 1을 읽었고,
이후 새 숫자 2 입력의 정상 자동 저장을 확인했다.
[충돌 실행 기록](../analysis/TextureAutomaticInspectorConflictValidation20261010.md)에
빌드/runtime hash·화면·receipt·경계를 기록한다. 일반/종료 각 1건의 유효 외부 설정
주입 수용이며 최종 비교 이후 동시 쓰기·파일 잠금/삭제/손상·반복/장시간·다중 창/배율·
Debug 및 전체 소비자/GPU/품질 게이트는 남긴다.
T0/T1a/T2 진행 중 및 earnedDays 0, T1b 대기, T3 중단은 유지한다.

## 20. 2026-10-10 수용 단위 분리와 진행률 반영

사용자 요청에 따라 통과한 세부 수용 항목을 독립된 완료 작업으로 분리한다.
§10~19의 earnedDays 0 유지 문장은 각 실행 당시의 집계 이력이며 현재 집계는 이 절이
대체한다. T0/T1a/T2 전체 완료를 선언하는 변경은 아니다. 남은 검증은 별도 진행 행에서
earnedDays 0을 유지하며 T1b 대기와 T3 중단은 변경하지 않는다.

기존 계획의 T0 3일·T1a 8일·T2 5일·T1b 4일, 활성 총20일을 그대로 재배분한다.
아래 일수는 수용 범위의 계획 가중치다. 실제 투입 시간이나 남은 작업의 재측정 견적이
아니며 실행 증거가 없는 범위에는 완료 공수를 배정하지 않는다.
동일 실행 증거가 여러 경계를 검사해도 T0 저작/저장, T1a 생산 산출물, T2 소비/배포의
서로 다른 책임에만 배정한다. T1b 품질·성능을 바이트 왕복이나 형식 지원으로 계상하지 않는다.

| 대시보드 ID | 수용 완료 범위 | 가중치 | 실행 근거와 경계 |
|---|---|---:|---|
| 12-0-A | 설정·기본값·metadata 정책 | 1일 | [기초 계약](../analysis/TexturePipelineValidation20261009.md), [typed sidecar 보존](../analysis/TextureImportLifecycleValidation20261010.md) |
| 12-0-B | 자산 서비스 재임포트·실패 복구 | 0.75일 | [Debug/Release 짧은·긴 경로 20회×36단정](../analysis/TextureImportLifecycleValidation20261010.md); Tiny4 한정 |
| 12-0-C | 자동 Inspector·제어된 저장 경합 | 0.75일 | [Debug 입력](../analysis/TextureAutomaticInspectorUiValidation20261010.md), [Release 붙여넣기·닫기/종료](../analysis/TextureAutomaticInspectorReleaseValidation20261010.md), [유효 외부 설정 충돌·복구](../analysis/TextureAutomaticInspectorConflictValidation20261010.md); 반복/장시간 아님 |
| 12-1-A | GPU-ready cook·CECT2·Terrain 계약 | 4일 | [Debug/Release texcook 각138+53](../analysis/TexturePipelineValidation20261009.md); CPU 생산 계약 |
| 12-1-B | PNG/HDR 정본 왕복 | 2일 | [Release PNG102/HDR19](../analysis/TexturePipelineValidation20261009.md); 같은 DirectXTex 왕복, 독립 decoder·품질 수용 아님 |
| 12-1-C | 모델 PNG/JPEG cooked export | 1일 | [모델 export·신원/거부 계약](../analysis/TexturePipelineValidation20261009.md); 모든 모델 역할의 GPU sampling 아님 |
| 12-2-A | source-free Player·코덱 배포 경계 | 1.5일 | [Player/encrypted Scene·Release/Shipping 코덱0](../analysis/TexturePipelineValidation20261009.md); Shipping GPU package 전체 실행 아님 |
| 12-2-B | 기존 백엔드 업로드·BC5 재질 GPU | 1일 | [DX12/Vulkan 기존4형식·BC5 GPU3960 checks](../analysis/TexturePipelineValidation20261009.md); 새 포맷의 전체 소비 아님 |
| 12-2-C | ImageComponent/SpriteRenderer 세대 교체 | 1일 | [두 소비자 종단](../analysis/TextureSpriteReimportValidation20261010.md), [최종 Release 회귀282단정](../analysis/TextureAutomaticInspectorConflictValidation20261010.md); 다른 소비자/GPU pixel oracle 아님 |
| **합계** | **완료9행** | **13일** | **한정된 수용 범위만 done으로 계상** |

| 대시보드 ID | 미수용 범위 | 잔여 가중치 | 상태 |
|---|---|---:|---|
| 12-0 | 최종 비교 후 동시 쓰기·파일 잠금/삭제/손상·반복/장시간·다중 창/배율·Debug 후속 수정 재검증·미실행 오류 해제 UI 경계 | 0.5일 | 진행, earnedDays 0 |
| 12-1 | 전체 자산 코퍼스·입력 포맷/WIC 지원 및 이전 decoder 대비 보존 경계 | 1일 | 진행, earnedDays 0 |
| 12-2 | 전체 소비자/모델 sampling·새 포맷의 소스 없는 DX12/Vulkan GPU 소비·Shipping GPU package | 1.5일 | 진행, earnedDays 0 |
| 12-3 | BC5/BC7 품질·시간·메모리 측정과 BC6H 실제 필요 판단 | 4일 | 대기, earnedDays 0 |
| **합계** | **미수용4행** | **7일** | **추정 공수의 잔여 비중** |

T3의 과거10일은 중단1행으로 유지해 활성 집계에서 제외한다.
활성13행 = 완료9행 + 진행3행 + 대기1행이며 총20일, 완료13일, 진행 기성0일,
잔여7일이다. 대시보드의 기존 공수 가중 계산은 **13÷20 = 65%**를 표시한다.
핵심 T0~T2만 따로 보면 **13÷16 = 81.25%**다. 남은 행이 있으므로 PHASE 12는 열린 상태다.
