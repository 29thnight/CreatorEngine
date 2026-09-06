#pragma once
#include <cstdint>

// 픽셀 포맷 — 백엔드 중립 (PHASE 3-1 재정의, V1).
//
// ── 왜 이것이 첫 슬라이스인가 ──
//
// 계획서 §3.2가 RHIFormat을 열거만 해 두고 도입하지 않아서, 경계 타입들이
// (RHIBindingDesc · RHIReadback · RHITextureDesc · RHIDepthTargetDesc) 전부
// DXGI_FORMAT을 그대로 들고 다닌다 — 실측 145건. 다른 모든 슬라이스가 포맷을
// 인자로 받으므로(핸들·바인딩·파이프라인·리드백) 이것을 먼저 갈지 않으면
// 뒤의 슬라이스가 전부 DXGI에 묶인 채로 만들어진다.
//
// ── 열거를 실사용에서 뽑았다 ──
//
// 리포 전체에서 실제로 쓰이는 DXGI 포맷은 18종뿐이다(2026-08-10 실측).
// "언젠가 쓸지 모르는" 것을 넣지 않는다 — 백엔드마다 대응표가 늘고, 쓰지
// 않는 항목은 틀려도 아무도 모른다. 필요해지면 그때 더한다.
//
// ── 이름 규칙 ──
//
// 채널·비트·해석을 이 순서로 적는다(RGBA16Float). DXGI도 Vulkan도 같은
// 정보를 담고 표기만 다르므로, 어느 쪽으로도 기계적으로 대응한다:
//
//   RGBA16Float  ↔  DXGI_FORMAT_R16G16B16A16_FLOAT  ↔  VK_FORMAT_R16G16B16A16_SFLOAT
//   RGBA8Unorm   ↔  DXGI_FORMAT_R8G8B8A8_UNORM      ↔  VK_FORMAT_R8G8B8A8_UNORM
//
// ★ 깊이를 셰이더로 읽을 때의 포맷 변환(D32_FLOAT → R32_FLOAT 따위)은 여기
//   없다. 그것은 DX12가 타입리스 리소스를 요구해서 생긴 사정이고, Vulkan은
//   같은 포맷에 aspect mask로 읽는다. 즉 백엔드 사정이므로 대응표에 남기고
//   상위는 "깊이를 읽는다"만 말한다(RHIBindingDesc::SrvDepth).
enum class RHIFormat : uint16_t
{
    Unknown = 0,

    // ── 색 ──
    RGBA8Unorm,
    RGBA8UnormSrgb,
    RGBA16Float,
    RGBA32Float,
    RG16Float,
    RG32Float,
    RGB32Float,      // 정점 위치·노멀. 렌더 타깃으로는 못 쓴다
    R16Float,
    R16Unorm,
    R32Float,
    R32Uint,

    // ── 깊이 ──
    D16Unorm,
    D24UnormS8Uint,
    D32Float,
    D32FloatS8Uint,

    // ── 정수 정점 속성 (V4/I5-D) ──
    // experiment 정점의 BoneIndices(experiment::VertexFormat::RGBA8Uint) 대응.
    // 끝에 두는 이유: enum 값이 캐시 키 등 런타임 밖에 스치더라도 기존 값을
    // 밀지 않기 위해서다(중간 삽입 금지).
    RGBA8Uint,

    // ── 블록 압축 (축 A) ──
    //
    // ★ "실사용에 압축 포맷이 없다"는 아래 RHIFormatBytes의 전제가 틀렸다.
    //   legacy 재질 경로는 baseColorMap을 BC1_UNORM_SRGB로 압축해 CPU에
    //   남긴다(Texture::LoadSharedFromPath, isCompress=true — 그 인자가
    //   켜지는 자리가 baseColorMap 하나다). 그런데 이 열거에 대응이 없어서
    //   DX12만 DXGI_FORMAT을 컨테이너째 들고 다녀 통과했고, Vulkan은
    //   포맷을 몰라 업로드를 통째로 거절하고 흰색을 돌려줬다.
    //   즉 "같은 자산이 백엔드에 따라 다른 그림"이었고, 압축 포맷이 상위
    //   어휘에 없었던 것이 그 비대칭의 원인이다.
    //
    // 여기도 끝에 붙인다(중간 삽입 금지).
    //
    // BC3 는 저작 자산에서 온다 — blueNoise.dds 가 DXT5 다. 그것도 DX12 만
    // 통과하고 Vulkan 은 거절하던 자산이었다. BC3 sRGB는 W6에서 아래에 추가했다.
    BC1Unorm,
    BC1UnormSrgb,
    BC3Unorm,

    // ── 채널 순서가 뒤집힌 8비트 (축 A) ──
    //
    // WIC PNG 디코더는 FORCE_RGB 가 없으면 흔히 BGRA8 을 남긴다. DX12 는
    // 그것을 그대로 GPU 에 올려 왔지만(컨테이너째 DXGI_FORMAT 을 들고
    // 다녔으므로) Vulkan 은 어휘에 BGRA 가 없어서 업로드 때마다 픽셀
    // 하나하나 채널을 뒤집었다 — 두 백엔드가 같은 자산을 다르게 다루던
    // 자리이고, BC1 과 원인이 같다(상위 어휘의 구멍).
    //
    // 두 API 모두 이 포맷을 네이티브로 받는다. 어휘에 넣으면 그 스위즐
    // 루프가 통째로 사라진다.
    BGRA8Unorm,
    BGRA8UnormSrgb,
    BC3UnormSrgb,
};

/// 픽셀 하나의 바이트 수. 0이면 이 포맷을 모르는 것이다.
///
/// ★ 여기 두는 이유: 리드백의 행 간격 계산과 업로드 크기 계산이 같은 표를
///   필요로 하는데, 백엔드에 두면 그 둘이 서로 다른 표를 보게 된다(R2c-b가
///   행 간격 계산 63곳을 하나로 모은 것과 같은 이유다).
///   블록 압축 포맷은 이 함수로 답할 수 없다 — 텍셀 하나의 바이트 수가
///   정수가 아니다. 그래서 0을 준다. 크기를 재는 쪽은 아래 RHIFormatRowPitch·
///   RHIFormatSlicePitch를 쓴다(그 둘은 압축·비압축을 함께 답한다).
constexpr uint32_t RHIFormatBytes(RHIFormat format)
{
    switch (format)
    {
    case RHIFormat::RGBA32Float:    return 16;
    case RHIFormat::RGBA16Float:
    case RHIFormat::RG32Float:      return 8;
    case RHIFormat::RGB32Float:     return 12;
    case RHIFormat::RGBA8Unorm:
    case RHIFormat::RGBA8UnormSrgb:
    case RHIFormat::RGBA8Uint:
    case RHIFormat::BGRA8Unorm:
    case RHIFormat::BGRA8UnormSrgb:
    case RHIFormat::RG16Float:
    case RHIFormat::R32Float:
    case RHIFormat::R32Uint:
    case RHIFormat::D24UnormS8Uint:
    case RHIFormat::D32Float:       return 4;
    case RHIFormat::D32FloatS8Uint: return 8;   // 32비트 깊이 + 8비트 스텐실 + 패딩
    case RHIFormat::R16Float:
    case RHIFormat::R16Unorm:
    case RHIFormat::D16Unorm:       return 2;
    default:                        return 0;
    }
}

/// 깊이 포맷인가 — 뷰를 만들 때 렌더 타깃과 깊이 타깃이 갈린다.
constexpr bool RHIFormatIsDepth(RHIFormat format)
{
    switch (format)
    {
    case RHIFormat::D16Unorm:
    case RHIFormat::D24UnormS8Uint:
    case RHIFormat::D32Float:
    case RHIFormat::D32FloatS8Uint: return true;
    default:                        return false;
    }
}

/// 채널 수. 리드백 캡처가 At(x, y, channel)의 범위를 판단하는 데 쓴다.
constexpr uint32_t RHIFormatChannels(RHIFormat format)
{
    switch (format)
    {
    case RHIFormat::RGBA8Unorm:
    case RHIFormat::RGBA8UnormSrgb:
    case RHIFormat::RGBA8Uint:
    case RHIFormat::BGRA8Unorm:
    case RHIFormat::BGRA8UnormSrgb:
    case RHIFormat::RGBA16Float:
    case RHIFormat::RGBA32Float:    return 4;
    case RHIFormat::RGB32Float:     return 3;
    case RHIFormat::RG16Float:
    case RHIFormat::RG32Float:      return 2;
    case RHIFormat::R16Float:
    case RHIFormat::R16Unorm:
    case RHIFormat::R32Float:
    case RHIFormat::R32Uint:
    case RHIFormat::D16Unorm:
    case RHIFormat::D24UnormS8Uint:
    case RHIFormat::D32Float:
    case RHIFormat::D32FloatS8Uint: return 1;
    default:                        return 0;
    }
}

// ── 블록 압축을 아는 크기 계산 (축 A) ──
//
// ★ 압축·비압축을 한 식으로 답하게 둔 것이 요점이다. 호출부가 "이 포맷이
//   압축인가"를 먼저 묻고 갈라 쓰면 그 분기가 업로드 경로마다 복제되고,
//   빠뜨린 곳이 정확히 지금의 Vulkan 결함(BC1을 폭*4로 재던 자리)이 된다.
//   비압축에서 블록 한 변은 1이라 아래 두 식이 그대로 옛 계산과 같아진다.

/// 블록 압축 포맷인가.
constexpr bool RHIFormatIsBlockCompressed(RHIFormat format)
{
    switch (format)
    {
    case RHIFormat::BC1Unorm:
    case RHIFormat::BC1UnormSrgb:
    case RHIFormat::BC3Unorm:
    case RHIFormat::BC3UnormSrgb:   return true;
    default:                        return false;
    }
}

/// 블록 한 변의 텍셀 수. 비압축은 1이다.
constexpr uint32_t RHIFormatBlockExtent(RHIFormat format)
{
    return RHIFormatIsBlockCompressed(format) ? 4u : 1u;
}

/// 블록 하나의 바이트 수. 비압축이면 텍셀 하나의 바이트 수와 같다.
constexpr uint32_t RHIFormatBlockBytes(RHIFormat format)
{
    switch (format)
    {
    case RHIFormat::BC1Unorm:
    case RHIFormat::BC1UnormSrgb:   return 8;
    case RHIFormat::BC3Unorm:
    case RHIFormat::BC3UnormSrgb:   return 16;
    default:                        return RHIFormatBytes(format);
    }
}

/// 한 행의 바이트 수. 압축이면 블록 한 줄의 바이트 수다(행 4개분).
constexpr uint64_t RHIFormatRowPitch(RHIFormat format, uint32_t width)
{
    const uint32_t extent = RHIFormatBlockExtent(format);
    const uint64_t blocks = (static_cast<uint64_t>(width) + extent - 1u) / extent;
    return blocks * RHIFormatBlockBytes(format);
}

/// 서브리소스 하나의 바이트 수.
constexpr uint64_t RHIFormatSlicePitch(RHIFormat format, uint32_t width,
    uint32_t height)
{
    const uint32_t extent = RHIFormatBlockExtent(format);
    const uint64_t rows = (static_cast<uint64_t>(height) + extent - 1u) / extent;
    return RHIFormatRowPitch(format, width) * rows;
}

/// 행 복사에서 한 번에 옮길 '행'의 개수. 압축은 블록 줄이 4텍셀을 덮는다.
constexpr uint32_t RHIFormatRowCount(RHIFormat format, uint32_t height)
{
    const uint32_t extent = RHIFormatBlockExtent(format);
    return (height + extent - 1u) / extent;
}
