#pragma once
#ifndef DYNAMICCPP_EXPORTS
#include <cstdint>
#include <vector>
#include <wrl/client.h>

#include "EnhancedRenderPass.h"
#include "EnhancedGBufferPass.h"

// 데칼 패스 (PHASE 3-6, 미구현 패스 이식 2차).
//
// ── 기존 DX11 DecalPass가 하는 일 ──
//
//   데칼 하나가 단위 상자(-0.5~+0.5) 하나다. 그 상자를 그리면서 화면 깊이로
//   월드 좌표를 복원하고, 그 좌표가 상자 안이면 GBuffer의 확산·노멀·ORM에
//   덧칠한다. 상자 밖이면 버린다. 지오메트리를 다시 그리지 않고 표면에
//   무늬를 얹는 방식이다.
//
//   ★ 읽으면서 쓸 수 없으므로 깊이·확산·노멀·ORM 넷을 통째로 복사한다
//     (화면 크기 CopyResource 4회). 셰이더는 사본을 읽고 원본에 블렌드한다.
//
//   ★ 데칼마다 상수 버퍼 둘을 갱신하고, 블렌드 상태를 갈아 끼우고, SRV 셋을
//     다시 물리고, 정점·인덱스 버퍼를 다시 걸고, 36인덱스를 그린다. 데칼
//     100개면 상태 변경 800번에 드로우 100번이다.
//
// ── 다시 쓰면서 바꾸는 것 ──
//
//   깊이 복사가 사라진다. D3D12는 읽기 전용 DSV와 SRV를 동시에 허용하므로
//   (RGResourceState::DepthReadShaderResource) 깊이는 복사 없이 테스트하면서
//   읽는다. 넷 중 하나가 빠져 셋이 된다 — 나머지 셋은 정말로 읽으면서 쓰는
//   대상이라 사본이 필요하다.
//
//   정점·인덱스 버퍼가 사라진다. 상자는 고정 24정점·36인덱스라 셰이더 안의
//   상수 표를 SV_VertexID로 짚으면 된다(스카이박스와 같은 처리). 원본의
//   정점 배열과 인덱스 배열을 그대로 옮겨 두므로 감김 방향이 보존된다.
//
//   연속한 같은 묶음을 인스턴싱한다. 텍스처 셋이 같은 데칼이 이어 나오면
//   한 번의 DrawInstanced로 묶는다.
//
//   ★ 정렬하지 않고 '연속한 것만' 묶는 이유: 데칼은 블렌드한다. 겹친 둘의
//     순서가 바뀌면 결과가 달라지므로, 배치를 위해 큐를 정렬하면 그림이
//     바뀐다. 순서를 지키는 대신 이득을 줄이는 쪽을 골랐다.
//
// ── 원본에서 발견한 것 셋 ──
//
//   옮기면서 블렌드 상태와 셰이더 출력 알파를 나란히 놓고 계산해 보니 셋이
//   나왔다. 전부 그대로 보존한다 — 기준선이 DX11이고, 여기서 고치면 픽셀
//   대조가 성립하지 않는다. 자가 검증이 셋을 각각 측정해 근거로 남긴다.
//
//   ① 확산의 알파가 제곱된다.
//      셰이더가 lerp(base, decal, a)를 하고, 블렌드가 SRC_ALPHA/INV_SRC_ALPHA로
//      한 번 더 섞는다. 정리하면 lerp(base, decal, a²)다.
//
//   ② 노멀은 아무 일도 하지 않는다.
//      노멀 출력의 알파가 0인데(float4(nWS*0.5+0.5, 0)) 블렌드가 SRC_ALPHA다.
//      즉 src*0 + dest*1 = dest. 노멀 데칼은 켜도 화면이 바뀌지 않는다.
//
//   ③ ORM은 데칼의 불투명도가 아니라 표면의 IOR로 섞인다.
//      ORM 출력의 알파가 baseORM.a인데 GBuffer의 그 채널은 IOR이다
//      (GBuffer.ps.hlsl: float4(metallic, roughness, occlusion, ior)).
//      IOR 기본값이 1.5라 블렌드 계수가 1을 넘어 보간이 아니라 외삽이 된다.
class EnhancedDecalPass : public EnhancedRenderPass
{
public:
    /// 데칼 하나. 프레임 밀봉된 값만 든다(3-2) — 프록시를 그대로 들지 않는다.
    struct Item
    {
        Mathf::Matrix worldMatrix{};

        // 셋 다 null일 수 있으나 하나는 있어야 한다. 어느 것이 있느냐가
        // 곧 어느 채널을 켤지를 정한다(DX11의 g_useFlags와 같은 규칙).
        Texture* diffuse{ nullptr };
        Texture* normal{ nullptr };
        Texture* occRoughMetal{ nullptr };

        // 아틀라스 분할. sliceNum이 몇 번째 칸을 쓸지 고른다.
        uint32_t sliceX{ 1 };
        uint32_t sliceY{ 1 };
        int32_t  sliceNum{ 0 };
    };

    /// 채널 조합. DX11 DecalChannel과 같은 비트값이다.
    enum Channel : uint32_t
    {
        kChannelNone = 0,
        kChannelDiffuse = 1u << 0,
        kChannelNormal = 1u << 1,
        kChannelOrm = 1u << 2,
        kChannelCount = 8,   // 조합 수(0~7)
    };

    const char* GetName() const override { return "Decal"; }

    bool Initialize(const EnhancedFrameContext& context, std::string& outError) override;
    bool PrepareFrame(const EnhancedFrameContext& context, std::string& outError) override;
    void Declare(EnhancedRenderGraph& graph, const EnhancedFrameContext& context) override;
    void Shutdown() override;

    /// 덧칠 대상. GBuffer 출력을 그대로 받는다 — 데칼은 새 타깃을 만들지 않고
    /// 이미 그려진 GBuffer에 얹는다.
    void SetInputs(const EnhancedGBufferPass::Outputs& inputs) { m_inputs = inputs; }

    /// 이번 프레임에 그릴 데칼. PrepareFrame이 텍스처를 올리고 배치를 짠다.
    void SetDecals(const std::vector<Item>& decals) { m_decals = decals; }

    /// 이 패스를 컬링 뿌리로 표시할지. 소비자(Deferred)가 붙기 전에는 켜 둔다.
    void SetKeepAlive(bool keepAlive) { m_keepAlive = keepAlive; }

    /// 그린 데칼 수와 실제 발행한 드로우 수.
    ///
    /// 둘이 늘 같으면 인스턴싱이 죽은 것이다. 다만 정렬을 하지 않으므로
    /// 같은 텍스처가 흩어져 들어오면 정상적으로도 같아진다 — 그래서 둘을
    /// 따로 내보낸다. 그림만 봐서는 배치가 묶였는지 알 길이 없다.
    uint32_t GetLastDecalCount() const { return m_lastDecalCount; }
    uint32_t GetLastBatchCount() const { return m_lastBatchCount; }

private:
    template <typename T> using ComPtr = Microsoft::WRL::ComPtr<T>;

    bool CreatePipelines(const EnhancedFrameContext& context, std::string& outError);

    /// 셰이더가 읽는 데칼 하나. HLSL의 StructuredBuffer 원소와 배치가 같아야
    /// 한다 — 어긋나면 값이 조용히 밀려 '데칼 위치가 이상하다'로만 드러난다.
    struct InstanceData
    {
        Mathf::Matrix world{};          // 상자를 놓는다(정점 셰이더)
        Mathf::Matrix inverseWorld{};   // 월드 좌표를 상자 안으로 되돌린다(픽셀)
        uint32_t      useFlags{ 0 };
        uint32_t      sliceX{ 1 };
        uint32_t      sliceY{ 1 };
        int32_t       sliceNum{ 0 };
    };

    /// 한 번의 DrawInstanced로 그릴 묶음. 텍스처 셋이 같고 큐에서 연속한 것들.
    struct Batch
    {
        uint32_t channel{ 0 };
        uint32_t firstInstance{ 0 };
        uint32_t instanceCount{ 0 };

        // 묶음의 정체성은 올라간 자원이 아니라 원본 포인터로 판단한다.
        // 업로드가 실패하거나 아직 안 된 텍스처도 서로 같고 다름은 정해지고,
        // 그래야 배칭이 업로드 성공 여부에 흔들리지 않는다.
        Texture*        sources[3]{};    // 확산·노멀·ORM
        ID3D12Resource* textures[3]{};
        DXGI_FORMAT     formats[3]{};
        uint32_t        mipLevels[3]{};
    };

    EnhancedGBufferPass::Outputs m_inputs{};
    std::vector<Item>            m_decals;
    bool                         m_keepAlive{ false };

    // PrepareFrame이 채우고 Record가 읽는다.
    std::vector<InstanceData> m_instances;
    std::vector<Batch>        m_batches;

    // 사본 셋. 정말로 읽으면서 쓰는 대상이라 사본이 필요하다.
    RGHandle m_copiedDiffuse;
    RGHandle m_copiedNormal;
    RGHandle m_copiedOrm;

    // 프레임 밀봉 값(3-2). Record가 스냅샷을 다시 읽지 않는다.
    Mathf::Matrix m_inverseView{};
    Mathf::Matrix m_inverseProjection{};

    uint32_t m_width{ 0 };
    uint32_t m_height{ 0 };
    uint32_t m_lastDecalCount{ 0 };
    uint32_t m_lastBatchCount{ 0 };

    ComPtr<ID3D12DescriptorHeap> m_rtvHeap;   // 확산·노멀·ORM 셋
    ComPtr<ID3D12DescriptorHeap> m_dsvHeap;   // 읽기 전용 깊이 하나
    uint32_t                     m_rtvIncrement{ 0 };

    // 채널 조합마다 하나. 조합이 곧 어느 타깃에 쓸지라 PSO가 갈린다.
    ID3D12PipelineState* m_pipelines[kChannelCount]{};
    ID3D12RootSignature* m_rootSignature{ nullptr };
};

#endif
