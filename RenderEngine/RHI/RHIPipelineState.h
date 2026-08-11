#pragma once
#ifndef DYNAMICCPP_EXPORTS
#include <cstdint>

#include "RHIFormat.h"
#include "RHIPipelineLayout.h"   // RHICompareOp 를 공유한다

// 파이프라인 상태 기술 — 백엔드 중립 (V6).
//
// ── 어휘를 실사용에서 뽑았다 (V1·V4 와 같은 규칙) ──
//
// 패스 전체가 쓰는 값이 놀랄 만큼 적다. 실측:
//
//   채움 2(SOLID·WIREFRAME) · 컬링 2(NONE·BACK) · 토폴로지 2(삼각형·선)
//   블렌드 계수 4(SRC_ALPHA·INV_SRC_ALPHA·ONE·ZERO) · 블렌드 연산 1(ADD)
//   깊이 쓰기 2(ALL·ZERO) · 입력 분류 1(정점별)
//
// ★ D3D12 열거 전체를 옮겨 적지 않는다. 안 쓰는 값을 넣으면 Vulkan 백엔드가
//   그것까지 대응해야 하는데, 그 대응이 맞는지 검증할 자리가 없다 — 쓰는
//   코드가 없으므로 틀려도 아무도 모른다. 필요해지면 그때 는다.
//
// ── 왜 V6 이 여기까지인가 ──
//
// ★ ID3D12PipelineState* · ID3D12RootSignature* 는 그대로 둔다(실측 65건).
//   그것은 '기술'이 아니라 '만들어진 객체'이고, 불투명 핸들로 바꾸는 것은
//   두 번째 백엔드가 실제로 들어올 때 그 모양을 보고 정할 일이다.
//   지금 정하면 소비자가 하나인 채로 추상을 만드는 것이고, 구 RHI 가 죽은
//   이유가 정확히 그것이다(§1.1).

enum class RHIFillMode : uint8_t
{
    Solid = 0,
    Wireframe,
};

enum class RHICullMode : uint8_t
{
    None = 0,
    Back,
    Front,
};

enum class RHIDepthWrite : uint8_t
{
    All = 0,    ///< 기본. 깊이를 쓴다
    Zero,       ///< 깊이를 보되 쓰지 않는다(데칼이 이것을 쓴다)
};

/// 파이프라인이 조립하는 도형의 **부류**.
///
/// ★ 인코더의 RHIPrimitiveTopology(TriangleList·Strip·LineList…)와 다른
///   개념이다. 이쪽은 파이프라인을 굽는 시점에 정하는 부류이고, 저쪽은
///   드로우마다 정하는 구체적 배열이다. 한 파이프라인으로 List 와 Strip 을
///   둘 다 그릴 수 있으므로 둘은 하나가 될 수 없다.
///   (이름을 겹쳐 두었다가 컴파일러가 잡았다 — 겹쳤으면 조용히 한쪽이
///    가려졌을 자리다.)
enum class RHITopologyType : uint8_t
{
    Triangle = 0,
    Line,
    Point,
};

/// 블렌드 계수. 실사용 넷만 둔다.
enum class RHIBlendFactor : uint8_t
{
    Zero = 0,
    One,
    SrcAlpha,
    InvSrcAlpha,
};

enum class RHIBlendOp : uint8_t
{
    Add = 0,
};

/// 렌더 타깃 하나의 블렌드. MRT 에서 채널마다 다르게 켤 때 쓴다.
///
/// ★ writeMask 0 은 '바인딩돼 있어도 건드리지 않는다'는 뜻이다. 채널을
///   끄려고 타깃을 떼었다 붙였다 할 필요가 없어서 데칼이 이것을 쓴다.
struct RHIRenderTargetBlend
{
    bool           enable{ false };
    RHIBlendFactor srcColor{ RHIBlendFactor::One };
    RHIBlendFactor dstColor{ RHIBlendFactor::Zero };
    RHIBlendOp     colorOp{ RHIBlendOp::Add };
    RHIBlendFactor srcAlpha{ RHIBlendFactor::One };
    RHIBlendFactor dstAlpha{ RHIBlendFactor::Zero };
    RHIBlendOp     alphaOp{ RHIBlendOp::Add };

    /// 채널 쓰기 마스크. 비트 순서는 RGBA(1·2·4·8), 0xF 가 전부 쓴다.
    uint8_t        writeMask{ 0xF };
};

/// 정점 입력 원소 하나.
///
/// ★ semantic 이 const char* 인 것이 해시에서 함정이다 — 구조체를 통째로
///   바이트 해시하면 문자열 **주소**를 해시하게 되어 같은 레이아웃이 매번
///   다른 키가 되고 캐시가 논다. 내용으로 해시해야 한다(루트 시그니처에서
///   같은 것을 겪었다).
struct RHIInputElement
{
    const char* semantic{ nullptr };
    uint32_t    semanticIndex{ 0 };
    RHIFormat   format{ RHIFormat::Unknown };
    uint32_t    inputSlot{ 0 };
    uint32_t    alignedByteOffset{ 0 };

    /// 인스턴스별 데이터면 1 이상(그 값이 스텝 레이트). 0 이면 정점별이다.
    uint32_t    instanceDataStepRate{ 0 };
};

#endif // !DYNAMICCPP_EXPORTS
