#pragma once
#ifndef DYNAMICCPP_EXPORTS
#include "VulkanLoader.h"
#include "VulkanEncoder.h"
#include "VulkanPipelineCache.h"

#include "../RHIFormat.h"

#include <string>

class VulkanDeviceResources;

// 삼각형 패스 — Vulkan (V8-a).
//
// ══ 이 파일은 계측기다. A 가 끝나면 지운다 ══
//
// ★ **패스가 백엔드마다 있는 것은 틀린 구조다.** §7.4 완료 조건 6이
//   "Vulkan 백엔드가 **같은 패스 코드로** 삼각형 하나를 그린다"이고, 패스가
//   둘이면 그것이 곧 실패 상태다. 중립 패스 하나가 소비 시점에 백엔드로
//   컨버트되는 것이 목적지다 — V1~V6 이 '기술'에 대해 이미 그렇게 했고
//   (포맷 21필드 중 20이 그대로 건너간다), 남은 것은 '객체'뿐이다.
//
// ★ 그런데 지금은 그 하나를 만들 수가 없다. 패스 경로가 DX12 **객체**를
//   들고 있어서다(EnhancedGridPass 실측):
//
//     ID3D12PipelineState*  m_pso                  멤버가 DX12 객체
//     ID3D12RootSignature*  m_rootSignature        멤버가 DX12 객체
//     rootSignatures->GetOrCreate(...)             반환형이 DX12RootSignatureEntry
//     psoManager->GetOrCreate(DX12GraphicsPipelineDesc)  인자·반환형이 DX12
//     encoder.SetPipeline(..., m_pso, m_rootSignature)   인자가 DX12 객체
//     encoder.SetConstantBuffer(..., cb.gpuAddress)      D3D12_GPU_VIRTUAL_ADDRESS
//     resources->GetUploadRing()                   반환형이 DX12UploadRing&
//
//   컨버트는 **값**에만 성립한다. ToVulkan(RHIFormat)은 쓸 수 있어도
//   ToVulkan(ID3D12PipelineState*)는 쓸 수 없다 — 소비 시점에 컨버트하려면
//   그 전까지 중립이어야 하는데 위 일곱 자리가 중립이 아니다.
//
// ★ **폐기 조건**: A(객체 핸들화)가 위 일곱 자리를 핸들로 갈면, 그리드
//   패스가 두 백엔드에서 그대로 돌아야 한다. 그때 이 파일과
//   VulkanFrameContext 를 지운다. **A 가 끝났는데 이 파일이 남아 있으면
//   A 가 실패한 것이다.**
//
// ── 무엇을 재는가 ──
//
// §7.2.2 의 골격은 삼각형을 **검사 파일 안에서** vkCmd* 로 직접 그렸다.
// 그것을 `EnhancedGridPass` 와 같은 모양으로 옮긴다 — 지금 실재하는 가장
// 단순한 그래픽 패스이고, 하는 일이 넷이다:
//
//   ① 레이아웃을 캐시에서 받는다        rootSignatures->GetOrCreate
//   ② 파이프라인을 캐시에서 받아 **멤버로 든다**   psoManager->GetOrCreate
//   ③ 인코더에만 커맨드를 적는다         RHIEncoder
//   ④ 상수를 슬롯에 건다                 encoder.SetConstantBuffer
//
// ★ **넷 다 형태가 같고 타입만 갈린다.** 그것이 이 파일의 산출물이다.
//   갈리는 자리마다 주석을 달았고, 세면 A(객체 핸들화)의 크기가 나온다.
//
// ── 왜 그래프를 안 타는가 ──
//
// `EnhancedRenderGraph` 는 `RHI/DX12/` 에 있고 `d3d12.h` 를 문다. 패스의
// `Declare`/`Record` 분리는 배리어 유도와 병렬 기록을 위한 것이라
// (`EnhancedRenderPass.h`) 골격이 흉내 내면 흉내만 남는다. 그래서 여기는
// `Record` 안쪽만 재고, `Declare` 자리는 비워 둔다.

/// 한 프레임의 렌더 입력과 도구. `EnhancedFrameContext` 의 자리다.
///
/// ★ 필드가 다섯에서 둘로 줄었다. `IRenderDeviceServices` ·
///   `IRenderMeshCache` · `IRenderTextureCache` 는 서명이 DX12 라 Vulkan 이
///   구현할 수 없다(§7.2.2) — 없어서 없는 것이 아니라 **못 넣어서** 없다.
struct VulkanFrameContext
{
    VulkanDeviceResources* resources{ nullptr };
    VulkanPipelineCache*   pipelineCache{ nullptr };

    uint32_t width{ 0 };
    uint32_t height{ 0 };
};

class VulkanTrianglePass
{
public:
    ~VulkanTrianglePass() { Shutdown(); }

    const char* GetName() const { return "Vulkan.Triangle"; }

    /// 한 번만 부른다. 파이프라인·레이아웃·정적 리소스를 준비한다 —
    /// `EnhancedRenderPass::Initialize` 와 같은 계약이다.
    bool Initialize(const VulkanFrameContext& context, std::string& outError);

    /// 프레임마다, 기록보다 먼저. 이번 프레임의 상수를 올린다.
    bool PrepareFrame(const VulkanFrameContext& context, std::string& outError);

    /// 그래프가 정한 시점에 커맨드를 적는다. 그래프가 없으므로 검사가 직접
    /// 부른다 — 안쪽은 `EnhancedGridPass` 의 실행 람다와 같은 모양이다.
    void Record(VulkanEncoder& encoder, const VulkanRenderTargetBinding& targets);

    void Shutdown();

    /// 검사가 판정 줄에 남기는 값. 파이프라인이 캐시에서 나왔는지 새로
    /// 구웠는지를 본다.
    VulkanPipelineCache::Stats GetCacheStats() const { return m_cacheStats; }

    void SetOutputFormat(RHIFormat format) { m_outputFormat = format; }

    /// 셰이더가 곱하는 색. 자가 검증이 이 값이 실제로 닿았는지 픽셀로 잰다.
    void SetTint(float r, float g, float b, float a);

    /// 음성 대조용. 끄면 텍스처 디스크립터를 안 건다 — 위 `m_bindTexture` 참고.
    void SetBindTexture(bool bind) { m_bindTexture = bind; }

    /// 체커보드 한 칸의 픽셀 수. 자가 검증이 표본 두 점을 이 값으로 고른다.
    static constexpr uint32_t kTextureSize = 8;
    static constexpr uint32_t kCheckerCell = 4;

private:
    // HLSL 쪽 cbuffer 와 정확히 같은 배치.
    struct TriangleConstants
    {
        float tint[4]{ 1.f, 1.f, 1.f, 1.f };
    };

    bool CreatePipelines(const VulkanFrameContext& context, std::string& outError);
    bool CreateConstantBuffer(const VulkanFrameContext& context, std::string& outError);

    /// 체커보드 텍스처를 만들어 올린다 (V8-b).
    ///
    /// ★ DX12 패스는 이 자리가 `textureCache->GetOrUpload(texture, error)` 한
    ///   줄이다. `IRenderTextureCache` 의 반환형이 `DX12TextureEntry` 라
    ///   Vulkan 이 구현할 수 없어서(§7.2.2) 여기서 손으로 한다 — 스테이징 버퍼 ·
    ///   레이아웃 전이 두 번 · 복사 · 일회성 커맨드 제출.
    ///
    ///   **그 길이가 곧 A-4 의 청구서다.** 자산 캐시 둘이 중립이 되지 않으면
    ///   패스마다 이만큼이 백엔드별로 복제된다.
    bool CreateTexture(const VulkanFrameContext& context, std::string& outError);

    VkDevice m_device{ VK_NULL_HANDLE };

    // ★ 여기가 수명이 갈리는 자리다. DX12 패스는 이 자리에
    //   `ID3D12PipelineState*` 와 `ID3D12RootSignature*` 를 **원시 포인터로**
    //   든다(EnhancedGridPass::m_pso · m_rootSignature). 참조 계수가 있어
    //   캐시가 살아 있는 한 안전하고, 패스가 놓을 때 하는 일이 nullptr 대입
    //   하나다.
    //
    //   Vulkan 은 계수가 없다. 이 둘은 캐시가 소유하고 캐시만 파괴할 수 있으며,
    //   그 파괴는 **GPU 가 안 쓰고 있을 때**여야 한다. 즉 패스가 든 것은
    //   '포인터'가 아니라 **'캐시가 살아 있다는 전제 아래의 이름'**이다.
    //   A 의 핸들이 표를 통해 풀려야 하고 파괴를 미룰 수 있어야 하는 이유가
    //   이것이다 — 원시 포인터를 uint32_t 로 바꾸는 것으로는 모자란다.
    /// ★ A-1 이후로 DX12 패스와 **같은 타입**이다. V8-a 때 여기 적어 둔
    ///   "같은 코드가 다른 뜻이다"(포인터 vs 이름)가 없어졌다 — 양쪽 다
    ///   표를 거치는 핸들이고, 수명 규약도 같다(캐시가 살아 있는 동안).
    RHIPipelineHandle       m_pipeline;
    RHIPipelineLayoutHandle m_layout;

    // ★ 상수 버퍼를 패스가 직접 만든다. DX12 패스는
    //   `resources->GetUploadRing().Allocate(...)` 로 프레임마다 잘라 쓰는데,
    //   그것이 돌려주는 것이 `D3D12_GPU_VIRTUAL_ADDRESS` 라 Vulkan 이 쓸 수
    //   없다(§7.2.5 예상 7). 디스크립터 셋도 같은 이유로 `DX12DescriptorRing`
    //   대신 여기 작은 풀을 둔다.
    //
    //   **여기 있는 것이 옳아서가 아니라 갈 곳이 없어서다.** 그 자리를
    //   만드는 것이 A 다음의 몫이다.
    VkBuffer         m_constantBuffer{ VK_NULL_HANDLE };
    VkDeviceMemory   m_constantMemory{ VK_NULL_HANDLE };
    void*            m_constantMapped{ nullptr };
    VkDescriptorPool m_descriptorPool{ VK_NULL_HANDLE };
    VkDescriptorSet  m_descriptorSet{ VK_NULL_HANDLE };

    // 체커보드 텍스처 (V8-b). 셋 셋(이미지·메모리·뷰)이 한 덩어리다 —
    // §7.2.1 이 "Vulkan 에서는 VkImage + VkDeviceMemory + VkImageView 가 한
    // 덩어리라 포인터 하나로 못 가리킨다" 며 핸들을 정한 근거가 이 세 줄이다.
    VkImage        m_texture{ VK_NULL_HANDLE };
    VkDeviceMemory m_textureMemory{ VK_NULL_HANDLE };
    VkImageView    m_textureView{ VK_NULL_HANDLE };

    /// 텍스처를 디스크립터에 거는가. 자가 검증이 **음성 대조**로 이것을 끈다 —
    /// 안 걸어도 삼각형은 그려지므로, 끄고 재 보지 않으면 이 경로가 죽어도
    /// 판정이 통과한다(V8-a 가 상수 버퍼에서 같은 것을 확인했다).
    bool m_bindTexture{ true };

    RHIFormat m_outputFormat{ RHIFormat::RGBA8Unorm };
    uint32_t  m_width{ 0 };
    uint32_t  m_height{ 0 };

    TriangleConstants m_constants{};

    VulkanPipelineCache::Stats m_cacheStats{};
};

#endif
