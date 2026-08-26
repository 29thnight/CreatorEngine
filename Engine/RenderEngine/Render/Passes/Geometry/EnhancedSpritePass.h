#pragma once
#include "../../../RHI/RHIFormat.h"
#include <cstdint>
#include <vector>

#include "../../Graph/EnhancedRenderPass.h"

class Texture;

// SpriteRenderer와 3D Canvas 이미지를 함께 그리는 RHI 공용 월드 쿼드 패스.
class EnhancedSpritePass : public EnhancedRenderPass
{
public:
    static constexpr RHIFormat kOutputFormat = RHIFormat::RGBA16Float;

    struct Item
    {
        // 로컬 [-.5,.5] XY 쿼드를 월드로 옮기는 행렬.
        math::matrix4x4 world{ math::matrix4x4::identity() };
        Mathf::Vector4 uv{ 0.f, 0.f, 1.f, 1.f };
        Mathf::Color4 color{ 1.f, 1.f, 1.f, 1.f };
        Texture* texture{ nullptr };
        int canvasOrder{ 0 };
        int layerOrder{ 0 };
        bool enableDepth{ false };
    };

    static_assert(std::is_same_v<decltype(Item::world), math::matrix4x4>);

    struct Inputs
    {
        RGHandle color;
        RGHandle depth;
    };

    const char* GetName() const override { return "Sprite"; }
    bool Initialize(const EnhancedFrameContext& context, std::string& outError) override;
    bool PrepareFrame(const EnhancedFrameContext& context, std::string& outError) override;
    void Declare(EnhancedRenderGraph& graph, const EnhancedFrameContext& context) override;
    void Shutdown() override;

    void SetOutputFormat(RHIFormat format) { m_outputFormat = format; }
    void SetInputs(const Inputs& inputs) { m_inputs = inputs; }
    void SetItems(const std::vector<Item>* items) { m_items = items; }
    RGHandle GetOutput() const { return m_output; }
    uint32_t GetLastItemCount() const { return m_lastItemCount; }
    uint32_t GetLastBatchCount() const { return m_lastBatchCount; }

private:
    bool CreatePipelines(const EnhancedFrameContext& context, std::string& outError);

    struct Instance
    {
        math::matrix4x4 world{};
        Mathf::Vector4 uv{};
        Mathf::Color4 color{};
    };

    static_assert(sizeof(Instance) == 96);
    static_assert(std::is_trivially_copyable_v<Instance>);

    struct Batch
    {
        uint32_t first{ 0 };
        uint32_t count{ 0 };
        Texture* texture{ nullptr };
        bool enableDepth{ false };
        RHITextureEntry uploaded;
    };

    Inputs m_inputs{};
    RHIFormat m_outputFormat{ kOutputFormat };
    RGHandle m_output;
    const std::vector<Item>* m_items{ nullptr };
    std::vector<Instance> m_instances;
    std::vector<Batch> m_batches;
    math::matrix4x4 m_viewProjection{ math::matrix4x4::identity() };
    static_assert(std::is_same_v<decltype(m_viewProjection), math::matrix4x4>);
    uint32_t m_width{ 0 };
    uint32_t m_height{ 0 };
    uint32_t m_lastItemCount{ 0 };
    uint32_t m_lastBatchCount{ 0 };
    RHIPipelineHandle m_depthPso;
    RHIPipelineHandle m_overlayPso;
};
