#ifndef DYNAMICCPP_EXPORTS
#include "EnhancedSSGIPass.h"
#include "DX12DeviceResources.h"
#include "DX12PSOManager.h"
#include "DX12RootSignatureCache.h"
#include "EnhancedRenderGraph.h"

#include <algorithm>

// ★★ 골격만 있다. 아직 아무것도 그리지 않는다. ★★
//
// 설계는 헤더에 적었고 여기는 그 순서대로 채워 넣는 자리다. 남은 것:
//   1. Hi-Z 피라미드 빌드(컴퓨트) — 깊이 밉을 min으로 줄여 간다
//   2. 행진(트레이스) — Hi-Z를 타고 1/2 해상도로, 프레임당 kSlicesPerFrame
//   3. 리졸브 — 지난 프레임을 재투영해 누적(m_history 두 장을 번갈아)
//   4. 필터 — bilateral 한 번
//   5. 합성 — 업샘플 + 라이팅에 더하기
//
// 각 단계에 GPU 타임스탬프를 붙이고 기존 DX11 SSGI와 Release에서 시간을
// 나란히 잰다. 개선이 확인되지 않는 항목은 되돌린다.
//
// 지금 이 상태로 그래프에 넣으면 안 된다 — Declare가 아무 패스도 선언하지
// 않으므로 출력 핸들이 비어 있고, 그것을 읽는 쪽이 깨진다. 호출부를 붙이는
// 것은 트레이스 단계가 돌기 시작한 뒤다.

bool EnhancedSSGIPass::Initialize(const EnhancedFrameContext& context, std::string& outError)
{
    if (nullptr == context.resources || nullptr == context.psoManager ||
        nullptr == context.rootSignatures)
    {
        outError = "SSGI 패스 컨텍스트가 불완전하다";
        return false;
    }

    return CreatePipelines(context, outError);
}

bool EnhancedSSGIPass::CreatePipelines(const EnhancedFrameContext& context, std::string& outError)
{
    // 아직 셰이더를 쓰지 않았다. 여기서 true를 돌려주는 것은 '초기화가
    // 성공했다'가 아니라 '아직 만들 것이 없다'는 뜻이다 — 파이프라인이
    // 생기면 이 주석과 함께 지운다.
    (void)context;
    (void)outError;
    return true;
}

bool EnhancedSSGIPass::PrepareFrame(const EnhancedFrameContext& context, std::string& outError)
{
    (void)outError;

    // GI 해상도는 화면의 1/2. 0이 되지 않게 하한을 둔다 — 창을 아주 작게
    // 줄이면 Dispatch가 0이 되고, 그러면 조용히 아무것도 안 그린다.
    m_giWidth = (std::max)(1u, context.width / kResolutionDivisor);
    m_giHeight = (std::max)(1u, context.height / kResolutionDivisor);

    // 프레임마다 노이즈를 돌리기 위한 인덱스. 시간축이 샘플 수를 대신하려면
    // 프레임마다 다른 방향을 봐야 한다.
    ++m_frameIndex;

    // 재투영에 쓸 지난 프레임 행렬을 갱신한다. 실제 사용은 리졸브 단계에서.
    if (nullptr != context.camera)
    {
        m_previousViewProjection = XMMatrixMultiply(context.camera->view,
            context.camera->projection);
        m_hasPreviousFrame = true;
    }

    return true;
}

void EnhancedSSGIPass::Declare(EnhancedRenderGraph& graph, const EnhancedFrameContext& context)
{
    // 아직 선언할 패스가 없다. 그래프를 건드리지 않는 것이 지금으로선
    // 옳다 — 빈 패스를 넣으면 배리어와 컬링이 그것을 진짜로 취급한다.
    (void)graph;
    (void)context;

    m_output = RGHandle{};
}

void EnhancedSSGIPass::Shutdown()
{
    for (auto& texture : m_history) texture.Reset();

    m_historyValid = false;
    m_hasPreviousFrame = false;
    m_hiZMipCount = 0;
    m_frameIndex = 0;

    m_hiZBuildPSO = nullptr;
    m_tracePSO = nullptr;
    m_resolvePSO = nullptr;
    m_filterPSO = nullptr;
    m_compositePSO = nullptr;
    m_rootSignature = nullptr;
}

#endif
