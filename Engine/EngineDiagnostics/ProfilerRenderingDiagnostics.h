#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

// Value-only Rendering - Live boundary. This header deliberately has no renderer,
// scene, RHI, editor, or ImGui dependency and contains no process-local handles.
namespace ce::profiler_viewer::diagnostics
{
    inline constexpr std::uint32_t rendering_schema_version = 2;
    inline constexpr std::size_t maximum_rendering_bytes = 256 * 1024;
    inline constexpr std::size_t maximum_rendering_passes = 512;
    inline constexpr std::size_t maximum_rendering_messages = 128;
    inline constexpr std::size_t maximum_rendering_name_bytes = 128;
    inline constexpr std::size_t maximum_rendering_message_bytes = 1024;
    inline constexpr std::size_t maximum_rendering_text_bytes = 2048;
    inline constexpr std::size_t rendering_view_count = 3;

    enum class rendering_backend : std::uint8_t
    {
        dx12,
        vulkan,
    };

    enum class rendering_command : std::uint32_t
    {
        open_render_pass = 1,
    };

    struct rendering_pass_timing
    {
        std::string name;
        double milliseconds{};
    };

    struct rendering_shadow_cascade
    {
        float splitDepth{};
        float radius{};
        float worldTexel{};
        float depthSpan{};
        float constantBias{};
        std::uint32_t graphCasters{};
    };

    struct rendering_shadow_stats
    {
        bool valid{};
        bool hasDirectionalLight{};
        std::uint32_t lightIndex{};
        std::array<float, 3> lightDirection{};
        float shadowDistance{};
        float slopeScale{};
        std::uint32_t casterCandidates{};
        bool gpuVisibilityActive{};
        std::uint64_t gpuSubmittedCandidates{};
        std::uint64_t gpuSubmittedBins{};
        std::array<rendering_shadow_cascade, 3> cascades{};
    };

    struct rendering_display
    {
        std::uint64_t viewId{};
        std::uint64_t completedFrameId{};
        std::uint32_t completedWidth{};
        std::uint32_t completedHeight{};
        bool ready{};
    };

    // Numeric values are the versioned wire contract: kind 0 unknown/1 real/2
    // generated, resolution 0 unknown/1 native/2 reconstructed/3 native fallback.
    struct rendering_temporal_provenance
    {
        std::uint8_t frameKind{}, resolutionState{}, upscaler{}, frameGenerator{};
        std::uint64_t realFrameId{}, viewId{}, sceneEpoch{};
        std::uint32_t generatedOrdinal{}, renderWidth{}, renderHeight{}, displayWidth{}, displayHeight{};
        bool nativeGateActive{};
        std::uint64_t publicationFrameId{};
    };

    struct rendering_snapshot
    {
        rendering_backend backend{};
        bool enabled{};
        bool pipelineReady{};
        std::uint32_t width{};
        std::uint32_t height{};
        std::uint64_t framesRendered{};
        std::uint64_t framesIdle{};
        std::uint64_t framesInFlight{};
        std::uint64_t consumedFrameId{};
        std::uint32_t drawCount{};
        std::uint32_t batchCount{};
        std::uint32_t preparedMeshletBatchCount{};
        bool indexedIndirectSupported{};
        bool nonIndexedIndirectSupported{};
        std::uint64_t preparedGpuCandidates{};
        std::uint64_t preparedGpuCompactedBins{};
        std::uint64_t preparedGpuPreservedBins{};
        std::uint64_t preparedGpuConservativeCandidates{};
        std::string meshletFallback;
        bool currentFrameOcclusion{};
        std::string occlusionFallback;
        std::string skinningFallback;
        std::uint32_t decalCount{};
        std::uint32_t decalBatchCount{};
        std::array<rendering_shadow_stats, rendering_view_count> shadow{};
        std::array<rendering_display, rendering_view_count> views{};
        double cpuMs{};
        double gpuMs{};
        rendering_temporal_provenance temporalProvenance, gpuTemporalProvenance;
        std::uint64_t gpuCollects{};
        std::uint64_t gpuCollectMismatches{};
        std::uint64_t gpuQueryOverflowPasses{};
        std::uint64_t lastGpuFrameId{};
        std::uint64_t lastGpuSubmissionId{};
        std::uint64_t lastGpuViewId{};
        std::string lastGpuCollectError;
        std::uint64_t graveyardCount{};
        std::string lastError;
        std::vector<rendering_pass_timing> passTimings;
        std::vector<std::string> validationMessages;
        std::uint64_t omittedPassTimings{};
        std::uint64_t omittedValidationMessages{};
        bool textTruncated{};
    };

    // Explicit little-endian fields, bounded strings/counts, finite floats, exact
    // consumption, and schema validation. Failed decode never changes output.
    std::vector<std::byte> encode_rendering(const rendering_snapshot& snapshot);
    bool decode_rendering(std::span<const std::byte> bytes, rendering_snapshot& snapshot);
}
