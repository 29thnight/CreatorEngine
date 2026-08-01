#pragma once
#ifndef DYNAMICCPP_EXPORTS
#include <cstdint>
#include <string>
#include <vector>
#include <functional>
#include <wrl/client.h>
#include <d3d12.h>

// DX12 실행 경로용 렌더 그래프 (PHASE 3-5).
//
// 기존 RenderGraphBuilder를 쓰지 않는 이유:
//   - 싱글턴이다. 그래프는 카메라별·프레임별로 있어야 하는데 프로세스에 하나뿐이면
//     씬 뷰와 게임 뷰가 같은 그래프를 공유하게 된다.
//   - 배리어 유도가 없다. 위상 정렬까지만 있고, DX12에서 그래프를 두는 이유의
//     절반이 배리어 자동화인데 그 절반이 비어 있다.
//   - 리소스 타입이 Texture*와 uint32 format 자리표시자다.
//   - 참조하는 코드가 0곳이다.
// 3-6에서 패스를 새로 쓰기로 한 것과 같은 이유다 — 껍데기를 맞추는 데 드는 노력이
// 새로 쓰는 것보다 크고, 결과도 더 나쁘다.
//
// 이 그래프가 하는 일은 넷이다:
//   ① 선언이 데이터 흐름과 어긋나는지 검증한다 — 아무도 쓰지 않은 리소스를 읽는 패스
//   ② 리소스 상태를 추적해 전이 배리어를 자동으로 만든다 — 패스 앞에 모아서 한 번에
//      넣는다. 호출마다 배리어를 붙이면 파이프라인이 끊긴다.
//   ③ 결과에 기여하지 않는 패스를 걷어낸다
//   ④ transient 리소스의 수명을 알려 준다(힙 앨리어싱의 재료 — 실제 앨리어싱은
//      실전 예산이 보이는 3-6에서 붙인다)
//
// 하지 않는 일: 패스 재정렬.
//
// 처음에는 위상 정렬로 순서를 유도하게 짰다가 자가 검증에서 뒤집었다. 순수
// 데이터 흐름만으로는 순서가 정해지지 않는다 — 한 리소스에 두 패스가 쓰면 둘 중
// 무엇이 먼저인지 알 방법이 없고 결국 선언 순서로 되돌아온다. 그러면 정렬은
// 선언 순서를 다시 만들어 내는 일이 되고, 어쩌다 뒤집히면 프레임이 실행마다
// 달라져 픽셀 대조가 흔들린다. 실행 순서 = 선언 순서(컬링된 것만 빠짐)가 계약이다.
//
// 배리어를 사람이 붙이지 않는 것이 요점이다. DX11은 드라이버가 해 주던 일이라
// 손으로 옮기면 빠뜨리기 쉽고, 빠뜨린 배리어는 '가끔 이상하게 보인다'로만 드러난다.

// 그래프가 아는 리소스 상태. D3D12 상태의 부분집합이되, 그래프가 판단해야 하는
// 구분만 남긴다 — 상태를 다 노출하면 호출부가 배리어를 손으로 짜는 것과 같아진다.
enum class RGResourceState
{
    Common,
    RenderTarget,
    DepthWrite,
    DepthRead,
    ShaderResource,   // 픽셀 셰이더에서 읽기
    UnorderedAccess,
    CopySource,
    CopyDest,
};

struct RGHandle
{
    uint16_t index{ 0xFFFF };
    static constexpr uint16_t kInvalid = 0xFFFF;
    bool IsValid() const { return kInvalid != index; }
};

struct RGPassId
{
    uint16_t index{ 0xFFFF };
    static constexpr uint16_t kInvalid = 0xFFFF;
    bool IsValid() const { return kInvalid != index; }
};

// 그래프가 만들 리소스의 설명. transient(그래프 소유)만 이 설명을 쓴다.
struct RGTextureDesc
{
    uint32_t    width{ 0 };
    uint32_t    height{ 0 };
    DXGI_FORMAT format{ DXGI_FORMAT_R8G8B8A8_UNORM };
    bool        allowRenderTarget{ false };
    bool        allowDepthStencil{ false };
    bool        allowUnorderedAccess{ false };
    std::string name;
};

class EnhancedRenderGraph
{
public:
    // 패스가 기록할 때 받는 것. 커맨드 리스트는 호출부가 준 것을 그대로 넘긴다 —
    // 그래프는 순서와 배리어를 정할 뿐 기록 방식을 정하지 않는다(3-6에서 패스별
    // 커맨드 리스트를 워커가 병렬로 기록하게 될 때 이 경계가 필요하다).
    struct ExecuteContext
    {
        ID3D12GraphicsCommandList* commandList{ nullptr };
        const EnhancedRenderGraph* graph{ nullptr };

        // 선언한 리소스의 실제 D3D12 객체. transient는 그래프가 만든 것.
        ID3D12Resource* Resolve(RGHandle handle) const;
    };

    using ExecuteCallback = std::function<void(const ExecuteContext&)>;

    struct Stats
    {
        uint32_t passesDeclared{ 0 };
        uint32_t passesCulled{ 0 };
        uint32_t passesExecuted{ 0 };
        uint32_t barriersEmitted{ 0 };
        uint32_t barrierBatches{ 0 };   // 배리어를 몇 번에 나눠 넣었는가 — 적을수록 좋다
        uint32_t transientCreated{ 0 };
    };

    void Reset();

    // 외부 리소스를 그래프에 들인다. 현재 상태를 알려 줘야 첫 전이를 맞게 만든다.
    RGHandle ImportTexture(ID3D12Resource* resource, RGResourceState currentState,
        const std::string& name);

    // 그래프가 소유할 리소스를 선언한다. 실제 생성은 Compile에서 한다 —
    // 컬링으로 사라진 패스만 쓰던 리소스는 만들지 않기 위해서다.
    RGHandle CreateTexture(const RGTextureDesc& desc);

    // 패스 선언. usages는 (핸들, 그 패스가 요구하는 상태) 목록이다.
    //
    // hasSideEffect는 컬링에서 뿌리가 되는 표시다. 화면에 내보내거나 외부가
    // 읽는 리소스를 쓰는 패스는 결과가 그래프 밖으로 나가므로 걷어내면 안 된다.
    // 기본값을 false로 두는 이유는, 뿌리를 명시하지 않으면 전부 살아남아
    // 컬링이 무의미해지기 때문이다.
    struct RGPassUsage
    {
        RGHandle        handle;
        RGResourceState state{ RGResourceState::Common };
    };

    RGPassId AddPass(const std::string& name, const std::vector<RGPassUsage>& usages,
        ExecuteCallback execute, bool hasSideEffect = false);

    // 순서 유도 → 컬링 → 배리어 계획. 실패 사유는 문자열로.
    bool Compile(ID3D12Device* device, std::string& outError);

    // Compile이 정한 순서대로 배리어를 넣고 패스를 기록한다.
    bool Execute(ID3D12GraphicsCommandList* commandList, std::string& outError);

    Stats GetStats() const { return m_stats; }

    // 검증·진단용. Compile 뒤에 유효하다.
    const std::vector<uint16_t>& GetExecuteOrder() const { return m_executeOrder; }
    bool IsPassCulled(RGPassId pass) const;
    uint32_t GetPassBarrierCount(RGPassId pass) const;

    // transient 리소스의 수명(첫 사용 패스 ~ 마지막 사용 패스, 실행 순서 기준).
    // 힙 앨리어싱의 재료다 — 수명이 겹치지 않는 둘은 같은 메모리를 쓸 수 있다.
    bool GetTransientLifetime(RGHandle handle, uint32_t& outFirst, uint32_t& outLast) const;

    static D3D12_RESOURCE_STATES ToD3D12(RGResourceState state);

private:
    template <typename T> using ComPtr = Microsoft::WRL::ComPtr<T>;

    struct Resource
    {
        RGTextureDesc   desc;
        ID3D12Resource* external{ nullptr };
        Microsoft::WRL::ComPtr<ID3D12Resource> owned;   // transient
        RGResourceState state{ RGResourceState::Common };
        bool imported{ false };
        bool used{ false };          // 살아남은 패스가 쓰는가 — 아니면 만들지 않는다
        uint32_t firstUse{ 0xFFFFFFFF };
        uint32_t lastUse{ 0 };
        std::string name;
    };

    struct Pass
    {
        std::string              name;
        std::vector<RGPassUsage> usages;
        ExecuteCallback          execute;
        bool                     hasSideEffect{ false };
        bool                     culled{ false };
        std::vector<D3D12_RESOURCE_BARRIER> barriers;   // 이 패스 직전에 한 번에 넣는다
    };

    bool BuildOrder(std::string& outError);
    void CullPasses();
    bool CreateTransients(ID3D12Device* device, std::string& outError);
    void PlanBarriers();

    std::vector<Resource> m_resources;
    std::vector<Pass>     m_passes;
    std::vector<uint16_t> m_executeOrder;
    bool  m_compiled{ false };
    Stats m_stats;
};

#endif
