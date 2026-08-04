#ifndef DYNAMICCPP_EXPORTS
#include "EnhancedForwardPass.h"
#include "DX12DeviceResources.h"
#include "DX12PSOManager.h"
#include "DX12RootSignatureCache.h"
#include "EnhancedRenderGraph.h"

#include <algorithm>

// ★★ 골격만 있다. 아직 아무것도 그리지 않는다. ★★
//
// 설계는 헤더에 적었다. 남은 단계(순서대로 채운다):
//   [ ] 1. 광원 컬링 컴퓨트 — 타일 프러스텀 vs 광원 구, 타일 목록 쓰기
//   [ ] 2. 컬링 자가 검증 — 타일 카운트 리드백, 알려진 배치로 단정
//        (광원 하나를 화면 중앙에 두면 중앙 타일들만 카운트가 1이어야 한다)
//   [ ] 3. 포워드 셰이딩 — forwardQueue 드로우 + 타일 목록 조회
//   [ ] 4. 참조 경로(전 광원 루프)와 픽셀 대조
//   [ ] 5. 광원 수 스케일링 실측 — Forward+가 이기는 경계 찾기
//
// Declare가 그래프를 건드리지 않으므로 호출부에 붙이면 안 된다.

bool EnhancedForwardPass::Initialize(const EnhancedFrameContext& context, std::string& outError)
{
    if (nullptr == context.resources || nullptr == context.psoManager ||
        nullptr == context.rootSignatures)
    {
        outError = "Forward+ 패스 컨텍스트가 불완전하다";
        return false;
    }

    return CreatePipelines(context, outError);
}

bool EnhancedForwardPass::CreatePipelines(const EnhancedFrameContext& context, std::string& outError)
{
    // 아직 셰이더가 없다. 파이프라인이 생기면 이 주석과 함께 지운다.
    (void)context;
    (void)outError;
    return true;
}

bool EnhancedForwardPass::PrepareFrame(const EnhancedFrameContext& context, std::string& outError)
{
    (void)outError;

    // 타일 수. 화면이 타일 크기로 나누어떨어지지 않으면 가장자리 타일이
    // 화면 밖까지 걸치는데, 컬링 셰이더가 화면 경계로 잘라야 한다 —
    // 안 자르면 가장자리 광원이 이웃 타일로 샌다.
    m_tileCountX = (context.width + kTileSize - 1) / kTileSize;
    m_tileCountY = (context.height + kTileSize - 1) / kTileSize;

    return true;
}

void EnhancedForwardPass::Declare(EnhancedRenderGraph& graph, const EnhancedFrameContext& context)
{
    // 아직 선언할 패스가 없다. 빈 패스를 넣으면 배리어와 컬링이 그것을
    // 진짜로 취급한다.
    (void)graph;
    (void)context;

    m_output = RGHandle{};
}

void EnhancedForwardPass::Shutdown()
{
    m_tileCountX = 0;
    m_tileCountY = 0;
    m_lastCulledLights = 0;
    m_lastOverflowTiles = 0;

    m_cullPSO = nullptr;
    m_shadePSO = nullptr;
    m_cullRootSignature = nullptr;
    m_shadeRootSignature = nullptr;
}

#endif
