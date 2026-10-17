# 텍스처 CPU 이미지 경계 설계

작성: 2026-09-04

상태: **설계 — 미착수**

대상: CreatorEngine RenderEngine (Texture · RHI · DX12 · Vulkan), Editor 자산 경로

관련 문서: `RhiBoundaryPlan.md`, `AssetResidencyPlan.md`, `MaterialPipelinePlan.md`,
`ModelAssetBigBangCutoverPlan.md`

---

## 1. 이 문서가 하는 일

DirectXTex 의존을 **줄이는 것이 아니라**, 그 타입이 RHI 경계를 넘어 두 백엔드에
직접 노출된 것을 막는다. 라이브러리 교체는 이 문서의 범위가 아니다 — 교체가
**가능한 상태를 만드는 것**까지다.

착수 동기는 취향이 아니라 실측된 비대칭이다(§4). 지금 구조에서는 DX12 와 Vulkan 이
같은 자산에 대해 다른 결과를 내며, 그 원인이 정확히 이 누출 지점에 있다.

---

## 2. 실측 — 누출 지점 전수

### 2.1 DirectXTex 접점 파일 (2026-09-04 실측)

`<DirectXTex.h>` 를 직접 여는 파일 10개, 심볼만 쓰고 전이 include 에 기대는 파일 4개
= **14개**.

| 파일 | 무엇을 쓰는가 | B3 이후 |
|---|---|---|
| `Engine/RenderEngine/Texture.cpp` | `LoadFrom{DDS,TGA,HDR,WIC}{File,Memory}` · `Compress` · `IsCompressed` · `HasAlpha` · `BitsPerPixel` · `IsAlphaAllOpaque` | **남는다** (코덱 구현) |
| `Engine/RenderEngine/Assets/ModelAssetGeneration.cpp` | `Decompress` · `Convert` · `IsSRGB` | **남는다** (코덱 구현) |
| `Editor/EngineEntry/EditorAssetDatabase.cpp` | `Initialize2D` · `SaveToWICFile` · `GetWICCodec` | **남는다** (코덱 구현) |
| `Engine/RenderEngine/Texture.h` | `shared_ptr<ScratchImage>` 멤버 · `GetCpuPixels()` 반환형 | 사라진다 (B2) |
| `Engine/Utility_Framework/Core.Definition.h` | `#include <DirectXTex.h>` 만 | 사라진다 (B2) |
| `Engine/Utility_Framework/DirectXHelper.h` | 없음 (Core.Definition.h 전이) | 사라진다 (B2) |
| `Engine/RenderEngine/RHI/DX12/DX12TextureCache.{h,cpp}` | `ScratchImage` · `TexMetadata` · `GetImage` · `rowPitch` | 사라진다 (B3) |
| `Engine/RenderEngine/RHI/Vulkan/VulkanRenderServices.cpp` | `ScratchImage` · `TexMetadata` · `FormatOf(DXGI_FORMAT)` | 사라진다 (B3) |
| `Engine/RenderEngine/DataSystem.cpp` | 전이 소비 | 사라진다 (B3) |
| `Editor/RenderTests/**` 4개 | 검사 픽스처 | B3 과 함께 이관 |

### 2.2 헤더 파급

`Core.Definition.h:35` 의 `#include <DirectXTex.h>` 가 `Core.Minimal.h` 를 타고
**직접 include 106개 파일**에 들어간다(전이 포함하면 그 이상). 실제로 API 를 쓰는
파일은 14개다.

Core.Definition.h 의 다른 직접 소비자 6개(`SpriteSheet.h` · `ActionMap.cpp` ·
`InputAction.h` · `KeyState.h` · `DumpHandler.h` · `ImGuiContext.h`)는 전부
DirectXTex 심볼 사용 **0건**이다. 즉 이 include 는 `Texture.h` 하나를 위해 106개
TU 에 실려 있다.

`ReflectionRedesignPlan` CT3 이 `Reflection.hpp` 를 걷어낸 것과 같은 모양이다 —
그쪽은 280개 TU 에 yaml-cpp 를 실어 나르고 있었고, 헤더 터치 재빌드 4m29s 의
몸통이었다.

### 2.3 압축은 BC1 하나뿐

`DirectX::Compress` 호출 4곳이 전부 `BC1_UNORM` / `BC1_UNORM_SRGB` 다.
BC3 · BC5 · BC6H · BC7 은 0건.

- `Texture.cpp:151` (`LoadFormPath`)
- `Texture.cpp:270` (`LoadSharedFromPath`)
- `Texture.cpp:349` (`LoadSharedFromMemory`)
- `Texture.cpp:445` (`LoadManagedFromPath`)

네 경로가 매직 분기 · 압축 정책 · `m_cpuPixels` 게시를 각각 복제하고 있다. 경계를
세우면 이 넷이 하나로 접힌다 — 이 문서의 목표는 아니고 부수 효과다.

---

## 3. 이미 서 있는 경계와 그 구멍

경계를 **새로 만들 것이 아니다.** 이미 있다:

```cpp
// Engine/RenderEngine/RHI/IRenderTextureCache.h
class IRenderTextureCache
{
    virtual RHITextureEntry GetOrUpload(Texture* texture, std::string& outError) = 0;
    ...
};
```

`DX12TextureCache` 와 `VulkanTextureCache` 가 둘 다 이것을 구현하고, 패스는
`context.textureCache->GetOrUpload(...)` 만 부른다(GBuffer · Forward · Decal ·
Sprite). 반환 타입 `RHITextureEntry` 도 이미 중립이다 — `RHIFormat` · `uint32_t` 뿐.

**구멍은 하나다:**

```cpp
// Engine/RenderEngine/Texture.h:121
const DirectX::ScratchImage* GetCpuPixels() const { return m_cpuPixels.get(); }
```

두 백엔드가 인터페이스를 통과한 **직후** 이 접근자로 DirectXTex 타입에 손을 뻗는다.

```
DX12TextureCache.cpp:483    const DirectX::ScratchImage* pixels = texture->GetCpuPixels();
VulkanRenderServices.cpp:385 const DirectX::ScratchImage* pixels = texture->GetCpuPixels();
```

즉 이것은 "추상화 계층을 하나 더 얹는" 작업이 아니라 **이미 있는 인터페이스의
구멍을 막는** 작업이다.

### 3.1 같은 근거가 이미 한 번 쓰였다

`RHIResourceTypes.h` 머리에 이렇게 적혀 있다:

> 여기 있는 것은 전부 `RenderFrameServices.h`(RHI/DX12/) 안에 있었고, 전부
> **이미 중립**이었다 — 핸들·열거·POD 뿐이고 D3D12 타입이 하나도 없다.
> 그런데 그 헤더가 `d3d12.h` 를 물기 때문에, 내용이 중립인 것과 무관하게
> **Vulkan 이 이 타입들을 쓸 수가 없었다.**

CPU 이미지도 정확히 같다. 내용(픽셀 · 행 간격 · 밉 · 배열)은 백엔드와 무관한데,
타입이 `DirectX::ScratchImage` 라서 DXGI · WIC · `d3d11.h` 가 따라 들어온다.

---

## 4. 발견 — BC1 이 Vulkan 에서 흰색으로 떨어진다

착수 근거가 되는 실측이다. **정적 판독이며 실행으로 확인하지 않았다**(§9).

### 4.1 사슬

1. `DataSystem.cpp:998` — BaseColorMap 만 `compress = true` 로 로드한다.
   나머지 넷(Normal · ORM · AO · Emissive)은 `false`.
   ```cpp
   material.UseBaseColorMap(loadTexture(standard_material::property::BaseColorMap,
       material.m_baseColorTexName, true));
   ```
2. `Texture.cpp:270` — `isCompress` 면 `BC1_UNORM_SRGB` 로 압축해
   `m_cpuPixels` 에 게시한다.
3. `DX12TextureCache.cpp:327` — `desc.Format = metadata.format` 을 그대로 쓴다.
   BC1 이 그대로 올라간다. **정상 동작.**
4. `VulkanRenderServices.cpp:116` — `FormatOf(DXGI_FORMAT)` 이 처리하는 것은
   `R8G8B8A8_UNORM` · `R8G8B8A8_UNORM_SRGB` · `B8G8R8A8_UNORM` ·
   `B8G8R8A8_UNORM_SRGB` **넷뿐**이고 나머지는 `RHIFormat::Unknown` 이다.
5. `VulkanRenderServices.cpp:159` — `Unknown` 이면 거절한다.
   ```
   outError = "Vulkan GizmoIcon 슬라이스는 2D RGBA8 자산만 지원한다";
   ```
6. 호출부(`:387`)는 실패 시 흰색 solid 로 폴백한다.

즉 **Vulkan 백엔드에서 모든 재질의 base color 가 흰색**이 된다. 나머지 네 맵은
압축을 안 하므로 정상이다 — 그래서 "화면이 아예 안 나온다"가 아니라 "알베도만
빠진다"로 나타난다.

### 4.2 왜 지금까지 안 보였나

- 오류 문자열이 `GizmoIcon 슬라이스` 다. 이 코드가 기즈모 아이콘 전용으로
  들어왔고, 그 뒤 같은 캐시가 재질 텍스처 경로(`GetOrUpload`)에 연결되면서
  전제가 바뀌었는데 메시지와 검사 범위가 그대로 남았다.
- 실패가 **조용하다.** `outError` 를 받아 흰색을 돌려주므로 파이프라인은 계속 돈다.
- DX12 는 멀쩡하다. 사용자가 보는 기본 경로가 DX12 라 눈에 안 띈다.
  (`gate-measures-stale-binary` · `harness-limit-verdict-was-real-defect` 와 같은 양식)

### 4.3 이것이 설계에 주는 것

B4 의 **RED 기준선**이다. 고침 전 상태를 먼저 게이트로 굳히고, 그 전환으로
판정한다. 자가 검증이 착수 전 상태를 지키면 고침이 성공할수록 실패하므로
(`gate-premise-flips-on-landing`), 이 게이트는 처음부터 "BC1 자산이 Vulkan 에서
백엔드 간 다이제스트 일치" 를 묻는 형태로 쓴다 — 지금은 붉고 B4 가 초록으로 만든다.

---

## 5. 설계

### 5.1 선행 — `RHIFormat` 에 블록 압축 어휘가 없다

`RHIFormat.h:73` 주석:

> 블록 압축 포맷이 들어오면 이 함수로는 부족해지고, 그때 블록 크기까지
> 답하는 형태로 바꾼다 — **지금 실사용에 압축 포맷이 없어 넣지 않는다.**

이 문장은 사실과 다르다. BC1 은 §2.3 대로 살아 있다. 어휘에 안 보였던 이유는
**DX12 업로드 경로가 `RHIFormat` 을 우회**하기 때문이다 — `metadata.format`(DXGI)
을 `D3D12_RESOURCE_DESC.Format` 에 직접 넣는다. 그래서 압축 포맷이 중립 어휘를
한 번도 통과한 적이 없고, 그 결과가 §4 의 비대칭이다.

### 5.2 타입 — `RHIImageView`

새 헤더 `Engine/RenderEngine/RHI/RHIImageData.h`. include 는
`<cstdint>` · `<cstddef>` · `"RHIFormat.h"` 뿐. `d3d11.h` · `dxgi` ·
`DirectXTex.h` 를 물지 않는다.

```cpp
/// 서브리소스 하나의 CPU 픽셀. 소유하지 않는다.
struct RHISubimage
{
    const uint8_t* pixels{ nullptr };
    size_t   rowPitch{ 0 };
    size_t   slicePitch{ 0 };
    uint32_t width{ 0 };
    uint32_t height{ 0 };
};

/// CPU 이미지 전체의 읽기 전용 뷰. 소유하지 않는다 — 소유자는 Texture 다.
class RHIImageView
{
public:
    RHIFormat Format()      const;
    uint32_t  Width()       const;
    uint32_t  Height()      const;
    uint32_t  MipLevels()   const;
    uint32_t  ArraySize()   const;
    bool      IsCube()      const;
    bool      IsEmpty()     const;

    /// subresource = mip + arraySlice * mipLevels — DX12 규약과 같게 고정한다.
    uint32_t   SubresourceCount() const;
    RHISubimage At(uint32_t mip, uint32_t arraySlice) const;
};
```

**왜 뷰이고 컨테이너가 아닌가.** `Texture::m_cpuPixels` 는 이미
`shared_ptr<ScratchImage>` 로 소유를 잡고 있고, "은퇴 후 재업로드가 같은 픽셀을
다시 읽어야 하므로 놓지 않는다" 는 규약이 `Texture.h:110` 에 실측 근거와 함께
적혀 있다(씬 왕복 후 재업로드 실패 3102건). 소유를 옮기면 그 규약을 처음부터
다시 세워야 한다. 뷰는 그것을 건드리지 않는다.

**무엇을 담는지는 소비자에서 뽑았다.** 추측이 아니라 §2.1 의 실사용 심볼 그대로다:
DX12 는 `width/height/arraySize/mipLevels/format/dimension` 과 `pixels`·`rowPitch`,
Vulkan 은 거기에 BGRA 스위즐 판정을 더 쓴다. BGRA 는 `RHIFormat` 어휘에 없는
`B8G8R8A8` 을 백엔드가 스스로 뒤집던 것이므로, 뷰에는 두지 않고 §5.3 의 어휘 확장으로
흡수한다.

### 5.3 `RHIFormat` 확장

실사용에서 뽑는다는 그 파일의 규율을 그대로 지킨다 — 지금 쓰이는 것만 넣는다.

```cpp
// ── 블록 압축 (TextureCodecBoundaryDesign B0) ──
// 끝에 두는 이유는 기존 값과 같다: enum 값이 캐시 키로 런타임 밖에 스칠 수 있어
// 중간 삽입을 금한다.
BC1Unorm,
BC1UnormSrgb,

// ── BGRA (WIC PNG 로더가 남기는 것) ──
BGRA8Unorm,
BGRA8UnormSrgb,
```

`RHIFormatBytes` 는 블록 포맷에서 `0` 을 돌려주는 현 계약을 그대로 둔다(픽셀당
바이트가 정의되지 않으므로 그것이 옳다). 대신 블록 정보를 답하는 함수를 더한다:

```cpp
struct RHIFormatBlock
{
    uint32_t width{ 1 };
    uint32_t height{ 1 };
    uint32_t bytes{ 0 };   // 0 이면 이 포맷을 모른다
};
constexpr RHIFormatBlock RHIFormatBlockOf(RHIFormat);   // BC1 → {4, 4, 8}
```

★ 함정: `RHIFormatBytes` 를 블록 포맷에 대해 "블록당 바이트" 로 재해석하면 안 된다.
그 함수는 리드백 행 간격과 업로드 크기 계산 63곳을 하나로 모은 정본이고(R2c-b),
의미를 바꾸면 압축 포맷을 안 쓰는 그 63곳이 조용히 틀린다.

### 5.4 경계 이후의 모양

```
                          ┌──────────────────────────────┐
  파일 · 바이트  ───────► │ Texture (코덱 구현: DirectXTex) │
                          └──────────────┬───────────────┘
                                         │ RHIImageView  ← 여기가 경계
                          ┌──────────────┴───────────────┐
                          │      IRenderTextureCache      │
                          └───────┬───────────────┬───────┘
                          DX12TextureCache   VulkanTextureCache
```

DirectXTex 는 경계 **위쪽**에만 남는다. 아래쪽에서 DXGI 를 아는 것은
`DX12Format.h` 의 대응표 하나뿐이고, 그것은 DX12 백엔드의 정당한 소유물이다.

---

## 6. 슬라이스

의존이 일방향이라 순서를 바꿀 수 없다. 각 슬라이스는 단독으로 빌드되고 단독으로
게이트를 통과해야 한다.

### B0 — `RHIFormat` 에 블록·BGRA 어휘를 넣는다

- `RHIFormat` 열거 4종 추가, `RHIFormatBlockOf` 신설
- `RHIFormat.h:73` 의 "실사용에 압축 포맷이 없다" 주석을 실측으로 정정
- 소비자 변경 0 — 순수 추가

### B1 — `RHIImageData.h` 를 세운다

- 타입만. 소비자 0.
- `RHIImageView` 를 `ScratchImage` 에서 만드는 어댑터는 여기 두지 **않는다**.
  그것을 두면 이 헤더가 DirectXTex 를 물어 존재 이유가 없어진다. 어댑터는 B2 의
  `Texture.cpp` 안에 둔다.

### B2 — `Texture` 가 뷰를 낸다

- `GetCpuPixels()` → `GetImageView()`. 구현은 여전히 `ScratchImage` 를 읽되
  **`Texture.cpp` 안에서만** 읽는다.
- `Texture.h` 에서 `#include <d3d11.h>` · `#include <DirectXTex.h>` 제거.
  `m_cpuPixels` 는 전방선언한 `shared_ptr<DirectX::ScratchImage>` 로 남긴다
  (불완전 타입 `shared_ptr` 멤버는 소멸자가 `.cpp` 에 있으면 성립한다 — `Texture`
  의 소멸자는 이미 `.cpp` 에 있다).
- `Core.Definition.h:35` 의 `<DirectXTex.h>` 제거. §2.2 대로 다른 6개 소비자는
  이 심볼을 안 쓴다. 전이에 기대던 4개 파일(`DataSystem.cpp` ·
  `VulkanRenderServices.cpp` · `EnhancedGizmoIconTest.cpp` · `VulkanSkyBoxTest.cpp`)은
  B3 에서 정리되므로, 그때까지는 직접 include 를 임시로 더한다.
- `CreateFromPixels(..., DXGI_FORMAT textureFormat, ...)` 의 인자를 `RHIFormat` 으로
  바꾼다 — 이것도 공개 표면의 DXGI 누출이다.

### B3 — 두 백엔드를 뷰로 바꾼다

- `DX12TextureCache::UploadFromCpuPixels(const DirectX::ScratchImage&)`
  → `(const RHIImageView&)`. `D3D12_RESOURCE_DESC.Format` 은 `DX12Format.h` 의
  기존 대응표를 탄다.
- `VulkanRenderServices::FormatOf(DXGI_FORMAT)` **삭제**. 이 함수의 존재 이유가
  "DXGI 가 여기까지 들어왔다" 였다. BGRA 스위즐 판정은 `RHIFormat::BGRA8*` 로
  바뀐다.
- 이 시점에서 §2.1 표의 "사라진다" 가 전부 이행된다.

### B4 — BC1 을 Vulkan 이 받는다 (§4 청산)

- `VK_FORMAT_BC1_RGBA_UNORM_BLOCK` / `_SRGB_BLOCK` 대응
- 업로드 크기·행 간격 계산을 `RHIFormatBlockOf` 기준 블록 단위로 고친다.
  현재 `UploadRgba8` 의 `mipWidth * mipHeight * 4` 는 블록 포맷에서 틀린다.
- 함수 이름 `UploadRgba8` 이 더 이상 사실이 아니므로 함께 고친다. §4.2 대로
  이름이 낡아 남으면 다음 사람이 같은 오독을 한다.

---

## 7. 게이트

이 저장소의 규율대로, **새 검사가 초록이면 변이로 이빨을 증명한다**
(`mutation-proves-gate-has-teeth`).

| 슬라이스 | 게이트 | 변이 증명 |
|---|---|---|
| B0 | `RHIFormatBlockOf` 왕복 단정 (BC1 → 4×4×8) | BC1 의 `bytes` 를 8→16 으로 틀면 몇 건이 정확히 빨개지는가 |
| B2 | `Texture.h` · `Core.Definition.h` 에 DirectXTex 심볼 0건 (grep 계약, 래칫) | 심볼 하나를 되돌려 넣으면 빨개지는가 |
| B3 | **A/B 다이제스트** — 같은 자산의 스테이징 버퍼 바이트를 DX12·Vulkan 각각에서 떠 비교 | 한쪽 `rowPitch` 를 1 늘리면 빨개지는가 |
| B4 | BC1 자산(BaseColorMap)의 백엔드 간 다이제스트 일치 | — **착수 전부터 RED** 여야 한다(§4). 초록이면 §4 판독이 틀린 것이므로 착수 전에 그것부터 밝힌다 |

### 7.1 B3 게이트가 동어반복이 아닌 이유

`bridge-copies-name-not-meaning` · `control-group-needs-independent-derivation` 의
교훈대로, 대조군은 독립 유도를 가져야 한다. 여기서 두 백엔드는 footprint 를
**각자 계산한다** — DX12 는 `GetCopyableFootprints`(드라이버), Vulkan 은 직접 계산.
같은 뷰를 읽되 배치를 독립적으로 유도하므로 순브리지가 아니다.

다만 "둘 다 같은 방식으로 틀린" 경우는 못 잡는다. 그래서 DX12 축은 기존
`dx12.scene` 픽셀 골든에 계속 걸어 둔다 — 절대 기준은 그쪽이 갖는다.

### 7.2 못 잡는 것 (문서화까지가 증명)

- 뷰가 가리키는 픽셀의 **내용**이 맞는지는 이 게이트들이 못 본다. 디코드가 틀리면
  두 백엔드가 사이좋게 같은 쓰레기를 올리고 다이제스트는 일치한다.
  그 축은 `dx12.scene` 픽셀 골든이 담당한다.
- 밉 체인이 잘못 생성되는 경우. 지금 파이프라인은 밉을 만들지 않으므로
  (`GenerateMipMaps` 호출 0건) 해당 없음 — 도입하면 그때 게이트를 더한다.

---

## 8. 하지 않는 것

- **DirectXTex 교체.** B3 이 끝나면 남는 곳이 3개 파일(`Texture.cpp` ·
  `ModelAssetGeneration.cpp` · `EditorAssetDatabase.cpp`)이다. stb 로 갈지, BC1
  인코더를 따로 둘지는 **그때 재는 문제**다. 지금 정하면 근거 없이 정하는 것이다.
- **BC7 · BC6H 도입.** 품질 판단이지 경계 판단이 아니다. 다만 도입하게 되면
  DirectXTex 의 CPU BC7 은 느려서 어차피 ISPC 계열을 봐야 한다는 점만 적어 둔다
  (UE 의 `TextureFormatIntelISPCTexComp`, Unity 가 같은 선택을 한 지점).
- **PNG 라이터 이중화 정리.** `EditorAssetDatabase.cpp` 가 한 파일에서
  `SaveToWICFile`(`:574`)과 `stbi_write_png`(`:690`, `:704`)를 둘 다 쓴다. 별건이고
  B0~B4 와 의존이 없다.
- **`Texture.cpp` 의 네 로드 경로 통합.** 경계가 서면 자연히 접히지만, 같은
  슬라이스에서 하면 A/B 다이제스트의 판별력이 흐려진다(무엇이 바뀌어서 달라졌는지
  못 가른다). 별도 슬라이스로 뒤에 둔다.

---

## 9. 착수 전에 확인할 것 — 미검증 전제

정직하게 적는다. 아래는 **정적 판독**이고 실행으로 확인하지 않았다.

1. **§4 의 흰색 폴백.** 코드 사슬은 위 6단계로 닫히지만, Vulkan 백엔드로 실제 씬을
   띄워 base color 가 흰색인지 보지 않았다. 착수 첫 행동은 이것의 확인이어야
   한다 — `plan-target-may-be-already-dead` 대로, 지목한 대상이 이미 다른 이유로
   죽어 있을 수 있다. 확인 방법: Vulkan 경로로 재질 씬을 띄우고
   `VulkanTextureCache::GetStats()` 의 `failures` 계수와 오류 문자열을 읽는다.
2. **B2 의 불완전 타입 `shared_ptr` 멤버.** `Texture` 소멸자가 `.cpp` 에 있는 것은
   확인했으나, 이동 생성자(`Texture(Texture&&)`)도 `.cpp` 에 있는지 확인하지 않았다.
   헤더에 있으면 그것도 옮겨야 한다.
3. **`Core.Definition.h` 의 DirectXTex 제거 파급.** 직접 소비자 9개는 세었지만
   유니티 빌드에서 앞선 파일이 순서를 맞춰 가려 온 이력이 `Texture.h:9` 에
   적혀 있다. 제거 후 파급은 **전체 리빌드로만** 판정한다 — 증분은
   `build-exit-code-not-mtime` 대로 exit code 로만 본다.
4. **밉 체인.** `GenerateMipMaps` 호출 0건을 확인했으나, 자산이 밉을 이미 담고
   들어오는 경우(DDS)는 `metadata.mipLevels > 1` 이 될 수 있다. DX12 는 그것을
   처리하고 Vulkan 은 `mipLevels` 를 읽되 크기 계산이 비압축 전제다. B4 에서
   함께 본다.
