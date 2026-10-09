#include "ProfilerRenderingDiagnostics.h"

#include <bit>
#include <cmath>
#include <concepts>
#include <utility>

namespace ce::profiler_viewer::diagnostics
{
    namespace rendering_codec
    {
        constexpr std::uint32_t magic = 0x44524c50; // PLRD

        class writer
        {
        public:
            bool valid = true;
            std::vector<std::byte> bytes;

            template <std::unsigned_integral T>
            void field(T value)
            {
                if (!valid || bytes.size() > maximum_rendering_bytes - sizeof(T))
                {
                    valid = false;
                    return;
                }
                for (std::size_t index = 0; index < sizeof(T); ++index)
                {
                    bytes.push_back(static_cast<std::byte>((value >> (index * 8)) & 0xff));
                }
            }

            void field(bool value)
            {
                field(static_cast<std::uint8_t>(value));
            }

            void field(float value)
            {
                valid = valid && std::isfinite(value);
                field(std::bit_cast<std::uint32_t>(value));
            }

            void field(double value)
            {
                valid = valid && std::isfinite(value);
                field(std::bit_cast<std::uint64_t>(value));
            }

            void field(rendering_backend value)
            {
                valid = valid && value <= rendering_backend::vulkan;
                field(static_cast<std::uint8_t>(value));
            }

            void text(const std::string& value, std::size_t maximum)
            {
                if (!valid || value.size() > maximum || value.find('\0') != std::string::npos)
                {
                    valid = false;
                    return;
                }
                field(static_cast<std::uint32_t>(value.size()));
                if (!valid || value.size() > maximum_rendering_bytes - bytes.size())
                {
                    valid = false;
                    return;
                }
                for (unsigned char character : value)
                {
                    bytes.push_back(static_cast<std::byte>(character));
                }
            }

            template <typename T>
            void count(const std::vector<T>& values, std::size_t maximum)
            {
                if (values.size() > maximum)
                {
                    valid = false;
                    return;
                }
                field(static_cast<std::uint32_t>(values.size()));
            }
        };

        class reader
        {
        public:
            explicit reader(std::span<const std::byte> input) : bytes(input) {}
            bool valid = true;
            std::span<const std::byte> bytes;
            std::size_t position{};

            template <std::unsigned_integral T>
            void field(T& value)
            {
                if (!valid || sizeof(T) > bytes.size() - position)
                {
                    valid = false;
                    return;
                }
                value = 0;
                for (std::size_t index = 0; index < sizeof(T); ++index)
                {
                    value |= static_cast<T>(std::to_integer<std::uint8_t>(bytes[position++])) << (index * 8);
                }
            }

            void field(bool& value)
            {
                std::uint8_t encoded{};
                field(encoded);
                valid = valid && encoded <= 1;
                value = encoded != 0;
            }

            void field(float& value)
            {
                std::uint32_t encoded{};
                field(encoded);
                value = std::bit_cast<float>(encoded);
                valid = valid && std::isfinite(value);
            }

            void field(double& value)
            {
                std::uint64_t encoded{};
                field(encoded);
                value = std::bit_cast<double>(encoded);
                valid = valid && std::isfinite(value);
            }

            void field(rendering_backend& value)
            {
                std::uint8_t encoded{};
                field(encoded);
                valid = valid && encoded <= static_cast<std::uint8_t>(rendering_backend::vulkan);
                value = static_cast<rendering_backend>(encoded);
            }

            void text(std::string& value, std::size_t maximum)
            {
                std::uint32_t size{};
                field(size);
                if (!valid || size > maximum || size > bytes.size() - position)
                {
                    valid = false;
                    return;
                }
                value.assign(reinterpret_cast<const char*>(bytes.data() + position), size);
                position += size;
                valid = value.find('\0') == std::string::npos;
            }

            template <typename T>
            void count(std::vector<T>& values, std::size_t maximum)
            {
                std::uint32_t size{};
                field(size);
                if (!valid || size > maximum)
                {
                    valid = false;
                    return;
                }
                values.resize(size);
            }
        };

        template <typename Archive, typename... Values>
        void fields(Archive& archive, Values&... values)
        {
            (archive.field(values), ...);
        }

        template <typename Archive, typename Snapshot>
        void visit(Archive& archive, Snapshot& value, std::uint32_t version)
        {
            fields(archive, value.backend, value.enabled, value.pipelineReady, value.width, value.height,
                value.framesRendered, value.framesIdle, value.framesInFlight, value.consumedFrameId,
                value.drawCount, value.batchCount, value.preparedMeshletBatchCount,
                value.indexedIndirectSupported, value.nonIndexedIndirectSupported,
                value.preparedGpuCandidates, value.preparedGpuCompactedBins,
                value.preparedGpuPreservedBins, value.preparedGpuConservativeCandidates);
            archive.text(value.meshletFallback, maximum_rendering_text_bytes);
            archive.field(value.currentFrameOcclusion);
            archive.text(value.occlusionFallback, maximum_rendering_text_bytes);
            archive.text(value.skinningFallback, maximum_rendering_text_bytes);
            fields(archive, value.decalCount, value.decalBatchCount);
            for (auto& shadow : value.shadow)
            {
                fields(archive, shadow.valid, shadow.hasDirectionalLight, shadow.lightIndex);
                for (auto& direction : shadow.lightDirection)
                {
                    archive.field(direction);
                }
                fields(archive, shadow.shadowDistance, shadow.slopeScale, shadow.casterCandidates,
                    shadow.gpuVisibilityActive, shadow.gpuSubmittedCandidates, shadow.gpuSubmittedBins);
                for (auto& cascade : shadow.cascades)
                {
                    fields(archive, cascade.splitDepth, cascade.radius, cascade.worldTexel,
                        cascade.depthSpan, cascade.constantBias, cascade.graphCasters);
                }
            }
            for (auto& view : value.views)
            {
                fields(archive, view.viewId, view.completedFrameId, view.completedWidth,
                    view.completedHeight, view.ready);
            }
            fields(archive, value.cpuMs, value.gpuMs, value.gpuCollects, value.gpuCollectMismatches,
                value.gpuQueryOverflowPasses, value.lastGpuFrameId, value.lastGpuSubmissionId,
                value.lastGpuViewId);
            const auto provenance = [&](auto& p)
            {
                fields(archive, p.frameKind, p.resolutionState, p.upscaler, p.frameGenerator,
                    p.realFrameId, p.viewId, p.sceneEpoch, p.generatedOrdinal,
                    p.renderWidth, p.renderHeight, p.displayWidth, p.displayHeight, p.nativeGateActive,
                    p.publicationFrameId);
                if (version >= 3)
                {
                    fields(archive, p.spatialProvenanceAvailable, p.spatialMode, p.deepDvcApplied);
                }
                archive.valid = archive.valid && p.frameKind <= 2 &&
                    p.resolutionState <= (version >= 3 ? 4 : 3) &&
                    p.upscaler <= 3 && p.frameGenerator <= 3 &&
                    (p.frameKind == 0 || (p.realFrameId != 0 && p.resolutionState != 0 &&
                        p.renderWidth != 0 && p.renderHeight != 0 &&
                        p.displayWidth != 0 && p.displayHeight != 0)) &&
                    (p.frameKind != 1 || p.generatedOrdinal == 0) &&
                    (p.frameKind != 2 || (p.generatedOrdinal != 0 && p.frameGenerator != 0)) &&
                    p.spatialMode <= 2 &&
                    (p.spatialProvenanceAvailable || (p.spatialMode == 0 && !p.deepDvcApplied)) &&
                    (p.resolutionState != 4 || (p.spatialProvenanceAvailable && p.spatialMode == 1 &&
                        p.upscaler == 0 && p.renderWidth <= p.displayWidth && p.renderHeight <= p.displayHeight)) &&
                    (p.spatialMode != 1 || p.resolutionState == 4);
            };
            if (version >= 2)
            {
                provenance(value.temporalProvenance);
                provenance(value.gpuTemporalProvenance);
            }
            archive.text(value.lastGpuCollectError, maximum_rendering_text_bytes);
            archive.field(value.graveyardCount);
            archive.text(value.lastError, maximum_rendering_text_bytes);
            archive.count(value.passTimings, maximum_rendering_passes);
            if (!archive.valid)
            {
                return;
            }
            for (auto& timing : value.passTimings)
            {
                archive.text(timing.name, maximum_rendering_name_bytes);
                archive.field(timing.milliseconds);
                archive.valid = archive.valid && timing.milliseconds >= 0.0;
            }
            archive.count(value.validationMessages, maximum_rendering_messages);
            if (!archive.valid)
            {
                return;
            }
            for (auto& message : value.validationMessages)
            {
                archive.text(message, maximum_rendering_message_bytes);
            }
            fields(archive, value.omittedPassTimings, value.omittedValidationMessages, value.textTruncated);
        }
    }

    std::vector<std::byte> encode_rendering(const rendering_snapshot& snapshot)
    {
        rendering_codec::writer writer;
        writer.bytes.reserve(4096);
        writer.field(rendering_codec::magic);
        writer.field(rendering_schema_version);
        rendering_codec::visit(writer, snapshot, rendering_schema_version);
        if (!writer.valid)
        {
            return {};
        }
        return std::move(writer.bytes);
    }

    bool decode_rendering(std::span<const std::byte> bytes, rendering_snapshot& snapshot)
    {
        if (bytes.size() > maximum_rendering_bytes)
        {
            return false;
        }
        rendering_codec::reader reader(bytes);
        std::uint32_t magic{};
        std::uint32_t version{};
        reader.field(magic);
        reader.field(version);
        if (!reader.valid || magic != rendering_codec::magic || version == 0 || version > rendering_schema_version)
        {
            return false;
        }
        rendering_snapshot candidate;
        rendering_codec::visit(reader, candidate, version);
        if (!reader.valid || reader.position != bytes.size())
        {
            return false;
        }
        snapshot = std::move(candidate);
        return true;
    }
}
