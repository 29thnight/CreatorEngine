#pragma once
#ifndef DYNAMICCPP_EXPORTS
#include <unordered_map>
#include <wrl/client.h>

#include "EnhancedRenderPass.h"
#include "DX12MeshCache.h"

// 그림자 패스 (PHASE 3-6, 세 번째 패스).
//
// 방향광 하나에 대한 단일 캐스케이드로 시작한다. DX11 쪽은 3단 캐스케이드지만
// 한 번에 거기까지 가면 검증할 것이 너무 많아진다 — 이 슬라이스에서 뚫어야 하는
// 것은 넷이고, 캐스케이드는 그 위에 얹는 확장이다:
//   ① 깊이 전용 렌더(픽셀 셰이더도 렌더 타깃도 없는 PSO)
//   ② 라이트 공간 행렬 — 카메라 프러스텀을 감싸는 정사영 상자
//   ③ 그림자 맵을 다음 패스에서 SRV로 읽기(깊이 → 셰이더 리소스 전이)
//   ④ 비교 샘플러(하드웨어 PCF)
//
// 라이트 공간 행렬은 카메라 스냅샷만으로 만든다. 씬 경계를 쓰면 오브젝트가
// 하나 늘 때마다 그림자 해상도가 출렁이고, 카메라 기준이면 화면에 보이는 곳에
// 해상도가 집중된다.
class EnhancedShadowPass : public EnhancedRenderPass
{
public:
    static constexpr uint32_t   kShadowMapSize = 2048;
    static constexpr DXGI_FORMAT kShadowFormat = DXGI_FORMAT_D32_FLOAT;

    const char* GetName() const override { return "Shadow"; }

    bool Initialize(const EnhancedFrameContext& context, std::string& outError) override;
    bool PrepareFrame(const EnhancedFrameContext& context, std::string& outError) override;
    void Declare(EnhancedRenderGraph& graph, const EnhancedFrameContext& context) override;
    void Shutdown() override;

    RGHandle GetShadowMap() const { return m_shadowMap; }

    // Deferred가 월드 위치를 그림자 맵 좌표로 옮길 때 쓴다.
    const Mathf::Matrix& GetLightViewProjection() const { return m_lightViewProjection; }

    // 그림자를 드리울 방향광을 찾았는가. 없으면 그림자 없이 도는 것이 정상이다.
    bool HasDirectionalLight() const { return m_hasDirectionalLight; }

    uint32_t GetLastDrawCount() const { return m_lastDrawCount; }

private:
    template <typename T> using ComPtr = Microsoft::WRL::ComPtr<T>;

    struct ShadowConstants
    {
        Mathf::Matrix lightViewProjection{};
        Mathf::Matrix world{};
    };

    bool CreatePipeline(const EnhancedFrameContext& context, std::string& outError);
    void ComputeLightMatrix(const EnhancedFrameContext& context);

    RGHandle m_shadowMap;

    std::unordered_map<Mesh*, DX12MeshCache::Entry> m_drawGeometry;
    Mathf::Matrix m_lightViewProjection{};
    Mathf::Vector4 m_lightDirection{};
    bool m_hasDirectionalLight{ false };
    uint32_t m_lastDrawCount{ 0 };

    ComPtr<ID3D12DescriptorHeap> m_dsvHeap;

    ID3D12PipelineState* m_pso{ nullptr };
    ID3D12RootSignature* m_rootSignature{ nullptr };
};

#endif
