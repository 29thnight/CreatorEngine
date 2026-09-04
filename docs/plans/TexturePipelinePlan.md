# 텍스처 파이프라인 — 임포트 · cook · 런타임 소비

작성: 2026-09-04

상태: **계획 — 미착수**

대시보드: **PHASE 12**

대상: `Engine/RenderEngine`(Texture · 텍스처 캐시), `Engine/Experiment/Cooked`,
`Tools/AssetCooker`, 신설 `Engine/TexturePipeline` · `Engine/TextureCodec.*`

관련 문서: `TextureCodecBoundaryDesign.md`(선행 · 완료), `BuildPipelinePlan.md`(PHASE 12.5),
`MaterialPipelinePlan.md`, `SerializationPlan.md`, `ModelAssetBigBangCutoverPlan.md`,
`EngineLayerSeparationPlan.md`

---

## 1. 이 문서가 하는 일

텍스처가 **저작에서 GPU까지 가는 길**을 세운다. 지금 그 길에는 임포트 설정도, 밉도,
cook 트랜스코딩도 없고, 런타임이 매 로드마다 디코드와 압축을 한다.

목표는 상용 엔진의 표준 구도다 — **cook 시점에 굽고, 런타임은 구운 것을 읽는다.**

```
저작(PNG/HDR)  →  임포트 설정(.meta)  →  cook(디코드·밉·압축)  →  artifact(GPU-ready)
                                                                        ↓
                                          런타임: 파일 읽기 → 바로 업로드 (디코드 0)
```

★ 이 문서는 **라이브러리 교체 계획이 아니다.** 교체는 결과로 따라온다 —
런타임이 artifact를 읽게 되면 런타임에 이미지 라이브러리가 0개가 되고, 그때 남는
cook 도구의 라이브러리 선택은 성능 문제가 아니라 이식성 문제가 된다(§4).

---

## 2. 실측 — 지금 무엇이 있고 무엇이 없나

### 2.1 절반은 이미 서 있다

| | 모델 임베디드 텍스처 | 독립 텍스처 |
|---|---|---|
| cook 코드 | 있음 (`ModelAssetGeneration`) | 있음 (`TextureCookProducer`) |
| **저작분** | 있음 (`Library/ModelAssetGenerations/`) | **0건** |
| artifact 내용 | RGBA8 raw | **pass-through**(원본 바이트) |
| 런타임 소비 | 있음 (`ResolveModelGenerationTexture`) | **0건** — 원본 PNG 직독 |

`TextureCookProducer.h` 가 pass-through 인 이유를 스스로 적어 두었다:

> BC7 압축·밉 생성은 여기 없다. (…) 그러면 무엇을 얻는가. **압축이 아니라 주소
> 체계다** — GUID 주소 · 내용 해시 · manifest 등재. (…) `formatVersion` 이 1 이므로
> 트랜스코딩이 들어오는 날 2 가 되고 구버전 artifact 는 자동으로 거부된다.

**자리가 예약돼 있다.** 이 계획은 새 파이프라인을 만드는 것이 아니라 그 칸을 채운다.

### 2.2 없는 것 넷

1. **임포트 설정** — `.meta` 의 `importSettings` 가 `extension` · `timestamp` 둘뿐이다.
   색공간·압축·밉 정책을 자산마다 정할 수 없다. 지금은 코드가 정한다 —
   `DataSystem::FinalizeMaterialRuntime` 이 baseColorMap 에만 `compress=true` 를 넘긴다.
2. **밉** — `GenerateMipMaps` 호출 0건. 밉이 있는 자산은 소스가 이미 담고 들어온
   `blueNoise.dds`(서브리소스 8) 하나뿐이다.
3. **cook 트랜스코딩** — §2.1.
4. **런타임 소비 전환** — 런타임이 원본 PNG 를 읽고 디코드한다.

### 2.3 코퍼스 (2026-09-04)

★ **PNG 583 을 출처별로 갈라야 한다.** 초안은 섞어 세었다 — 그중 411 은 저작물이
아니라 generation 이 뽑아 둔 cook 산출물이다.

| PNG 출처 | 개수 | 성격 |
|---|---|---|
| `Dynamic_CPP/Assets` | **99** | **저작 자산.** 재질·UI·LUT 등 |
| `Dynamic_CPP/Library/ModelAssetGenerations` | 411 | 모델 임베디드 텍스처의 cook 산출물 |
| 에디터 아이콘 등 | 73 | |
| (합계) | 583 | **전부 8비트**. RGBA 345 · RGB 237 · 팔레트 1 |

| 그 밖 | 개수 | 비고 |
|---|---|---|
| HDR | 19 | 4096x2048 equirect. **장당 128MB**(RGBA32F) |
| DDS | 1 | `blueNoise.dds` — DXT5(BC3), 밉 8단 |
| TGA · JPEG · BMP · EXR | **0** | 로더는 있으나 태울 자산이 없다 |

디코더를 태우는 것은 저작 99 장만이 아니다 — generation 산출물도 같은 로더를 지나고
에디터 아이콘도 그렇다. 디코더 교체의 대조 범위는 **PNG 전수**다(§7).

### 2.4 성능 (Release · 99장 실측)

| 단계 | 총 | 장당 | 산출 |
|---|---|---|---|
| 파일 읽기 | 57.7 ms | **0.58 ms** | 24.8 MB |
| 디코드 | 348.7 ms | 3.52 ms | 92.9 MB |
| 밉 생성 | 843.6 ms | 8.52 ms | 123.8 MB |
| BC1 압축 | 2186.8 ms | **22.1 ms** | 15.5 MB |
| **cook 합계** | 3379 ms | **34.1 ms** | |
| **현재 런타임 로드** | 528.5 ms | **5.34 ms** | |

★ BC1 압축이 cook 의 **65%** 다. DirectXTex CPU 인코더가 느리다는 통설이 실측으로
확인됐고, 이것이 §4 의 압축기 선택을 좌우한다.

### 2.5 VRAM (계산 · Dynamic_CPP PNG 510장)

| | 크기 | 현재 대비 |
|---|---|---|
| **현재** (RGBA8 · 밉 없음) | 420.4 MB | 1.00x |
| RGBA8 · 밉 | 560.6 MB | 1.33x |
| **BC1 · 밉** | **70.1 MB** | **0.17x** |
| 알파면 BC3 · 아니면 BC1 · 밉 | 117.0 MB | 0.28x |

밉을 넣어도 압축이 압도한다. **3.6~6배 절감.**

---

## 3. 선행 — 이미 끝난 것

`TextureCodecBoundaryDesign.md`(축 A, 2026-09-04 완료)가 이 계획의 정지작업을 했다.

- `Texture::m_cpuPixels` 가 코덱 산출물을 불투명 타입으로 들고, 경계로는
  `TextureImageView`(비소유 뷰)만 낸다. **artifact 포맷이 이미 이 모양이다.**
- `RHIFormat` 에 BC1 · BC3 · BGRA8 어휘가 들어갔다. 블록 인식 크기 계산
  (`RHIFormatRowPitch` · `SlicePitch` · `RowCount`)도 함께.
- DirectXTex 를 컴파일하는 TU 가 113+ 에서 **4** 로 줄었다.
- 게이트: `vk.texturecodec`(두 백엔드 업로드 A/B 다이제스트) · 두 `gizmoicon` 의
  코덱 슬라이스.

즉 **엔진 자체 타입과 백엔드 중립 어휘가 서 있다.** 이 계획은 그 위에 얹는다.

---

## 4. 라이브러리 구성

### 4.1 원칙

1. **디코더·인코더는 외부를 쓴다.** PNG/JPEG 디코더를 직접 만들 이유가 없다.
2. **처리 파이프라인은 자체 구현한다.** 밉 정책 · 색공간 · 노멀 처리 · ORM swizzle ·
   플랫폼별 포맷 결정 · 압축 품질 · cooked 자산 관리는 엔진의 정책이다.
3. **백엔드는 분리한다.** 프로젝트 단위로 쪼개 링크 여부가 곧 포함 여부가 되게 한다(§6).

### 4.2 크로스플랫폼이 선택을 정한다

★ **"DirectXTex = Windows 전용" 은 부정확하다.** WIC 부분(PNG/JPEG)만 그렇고,
DDS·TGA·HDR 로더와 BC 압축기는 순수 계산이라 이식 가능하다.

그런데 막히는 그 한 곳이 하필 자산의 99%(PNG 583장)다. **크로스플랫폼이 로드맵이면
stb_image 도입은 확장 대비가 아니라 필수 조건이다.**

★ `stb_image` 는 **DDS 를 읽지 못한다**(JPEG·PNG·TGA·BMP·PSD·GIF·HDR·PIC·PNM).
자산 1개뿐이지만 미니 파서가 필요하다(헤더 128B + 블록 데이터, ~100줄).

★ **HDR 은 stb 로 갈지 않는다 (2026-09-04 실측).** `stbi_loadf` 와
`LoadFromHDRMemory` 가 **19장 전부 다른 값**을 냈다. 차이는 체계적이다 —
`0.057617`(WIC) vs `0.056641`(stb)로 정확히 mantissa 1 차이이고 비율이
59/58 = 1.01724 로 일정하며, 모든 채널이 같은 방향으로 어긋난다(알파만 일치).
RGBE 디코드의 반올림 규약이 다르다.

밝기 차이는 0.86% 라 눈에 띄지 않지만 **IBL 프리필터 결과가 바뀌고 픽셀 골든이
깨진다.** 그리고 DirectXTex 의 HDR 로더는 WIC 이 아니라 이식 가능하므로, 갈아서
얻을 것이 없다.

★ 어느 쪽이 Radiance 표준을 따르는지는 **아직 규명하지 않았다**(§10.7). 표준은
`(mantissa + 0.5) x 2^(e-136)` 인데 관측된 차이는 1 LSB 라 그것과도 다르다.
지금은 "값이 바뀌지 않는 쪽"을 고르는 것으로 충분하고, 규명은 HDR 압축(BC6H)을
다룰 때로 미룬다.

### 4.3 선택

| 역할 | 라이브러리 | 시점 | 근거 |
|---|---|---|---|
| PNG·JPG·BMP·TGA 디코드 | **stb_image** | T1a | 이미 vcpkg 의존. WIC 이 비-Windows 에서 PNG 를 막는다. **PNG 685장 A/B 바이트 완전 일치 확인** |
| **HDR 디코드** | **DirectXTex 유지** | — | WIC 이 아니라 이식 가능. stb 로 갈면 값이 바뀐다(§4.2) |
| 밉 · 리사이즈 | **stb_image_resize2** | T1a | 이미 설치됨. 감마 인식 리샘플 |
| DDS 디코드 | **미니 파서(자체)** | T1a | 자산 1개, stb 미지원 |
| BC1/3/4/5/7 압축 | **bc7enc_rdo** | T1a | 순수 C++. DirectXTex 압축기(22ms/장) 대체. RDO 로 패키지 압축률까지 |
| BC6H (HDR 압축) | 미정 | T1b | 지금 HDR 을 압축하지 않는다 |
| ASTC | astcenc | T3 | **모바일 실착수 시** |
| KTX2 / Basis | KTX-Software / basis_universal | T3 | **Web 실착수 시** |
| EXR | tinyexr | 보류 | **EXR 자산 0개** |

`bc7enc_rdo` 는 vcpkg 포트 확인 후 없으면 `ThirdParty/` 에 벤더링한다
(`fastgltf` · `ufbx` · `mikktspace` 와 같은 자리).

★ **이 구성이면 DirectXTex 의 Windows 종속(WIC)이 0이 된다.** 자산의 99%를 차지하는
PNG 가 stb 로 넘어가므로 크로스플랫폼 목적은 달성된다.

DirectXTex 자체는 **HDR·DDS 로더로 남는다.** 둘 다 WIC 을 쓰지 않아 이식 가능하고,
갈아서 얻을 것이 없다(HDR 은 값이 바뀌고 DDS 는 자산 1개다). "DirectXTex 제거"는
목표가 아니었다 — 침투를 막는 것이 목표였고 그것은 축 A 가 이미 끝냈다.

### 4.4 왜 ISPC 가 아닌가

Intel ISPC Texture Compressor 는 BC 압축에 SIMD 를 가장 잘 쓰고 UE 가 쓴다
(`TextureFormatIntelISPCTexComp`). BC 블록이 4x4 로 완전히 독립적이고 분기 발산이
없어 SPMD 모델에 맞기 때문이다.

**대가는 빌드에 언어가 하나 늘어나는 것이다** — `.ispc` 를 `ispc.exe` 로 먼저
컴파일해 링크해야 하고, ISA 타깃마다 `.obj` 와 디스패치 스텁이 생기며, CI 에 ISPC 가
없으면 빌드가 깨진다.

우리 규모(510장 · BC1 17초)에서는 그 값을 못 한다. 압축이 빌드 시간을 지배하는
수만 장 규모가 되면 재검토한다 — **T1b 의 열린 항목**이다.

---

## 5. 슬라이스

### T0 — 임포트 설정 어휘

`.meta` 의 `importSettings` 에 텍스처 설정을 넣는다.

- 색공간(sRGB / Linear) · 압축(None / BC1 / BC3 / BC7 / Auto) · 밉 생성 여부 ·
  최대 크기 · wrap · filter · 노멀맵 여부
- 지금 하드코딩된 `compress=true`(baseColorMap)를 이 설정으로 대체한다
- 에디터 인스펙터에서 편집 · 변경 시 재cook

★ **이것이 먼저인 이유:** 없으면 T1 의 모든 정책이 코드에 박힌다. 지금
`FinalizeMaterialRuntime` 이 슬롯 이름으로 압축을 정하는 것이 정확히 그 증상이다.

★ 기본값은 **기존 동작을 재현**해야 한다 — baseColorMap 만 BC1_SRGB, 나머지 무압축.
그래야 T0 이 그림을 바꾸지 않는다(게이트가 그것을 단정한다).

### T1a — cook 트랜스코딩

`TextureCookProducer` 가 pass-through 를 그만두고 굽는다.

- 신설 프로젝트 셋: `Engine/TexturePipeline`(정책) ·
  `Engine/TextureCodec.Stb`(디코드·리사이즈) · `Engine/TextureCodec.Bc7Enc`(BC 인코더)
- 처리 순서: 디코드 → 색공간 확정 → 리사이즈 → 밉 생성 → 채널 처리 → 압축
- artifact 포맷: 헤더(포맷 · 치수 · 밉 · 배열) + 서브리소스 바이트.
  **`TextureImage` 의 직렬화형**이다
- `formatVersion` 1 → 2 (예약된 자리)
- `ITextureDecoder` · `ITextureEncoder` 인터페이스를 여기서 세운다. 구현체는 실제로
  도는 것만(Stb · Bc7Enc)

★ **압축을 한 함수로 가둔다.** `EncodeBlocks(view, format, quality)` — T1b 의 압축기
교체가 그 함수 교체로 끝나야 한다.

### T2 — 런타임 소비 전환

- `DataSystem` 이 원본 PNG 대신 artifact 를 읽는다. 로드 = 파일 읽기 + 헤더 파싱
- **에디터는 원본 직독 경로를 남긴다** — 아직 안 구워진 자산도 보여야 한다.
  상용 엔진도 같다
- 게이트: 런타임 → `TextureCodec.*` 의존 간선 **0** (CI 래칫)

★ [[dead-produce-only-pipeline]] 의 둘째 사례가 정확히 이 자리였다 — "cooked
파이프라인은 지울 게 아니라 이을 것이었고, 잇는 순간 첫 소비에서 결함이 나왔다".
**잇는 슬라이스는 결함을 기대하고 들어간다.**

### T1b — DirectXTex 잔여 청산 · 압축기 재검토

- DDS 미니 파서로 마지막 DirectXTex 소비를 걷는다
- BC7 · BC5 도입 판단(품질) · BC6H 판단(HDR 128MB 압축)
- 압축기가 빌드 시간을 지배하기 시작하면 ISPC 재검토(§4.4)

### T3 — 모바일 · Web

- `TextureCodec.Astc` · `TextureCodec.Basis` 프로젝트 신설
- 플랫폼별 압축 정책 표를 `CompressionPolicy` 에 넣는다

| 타깃 | Albedo | Normal | ORM | Emissive | HDR |
|---|---|---|---|---|---|
| Windows · Linux | BC7 sRGB | BC5 Linear | BC1/BC7 Linear | BC7 | BC6H |
| Android · iOS | ASTC | ASTC | ASTC | ASTC | ASTC HDR(지원 시) |
| Web | KTX2 → Basis 트랜스코딩 | | | | |

★ **착수 조건이 붙는다** — 해당 플랫폼 타깃이 실제로 생겼을 때. 지금 vcxproj 는
Win32/x64 뿐이고, 빈 프로젝트를 미리 만들면 그것이 곧 죽은 코드다.

---

## 6. 조합 — 경량 · 선택집중

### 6.1 매크로가 아니라 프로젝트다

`EngineLayerSeparationPlan` §8 이 못박은 원칙을 그대로 따른다:

> `#ifdef EDITOR` 나 `BUILD_FLAG` 로 Core 안의 Editor 코드를 가리는 것은 완료가
> 아니다. **해당 코드가 Core 프로젝트에서 컴파일되지 않아야 한다.**
> **프로젝트 참조와 소스 편입이 실제 경계의 증거다.**

실제로 이 엔진에 기능 토글 매크로가 0건이고, 렌더 백엔드조차 런타임 선택이다.

### 6.2 링크 조합

| 호스트 | 링크 |
|---|---|
| **게임 런타임** | **없음** — artifact 만 읽는다 |
| 에디터 | `TexturePipeline` + `Stb` + `Bc7Enc` |
| AssetCooker | 타깃별. Windows 면 `Stb`+`Bc7Enc`, 모바일이면 `+Astc` |

### 6.3 등록은 호스트가 명시적으로

```cpp
// 에디터 · 쿠커 부팅
RegisterStbCodecs(registry);        // TextureCodec.Stb 를 링크했을 때만 존재
RegisterBc7EncEncoders(registry);
```

★ 자동 등록(static initializer)을 쓰지 않는다. **링크하지 않으면 함수가 없어 링크
에러가 난다** — 누락이 조용하지 않다. 자동 등록이면 "빠졌는데 아무도 모르는" 상태가
되고, 이 저장소에 그 전례가 여럿이다.

`EngineLayerSeparationPlan` 원칙 3 과도 맞는다 — "동작 차이는 Core 내부의 모드 분기가
아니라 Host 가 전달하는 설정·adapter 로 만든다".

### 6.4 경량화는 두 단계로 온다

1. **T2 가 끝나면 게임 런타임은 자동으로 코덱 0개다.** 조합 없이 그렇다
2. **툴 빌드는 타깃별로 조합한다.** 참조하지 않으면 소스가 컴파일되지 않는다

★ `TextureImage` · `TextureImageView` 는 코덱 쪽으로 옮기지 않는다. 런타임 캐시
둘이 그 뷰를 소비하므로 `RenderEngine` 에 남는다.

---

## 7. 게이트

이 저장소의 규율대로, **새 검사가 초록이면 변이로 이빨을 증명한다.**

| 슬라이스 | 게이트 | 변이 증명 |
|---|---|---|
| T0 | 기본 설정이 기존 동작을 재현 — baseColorMap 만 BC1_SRGB, 나머지 무압축 | 기본값을 뒤집으면 `vk.texturecodec` 포맷 단정이 빨개지는가 |
| T1a | **PNG 전수 A/B 바이트 대조**(`assets.decodeab`) — DirectXTex 와 stb_image 로 각각 디코드해 픽셀 일치 | **2026-09-04 통과: 685장 전수 일치**(BGRA 419장은 정규화 후, 266장은 원본끼리 — 양쪽 다 일치) |
| T1a | HDR A/B 대조(`assets.decodeabhdr`) — **회귀 감시용**. HDR 은 DirectXTex 를 유지하므로 이 검사는 "갈지 않았음"을 지킨다 | 19장 전부 불일치가 현재 값이다. 0이 되면 누군가 HDR 디코더를 바꾼 것이다 |
| T1a | 밉 체인 자기 대조 — 밉 n 의 치수·바이트가 `RHIFormatSlicePitch` 와 일치 | 밉 하나를 건너뛰면 빨개지는가 |
| T2 | **런타임 → `TextureCodec.*` 간선 0** (CI 래칫) | 런타임에서 디코더를 부르면 빨개지는가 |
| T2 | artifact 로드 픽셀 = 원본 직독 픽셀 (같은 자산, 두 경로) | artifact 헤더의 mipLevels 를 1 늘리면 빨개지는가 |
| 전 구간 | `assets.texturebench` 로 전후 수치 대조 | — |

★ 기존 `vk.texturecodec`(두 백엔드 업로드 A/B 다이제스트)은 그대로 유효하다.
포맷이 BC7·ASTC 로 늘어도 같은 단정이 선다.

### 7.1 못 잡는 것 (문서화까지가 증명)

- **압축 품질**. 다이제스트는 "같은 바이트인가"만 답한다. BC1 이 BC7 보다 나쁜지는
  사람이 화면을 봐야 한다. 품질 판정 게이트는 이 계획에 없다
- **Vulkan 쪽 픽셀 내용**. 축 A 에서 확인된 기존 구멍이 그대로다 — 뷰가 엉뚱한 데를
  가리키면 두 백엔드가 사이좋게 같은 쓰레기를 올린다. 그 축은 DX12 픽셀 골든이
  혼자 지킨다

---

## 8. 완료 조건

§2.4 · §2.5 실측을 기준선으로 삼는다.

| 축 | 목표 | 현재 |
|---|---|---|
| 런타임 텍스처 로드 | **< 1 ms/장** | 5.34 ms |
| 텍스처 VRAM (Dynamic_CPP) | **< 120 MB** | 420 MB |
| cook | **< 50 ms/장** | 34 ms (압축기 교체 시 < 15 ms 기대) |
| 에디터 저장 → 화면 반영 | **+50 ms 이내** | +34 ms 예상(압축 생략 시 +12 ms) |
| 게임 런타임의 이미지 라이브러리 | **0개** | DirectXTex |
| 런타임 → 코덱 의존 간선 | **0** | — |

★ 마지막 둘이 §6 의 조합 전략을 숫자로 만든 것이다.

---

## 9. 하지 않는 것

- **압축 품질 튜닝.** BC1 로 VRAM 이 6배 줄고, BC7 이 얼마나 나은지는 실제 화면을
  보고 정한다. T1b 의 판단이지 T1a 의 범위가 아니다
- **텍스처 스트리밍.** 밉 단위 상주 관리는 이 계획 밖이다. 밉이 생기는 것이 그
  전제이므로 T1a 뒤에 열린다
- **빈 백엔드 프로젝트 선행 생성.** §5 T3
- **`Texture` 로더 넷의 통합.** 경계가 서면 자연히 접히지만, 같은 슬라이스에서 하면
  A/B 대조의 판별력이 흐려진다

---

## 10. 착수 전에 확인할 것 — 미검증 전제

정직하게 적는다.

1. **`bc7enc_rdo` 의 vcpkg 포트 유무.** 없으면 벤더링이고, 그러면 T1a 에 빌드 배관이
   붙는다. 확인하지 않았다
2. ~~**stb_image 의 HDR 채널 수.**~~ **확인 완료(2026-09-04).** 파일이 3채널이고
   `stbi_loadf` 는 4로 강제 요청하면 알파를 1.0 으로 채운다. 그런데 **값 자체가
   달랐다** — §4.2. HDR 은 DirectXTex 를 유지하기로 했으므로 이 항목은 닫혔다
3. ~~**583장 A/B 대조의 실제 결과.**~~ **확인 완료(2026-09-04).** PNG **685장 전수
   완전 일치** — 픽셀 불일치 0 · 치수 불일치 0 · 실패 0. WIC 이 BGRA 로 낸 419장은
   `RGBA8` 정규화 후, 나머지 266장은 원본끼리 비교했고 양쪽 다 일치했다. 즉 정규화가
   결과를 만들어낸 것이 아니다. **T1a 의 핵심 전제가 섰다**
4. **cook 산출물의 디스크 크기.** VRAM 은 계산했으나 artifact 파일 총량은 재지 않았다.
   패키징 크기에 직접 영향한다
5. **`.meta` 스키마 변경의 파급.** `importSettings` 확장이 기존 자산 583개의 재임포트를
   요구하는지, 기본값으로 흡수되는지 확인하지 않았다
6. **에디터 재cook 반응성.** efsw 워처 → 재cook → 재로드 사슬의 실제 지연을 재지
   않았다. §8 의 "+50ms" 는 cook 시간에서 유도한 추정이다
7. **RGBE 반올림 규약의 정본.** DirectXTex 와 stb 가 mantissa 1 만큼 다른데 어느
   쪽이 Radiance 표준(`(v + 0.5) x 2^(e-136)`)을 따르는지 규명하지 않았다. 지금은
   "값이 바뀌지 않는 쪽"을 골랐을 뿐이다. BC6H(HDR 압축)를 다룰 때 정해야 한다
