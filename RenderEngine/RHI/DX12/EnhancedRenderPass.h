#pragma once
#ifndef DYNAMICCPP_EXPORTS
#include <cstdint>
#include <string>

#include "EnhancedRenderGraph.h"

class DX12DeviceResources;
class DX12PSOManager;
class DX12RootSignatureCache;

// EnhancedRenderPass — DX12 렌더 패스의 기반 (PHASE 3-6).
//
// 기존 IRenderPass를 상속하지 않는다. 그쪽은 CreateRenderCommandList(context, scene, camera)
// 하나로 '이 패스가 지금 다 그린다'를 전제하는데, 그래프 위에서는 선언과 기록이 나뉜다:
//   Declare  — 무슨 리소스를 어떤 상태로 쓸지 그래프에 알린다(기록하지 않는다)
//   Record   — 그래프가 정한 시점에 커맨드를 기록한다(배리어는 그래프가 이미 넣었다)
//
// 이 분리가 필요한 이유는 둘이다. 배리어를 그래프가 유도하려면 기록 전에 사용 계획을
// 알아야 하고(3-5), 기록을 워커가 병렬로 돌리려면 기록이 순수해야 한다 —
// 기록 중에 리소스를 만들거나 상태를 바꾸면 병렬화가 성립하지 않는다.
//
// 그래서 규약을 셋 둔다:
//   ① Declare에서 리소스를 만들거나 커맨드를 기록하지 않는다
//   ② Record에서 리소스를 만들거나 배리어를 넣지 않는다
//   ③ Record는 넘겨받은 커맨드 리스트에만 기록한다(전역 상태를 만지지 않는다)

// 한 프레임의 렌더 입력과 도구. 패스는 여기 있는 것만 쓴다 —
// 전역 DeviceStates를 만지지 않는 것이 3-6의 규약이다.
struct EnhancedFrameContext
{
    DX12DeviceResources*    resources{ nullptr };
    DX12PSOManager*         psoManager{ nullptr };
    DX12RootSignatureCache* rootSignatures{ nullptr };

    uint32_t width{ 0 };
    uint32_t height{ 0 };

    // 씬 입력(카메라 스냅샷·드로우 목록)은 씬 연결 슬라이스에서 붙인다.
    // 지금 넣어 두면 검증할 수 없는 서명이 쌓인다 — 3-1에서 배운 것과 같다.
};

class EnhancedRenderPass
{
public:
    virtual ~EnhancedRenderPass() = default;

    virtual const char* GetName() const = 0;

    /// 한 번만 부른다. PSO·루트 시그니처·정적 리소스를 준비한다.
    virtual bool Initialize(const EnhancedFrameContext& context, std::string& outError) = 0;

    /// 프레임마다 부른다. 그래프에 리소스 사용과 실행을 등록한다.
    /// 여기서 리소스를 만들거나 커맨드를 기록하면 안 된다.
    virtual void Declare(EnhancedRenderGraph& graph, const EnhancedFrameContext& context) = 0;

    virtual void Shutdown() {}
};

#endif
