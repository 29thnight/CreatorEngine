#pragma once
#include "RHIHandle.h"

// 리소스 상태 — 백엔드 중립 (PHASE 3-1 재정의, V3).
//
// ── 어디서 왔나 ──
//
// 렌더 그래프의 RGResourceState 였다. 그래프가 배리어를 유도하려고 만든
// 어휘인데, 실제로는 그래프 **밖**에서도 같은 말이 필요했다 — 업로드 직후의
// 한 번짜리 전이, 캐시가 올린 텍스처를 넓히는 자리, 자가 검증이 리드백 전에
// 거는 전이. 그 자리들이 전부 D3D12_RESOURCE_STATES 를 손으로 적고 있었다.
//
// 그래서 그래프의 것이 아니라 RHI 의 것으로 옮긴다. 이름이 RG- 로 남아 있으면
// "그래프 안에서만 쓰는 말"로 읽히고, 실제로 그렇게 읽혀서 그래프 밖이
// 원시 상수로 갈라져 나갔다.
//
// ── 왜 D3D12 상태를 그대로 안 쓰나 ──
//
// ★ Vulkan 에는 대응이 없다. 거기서는 VkImageLayout(레이아웃) + 접근 마스크 +
//   파이프라인 스테이지가 따로 놀고, 이미지에는 aspect mask 까지 붙는다.
//   D3D12_RESOURCE_STATES 하나가 그 셋을 뭉쳐 놓은 것이라, 원시 상수를
//   상위가 적는 순간 Vulkan 백엔드는 그 뭉침을 되돌릴 수 없다.
//
//   반대로 이 열거는 '무엇에 쓰는가'만 말한다 — 그것이면 두 백엔드가 각자
//   필요한 것을 만들어 낼 수 있다.
//
// 목록이 D3D12 상태의 부분집합인 것은 의도다. 상태를 다 노출하면 호출부가
// 배리어를 손으로 짜는 것과 같아진다.
enum class RHIResourceState
{
    Common,
    RenderTarget,
    DepthWrite,
    DepthRead,

    // 그래픽·컴퓨트 패스가 공통으로 쓰는 SRV 상태. 호출부가 셰이더 단계를
    // 따로 선언하지 않으므로 PIXEL 전용으로 좁히면 SSAO/SSGI 같은 compute
    // 패스가 같은 리소스를 읽는 순간 GPU Validation 오류가 된다.
    ShaderResource,

    // 픽셀 셰이더만 읽는다. 위의 ShaderResource(=모든 단계)보다 좁다.
    //
    // ★ 넣을지 한참 고민했다. 상태를 잘게 노출할수록 호출부가 배리어를 손으로
    //   짜는 쪽에 가까워지기 때문이다. 그런데 이것은 '규칙'이 아니라 실제로
    //   코드가 쓰는 '상태'였다 — 텍스처 캐시는 업로드를 이 상태로 끝내고,
    //   IBL 생성기와 스카이박스는 그것을 계약으로 적어 두었다. 어휘에서 빼면
    //   그 자리들이 다시 원시 상수로 갈라져 나간다.
    //
    //   Vulkan 에서도 갈린다 — FRAGMENT_SHADER 스테이지만 기다리는 것과 모든
    //   셰이더 스테이지를 기다리는 것은 다른 배리어다.
    PixelShaderResource,

    // 깊이를 DSV로 걸어 두고 같은 프레임에 셰이더로도 읽는다.
    //
    // 데칼처럼 '깊이 테스트는 하되 쓰지는 않고, 그 깊이로 월드 좌표를
    // 복원하는' 패스가 이것을 쓴다. DX11은 읽기 전용 DSV라는 개념을 이렇게
    // 쓰지 않고 깊이를 통째로 복사했는데(DecalPass), D3D12는 두 용도를
    // 동시에 허용하므로 화면 크기 복사가 통째로 사라진다.
    //
    // 쓰는 쪽의 계약: DSV를 읽기 전용으로 만들어야 한다. 읽기 전용이 아닌
    // DSV로 이 상태의 리소스를 걸면 검증 레이어가 잡는다.
    DepthReadShaderResource,

    UnorderedAccess,
    CopySource,
    CopyDest,
};

/// 전이 하나. 그래프 밖에서 상태를 바꿀 때 쓴다(그래프 안은 usage 선언이 한다).
///
/// ★ before 를 받는 이유: 백엔드가 리소스의 현재 상태를 추적하지 않는다.
///   추적하려면 표가 상태를 들어야 하고, 그러면 그래프의 상태 추적과 두 벌이
///   되어 어긋날 자리가 생긴다. 아는 쪽이 말한다 — 그래프는 writeback 으로
///   알고, 그래프 밖은 방금 자기가 만든 상태를 안다.
struct RHITransition
{
    RHITextureHandle texture;
    RHIResourceState before{ RHIResourceState::Common };
    RHIResourceState after{ RHIResourceState::Common };
};
