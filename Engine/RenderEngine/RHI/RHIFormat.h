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
};

/// 픽셀 하나의 바이트 수. 0이면 이 포맷을 모르는 것이다.
///
/// ★ 여기 두는 이유: 리드백의 행 간격 계산과 업로드 크기 계산이 같은 표를
///   필요로 하는데, 백엔드에 두면 그 둘이 서로 다른 표를 보게 된다(R2c-b가
///   행 간격 계산 63곳을 하나로 모은 것과 같은 이유다).
///   블록 압축 포맷이 들어오면 이 함수로는 부족해지고, 그때 블록 크기까지
///   답하는 형태로 바꾼다 — 지금 실사용에 압축 포맷이 없어 넣지 않는다.
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
