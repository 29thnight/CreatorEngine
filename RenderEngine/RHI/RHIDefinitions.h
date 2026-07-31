#pragma once
#include <cstdint>

// ─────────────────────────────────────────────────────────────────────────
// RHI (Render Hardware Interface) — PHASE 3-1
//
// 상위 렌더 코드(패스·프록시·이펙트)가 그래픽 API를 직접 만지지 않게 하는
// 경계다. DX11 구현이 이 뒤로 들어가고, DX12(EnhancedSceneRenderer)는 같은
// 인터페이스의 두 번째 백엔드로 꽂힌다 — 교체가 스위치가 되는 구조의 전제.
//
// 이 헤더에는 어떤 그래픽 API 헤더도 include하지 않는다. 그것이 규약이다.
// ─────────────────────────────────────────────────────────────────────────

enum class RHIBackendKind : uint8_t
{
    DX11,
    DX12,
};

// D3D11/D3D12의 공통 부분집합만 둔다. 값은 백엔드가 각자 매핑한다.
enum class RHIPrimitiveTopology : uint8_t
{
    PointList,
    LineList,
    LineStrip,
    TriangleList,
    TriangleStrip,
};

// D3D11_VIEWPORT / D3D12_VIEWPORT와 필드 순서·의미가 같다(둘 다 동일 배치라
// 백엔드에서 그대로 옮겨 담는다). 값 의미는 픽셀 단위 클라이언트 좌표.
struct RHIViewport
{
    float x = 0.f;
    float y = 0.f;
    float width = 0.f;
    float height = 0.f;
    float minDepth = 0.f;
    float maxDepth = 1.f;
};

// ── 전환기 임시 핸들 ──
//
// 최종형은 RHITexture/RHIBuffer가 뷰를 소유하는 것이지만, 지금은 기존 코드가
// ID3D11*View*를 직접 들고 다닌다. 이 typedef는 그 포인터를 "타입을 숨긴 채"
// 경계 너머로 나르는 임시 통로다 — DX11에서는 ID3D11RenderTargetView* 등이
// 들어온다. 리소스 계층이 RHI 타입으로 넘어오는 시점(텍스처 이식)에 이
// typedef들은 실제 클래스로 교체되고, 그때까지 새 코드가 이 핸들을 저장해
// 두는 것은 금지다(넘겨받아 즉시 쓰기만 한다).
using RHINativeRenderTarget      = void*;   // DX11: ID3D11RenderTargetView*
using RHINativeDepthStencil      = void*;   // DX11: ID3D11DepthStencilView*
using RHINativeShaderResource    = void*;   // DX11: ID3D11ShaderResourceView*
using RHINativeBuffer            = void*;   // DX11: ID3D11Buffer*
using RHINativeDepthStencilState = void*;   // DX11: ID3D11DepthStencilState*
using RHINativeBlendState        = void*;   // DX11: ID3D11BlendState*
using RHINativeUnorderedAccess   = void*;   // DX11: ID3D11UnorderedAccessView*
using RHINativeComputeShader     = void*;   // DX11: ID3D11ComputeShader*
using RHINativeSamplerState      = void*;   // DX11: ID3D11SamplerState*
using RHINativeResource          = void*;   // DX11: ID3D11Resource*
