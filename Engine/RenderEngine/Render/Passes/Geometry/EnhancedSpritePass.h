#pragma once
#include <mathematics/vector4.hpp>
#include "../../../RHI/RHIFormat.h"
#include <cstdint>
#include <mathematics/color.hpp>
#include <memory>
#include <vector>

#include "../../Graph/EnhancedRenderPass.h"
#include "../../../GpuGeometryVisibility.h"

class Texture;

// SpriteRenderer와 3D Canvas 이미지·SDF 글리프를 함께 그리는 RHI 공용 월드 쿼드 패스.
class EnhancedSpritePass : public EnhancedRenderPass
{
public:
    static constexpr RHIFormat kOutputFormat = RHIFormat::RGBA16Float;

    struct Item
    {
        // 로컬 [-.5,.5] XY 쿼드를 월드로 옮기는 행렬.
        math::matrix4x4 world{ math::matrix4x4::identity() };
        math::vector4 uv{ 0.f, 0.f, 1.f, 1.f };
        math::color   color{ 1.f, 1.f, 1.f, 1.f };
        const Texture* texture{ nullptr };
        int canvasOrder{ 0 };
        int layerOrder{ 0 };
        bool enableDepth{ false };
        bool signedDistance{ false };
        std::size_t texturePinIndex{ TextureFramePins::InvalidIndex };
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
    // Called after the upload prefix, under the backend shader-output scope.
    bool PrepareGpuVisibility(const EnhancedFrameContext& context, std::string& outError);
    GpuGeometryVisibility::PreparedStats GetGpuVisibilityStats() const
    {
        return m_visibilityFrame ? m_visibilityFrame->GetPreparedStats() : GpuGeometryVisibility::PreparedStats{};
    }

    void Declare(EnhancedRenderGraph& graph, const EnhancedFrameContext& context) override;
    void Shutdown() override;

    void SetOutputFormat(RHIFormat format) { m_outputFormat = format; }
    void SetInputs(const Inputs& inputs) { m_inputs = inputs; }
    void SetItems(const std::vector<Item>* items,
        own::shared_owner<TextureFramePins> texturePins = {})
    {
        m_items = items;
        m_texturePins = std::move(texturePins);
    }
    RGHandle GetOutput() const { return m_output; }
    uint32_t GetLastItemCount() const { return m_lastItemCount; }
    uint32_t GetLastBatchCount() const { return m_lastBatchCount; }

private:
    GpuGeometryVisibility m_visibility;
    std::shared_ptr<const GpuGeometryVisibility::Frame> m_visibilityFrame;
    bool m_gpuVisibilityEnabled{ false };
    std::vector<math::vector4> m_visibilitySpheres;

    bool CreatePipelines(const EnhancedFrameContext& context, std::string& outError);

    struct Instance
    {
        math::matrix4x4 world{};
        math::vector4 uv{};
        math::color   color{};
        math::vector4 sampling{}; // x = signed-distance flag; WorldSprite.slang과 동일
    };

    static_assert(sizeof(Instance) == 112u);
    static_assert(offsetof(Instance, color) == 80u);
    static_assert(offsetof(Instance, sampling) == 96u);
    static_assert(std::is_same_v<decltype(Instance::color), math::color>);
    static_assert(std::is_trivially_copyable_v<Instance>);

    struct Batch
    {
        uint32_t first{ 0 };
        uint32_t count{ 0 };
        const Texture* texture{ nullptr };
        bool enableDepth{ false };
        std::size_t texturePinIndex{ TextureFramePins::InvalidIndex };
        std::uint64_t textureId{};
        RHITextureEntry uploaded;
    };

    Inputs m_inputs{};
    RHIFormat m_outputFormat{ kOutputFormat };
    RGHandle m_output;
    const std::vector<Item>* m_items{ nullptr };
    own::shared_owner<TextureFramePins> m_texturePins;
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
