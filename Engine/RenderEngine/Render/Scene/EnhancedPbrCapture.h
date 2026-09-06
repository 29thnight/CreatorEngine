#pragma once

#include "EnhancedSceneRenderer.h"
#include "../Core/EnhancedLivePipelineDesc.h"
#include "../Passes/Geometry/EnhancedGBufferPass.h"
#include "../../Texture.h"
#include <AuthoringRymlErrorPolicy.h>
#include <ryml/ryml.hpp>
#include <ryml/ryml_std.hpp>
#include <c4/yml/emit.hpp>
#include <filesystem>
#include <fstream>
#include <cmath>
#include <limits>
#include <cstring>

// Explicit diagnostic capture only. The render thread owns this object and its
// readbacks. Release must run after submission completion (or frame abort).
struct EnhancedPbrCapture
{
    EnhancedLivePbrCaptureStatus result;
    EnhancedLiveDisplayTarget target{ EnhancedLiveDisplayTarget::Game };
    uint64_t afterFrameId{};
    ryml::Tree manifest;
    std::array<RHIReadback, 7> readbacks{};

    void Begin(const EnhancedLiveFramePacket& frame, const EnhancedLiveViewPacket& view,
        EnhancedLiveBackend backend, std::span<const EnhancedDrawItem> opaque,
        std::span<const EnhancedDrawItem> transparent,
        std::span<const EnhancedLight> lights, const std::string& skyBoxPath)
    {
        result.state = EnhancedPbrCaptureState::Recording;
        result.frameId = frame.frameId;
        Authoring::EnsureRymlErrorPolicy();
        auto root = manifest.rootref();
        root |= ryml::MAP;
        root["schemaVersion"] << 1;
        root["source"] << "product-live";
        root["backend"] << (backend == EnhancedLiveBackend::DX12 ? "dx12" : "vulkan");
        root["frameId"] << frame.frameId;
        root["requestedAfterFrameId"] << afterFrameId;
        root["sceneEpoch"] << frame.sceneEpoch;
        root["viewId"] << view.key.viewId;
        root["historyRevision"] << view.key.historyRevision;
        root["width"] << frame.width;
        root["height"] << frame.height;
        root["totalSeconds"] << frame.totalSeconds;
        root["deltaSeconds"] << frame.deltaSeconds;
        const auto matrix = [](ryml::NodeRef node, const math::matrix4x4& value)
        {
            std::array<float, 16> values;
            static_assert(sizeof(value) == sizeof(values));
            std::memcpy(values.data(), &value, sizeof(value));
            node |= ryml::SEQ;
            for (float component : values) node.append_child() << component;
        };
        root["camera"] |= ryml::MAP;
        matrix(root["camera"]["view"], view.camera.view);
        matrix(root["camera"]["projection"], view.camera.projection);
        root["skyBoxPath"] << skyBoxPath;
        root["lights"] |= ryml::SEQ;
        for (const auto& light : lights)
        {
            // EnhancedLight has an asserted packed 16-float layout.
            std::array<float, 16> values;
            std::memcpy(values.data(), &light, sizeof(light));
            auto node = root["lights"].append_child();
            node |= ryml::SEQ;
            for (float component : values) node.append_child() << component;
        }
        root["draws"] |= ryml::SEQ;
        const auto append = [&](const EnhancedDrawItem& draw, const auto& material,
            const char* route)
        {
            auto item = root["draws"].append_child();
            item |= ryml::MAP;
            item["route"] << route;
            matrix(item["world"], draw.worldMatrix);
            item["modelId"] << FileGuid(draw.modelMeshView.handle.modelId).ToString();
            item["meshId"] << FileGuid(draw.modelMeshView.handle.meshId).ToString();
            item["modelGeneration"] << draw.modelMeshView.handle.generation;
            if (material)
            {
                item["shaderMetaSlot"] << material->shaderMetaHandle.slot;
                item["shaderMetaGeneration"] << material->shaderMetaHandle.generation;
                item["permutation"] << material->permutationKey.Hex();
                item["propertyBytes"] |= ryml::SEQ;
                for (auto byte : material->propertyBytes)
                    item["propertyBytes"].append_child() << static_cast<uint32_t>(byte);
                item["useNormalMap"] << material->useNormalMap;
                item["coverageFlags"] << material->coverage.flags;
                item["alphaCutoff"] << material->coverage.cutoff;
                item["textures"] |= ryml::SEQ;
                for (const auto& texture : material->textureBindings)
                {
                    auto binding = item["textures"].append_child();
                    binding |= ryml::MAP;
                    binding["property"] << texture.propertyName;
                    binding["assetId"] << texture.textureGuid.ToString();
                    binding["register"] << texture.registerIndex;
                    binding["space"] << texture.registerSpace;
                    binding["runtimeIdentity"] << (texture.textureOwner
                        ? texture.textureOwner->m_assetId.m_ID_Data : 0);
                    binding["authored"] << (texture.textureOwner ? "true" : "false");
                }
            }
            else item["missing"] << "material snapshot";
        };
        for (const auto& draw : opaque) append(draw, draw.materialSnapshot, "gbuffer");
        for (const auto& draw : transparent) append(draw, draw.forwardMaterialSnapshot, "forward");
        // These contracts are not represented by the current draw snapshot.
        root["missing"] |= ryml::SEQ;
        for (const char* missing : { "sampler identity", "descriptor generation", "resolved PSO key" })
            root["missing"].append_child() << missing;
    }

    bool Declare(IRenderDeviceServices& resources, EnhancedRenderGraph& graph,
        const LiveBlackboard& blackboard, uint32_t width, uint32_t height,
        std::string& error)
    {
        constexpr const char* slots[] = { LiveSlots::kGBufferDiffuse,
            LiveSlots::kGBufferMetalRough, LiveSlots::kGBufferNormal,
            LiveSlots::kGBufferEmissive, LiveSlots::kGBufferDepth,
            LiveSlots::kLitColor, LiveSlots::kDisplayLdr };
        for (uint32_t i = 0; i < readbacks.size(); ++i)
        {
            const auto handle = blackboard.Get(slots[i]);
            if (!handle.IsValid()) { error = std::string("missing capture output: ") + slots[i]; return false; }
            const auto format = i < 4 ? EnhancedGBufferPass::GetRenderTargetFormat(i)
                : i == 4 ? RHIFormat::D32Float
                : i == 5 ? RHIFormat::RGBA16Float : RHIFormat::RGBA8Unorm;
            if (!resources.CreateReadback(width, height, format, 1, readbacks[i], error))
                return false;
            const auto readback = readbacks[i];
            graph.AddPass(std::string("PBR.Capture.") + slots[i],
                { { handle, RHIResourceState::CopySource } },
                [handle, readback](const EnhancedRenderGraph::ExecuteContext& context)
                { context.encoder->CopyToReadback(readback, context.ResolveHandle(handle)); }, true);
        }
        return true;
    }

    bool Save(IRenderDeviceServices& resources, const EnhancedRenderGraph::Stats& stats,
        std::string& error, uint32_t validationCount, const std::string& validation)
    {
        try
        {
            const std::filesystem::path root(result.directory);
            constexpr const char* names[] = { "baseColor", "metalRough", "normal",
                "emissive", "depth", "preToneHdr", "display" };
            auto rootNode = manifest.rootref();
            rootNode["validationCount"] << validationCount;
            rootNode["validation"] << validation;
            rootNode["attachments"] |= ryml::SEQ;
            bool finite = true;
            for (uint32_t i = 0; i < readbacks.size(); ++i)
            {
                RHIReadbackImage image;
                if (!resources.MapReadback(readbacks[i], image, error)) return false;
                const uint32_t channels = i == 4 ? 1 : 4;
                std::vector<float> pixels;
                pixels.reserve(static_cast<size_t>(image.width) * image.height * channels);
                float minimum = std::numeric_limits<float>::max();
                float maximum = std::numeric_limits<float>::lowest();
                float rgbMaximum = std::numeric_limits<float>::lowest();
                uint64_t nonfinite = 0;
                for (uint32_t y = 0; y < image.height; ++y)
                    for (uint32_t x = 0; x < image.width; ++x)
                        for (uint32_t c = 0; c < channels; ++c)
                        {
                            const float value = image.At(x, y, c);
                            pixels.push_back(value);
                            if (!std::isfinite(value)) { ++nonfinite; continue; }
                            minimum = (std::min)(minimum, value);
                            maximum = (std::max)(maximum, value);
                            if (c < 3) rgbMaximum = (std::max)(rgbMaximum, value);
                        }
                const std::string file = std::string(names[i]) + ".f32";
                std::ofstream output(root / file, std::ios::binary | std::ios::trunc);
                output.write(reinterpret_cast<const char*>(pixels.data()),
                    static_cast<std::streamsize>(pixels.size() * sizeof(float)));
                output.close();
                if (!output) { error = "capture write failed: " + file; return false; }
                auto attachment = rootNode["attachments"].append_child();
                attachment |= ryml::MAP;
                attachment["name"] << names[i];
                attachment["file"] << file;
                attachment["encoding"] << "float32-le-row-major";
                attachment["channels"] << channels;
                attachment["width"] << image.width;
                attachment["height"] << image.height;
                attachment["nonfinite"] << nonfinite;
                attachment["min"] << minimum;
                attachment["max"] << maximum;
                attachment["rgbMax"] << rgbMaximum;
                finite &= nonfinite == 0;
            }
            auto graphNode = rootNode["graph"];
            graphNode |= ryml::MAP;
            graphNode["declared"] << stats.passesDeclared;
            graphNode["culled"] << stats.passesCulled;
            graphNode["executed"] << stats.passesExecuted;
            graphNode["barriers"] << stats.barriersEmitted;
            rootNode["finite"] << (finite ? "true" : "false");
            std::ofstream output(root / "manifest.json", std::ios::trunc);
            output << ryml::emitrs_json<std::string>(manifest) << '\n';
            output.close();
            if (!output) { error = "capture manifest write failed"; return false; }
            if (!finite) { error = "capture contains nonfinite pixels"; return false; }
            if (validationCount != 0) { error = "capture contains GPU validation messages: " + validation; return false; }
            result.state = EnhancedPbrCaptureState::Complete;
            return true;
        }
        catch (const std::exception& exception) { error = exception.what(); return false; }
    }

    void Release(IRenderDeviceServices& resources)
    {
        for (auto& readback : readbacks) resources.ReleaseReadback(readback);
    }
    void Fail(const std::string& error)
    {
        result.state = EnhancedPbrCaptureState::Failed;
        result.error = error;
    }
};
