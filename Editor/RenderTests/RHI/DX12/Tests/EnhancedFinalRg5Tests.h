#pragma once

#include "Render/Passes/Lighting/EnhancedVolumetricFogPass.h"
#include "Render/Passes/Editor/EnhancedGridPass.h"
#include "Render/Passes/Editor/EnhancedWireFramePass.h"
#include "Render/Passes/Editor/EnhancedGizmoIconPass.h"
#include "Render/Passes/Editor/EnhancedGizmoLinePass.h"
#include <stdexcept>

namespace
{
    bool ValidateRg5FinalGpu(DX12DeviceResources& resources, std::string& error)
    {
        constexpr uint32_t size = 64;
        constexpr uint32_t fixtures = 43;
        DX12PSOManager pso;
        DX12RootSignatureCache roots;
        DX12TextureCache textures;
        DX12MeshCache meshes;
        EnhancedFrameContext frame{};
        frame.resources = &resources;
        frame.psoManager = &pso;
        frame.rootSignatures = &roots;
        frame.textureCache = &textures;
        frame.meshCache = &meshes;
        frame.width = frame.height = size;
        FrameCameraSnapshot camera{};
        camera.eyePosition = {0.f, 2.f, -4.f};
        camera.view = math::look_at_lh(camera.eyePosition, math::vector3{0.f, 0.f, 3.f}, math::vector3{0.f, 1.f, 0.f});
        camera.projection = math::perspective_fov_lh(math::quarter_pi, 1.f, 0.5f, 1000.f);
        camera.inverseView = math::inverse(camera.view);
        camera.inverseProjection = math::inverse(camera.projection);
        camera.nearPlane = 0.5f;
        camera.farPlane = 1000.f;
        frame.camera = &camera;
        std::vector<EnhancedLight> lights(1);
        lights[0].position = {0.f, 0.f, 0.f, 0.f};
        lights[0].direction = {0.f, -1.f, 0.f, 0.f};
        lights[0].color = {1.f, 1.f, 1.f, 1.f};
        lights[0].attenuation = {1.f, 0.f, 0.f, 100.f};
        frame.lights = &lights;
        EnhancedVolumetricFogPass fog;
        EnhancedPostChainPass post;
        EnhancedUIPass ui;
        EnhancedGridPass grid;
        EnhancedWireFramePass wire;
        EnhancedGizmoIconPass icon;
        EnhancedGizmoLinePass line;
        ui.SetOutputFormat(RHIFormat::RGBA8Unorm);
        grid.SetOutputFormat(RHIFormat::RGBA8Unorm);
        wire.SetOutputFormat(RHIFormat::RGBA8Unorm);
        icon.SetOutputFormat(RHIFormat::RGBA8Unorm);
        line.SetOutputFormat(RHIFormat::RGBA8Unorm);
        struct Cleanup
        {
            std::function<void()> run;
            ~Cleanup() { run(); }
        } cleanup{[&] {
            resources.AbortFrame();
            resources.WaitForGpu();
            fog.Shutdown();
            post.Shutdown();
            ui.Shutdown();
            grid.Shutdown();
            wire.Shutdown();
            icon.Shutdown();
            line.Shutdown();
            meshes.Shutdown();
            textures.Shutdown();
            pso.Shutdown();
            roots.Shutdown();
        }};
        const auto require = [](bool condition, const std::string& message) {
            if (!condition)
            {
                throw std::runtime_error(message);
            }
        };
        try
        {
            require(pso.Initialize(&resources, L"dx12_rg5_final.cache", error) && roots.Initialize(&resources, error) &&
                    textures.Initialize(&resources, error) && meshes.Initialize(&resources, error), error);
            require(fog.Initialize(frame, error) && post.Initialize(frame, error) && ui.Initialize(frame, error) &&
                    grid.Initialize(frame, error) && wire.Initialize(frame, error) && icon.Initialize(frame, error) &&
                    line.Initialize(frame, error), error);
            fog.SetShadowMatrix(math::orthographic_lh(2000.f, 2000.f, 0.f, 2000.f));
            EnhancedVolumetricFogPass::CloudShadow cloud{};
            cloud.viewProjection = math::matrix4x4::identity();
            cloud.alpha = 1.f;
            cloud.size[0] = cloud.size[1] = 1.f;
            cloud.cloudMapSize[0] = cloud.cloudMapSize[1] = 4.f;
            fog.SetCloudShadow(cloud);
            const uint32_t white = 0xffffffff;
            own::shared_owner<const Texture> texture(Texture::CreateFromPixels(1, 1, "RG5.Final.White", RHIFormat::RGBA8Unorm, &white));
            require(texture != nullptr, "Final fixture texture");
            std::vector<Vertex> vertices(4);
            vertices[0].position = {-1.f, 0.f, 2.f};
            vertices[1].position = {1.f, 0.f, 2.f};
            vertices[2].position = {1.f, 0.f, 4.f};
            vertices[3].position = {-1.f, 0.f, 4.f};
            Mesh quad("RG5.Final.Quad", vertices, {0, 1, 2, 0, 2, 3});
            std::vector<EnhancedDrawItem> draws(1);
            draws[0].mesh = &quad;
            draws[0].worldMatrix = math::matrix4x4::identity();
            std::vector<EnhancedUIPass::Rect> rects(2);
            rects[0].left = 4.f;
            rects[0].top = 4.f;
            rects[0].right = 24.f;
            rects[0].bottom = 24.f;
            rects[0].color = {1.f, 0.f, 0.f, 0.5f};
            rects[0].texture = (texture ? &*texture.borrow() : nullptr);
            rects[1] = rects[0];
            rects[1].left = 28.f;
            rects[1].right = 48.f;
            std::vector<EnhancedGizmoIconPass::Icon> icons(2);
            icons[0].position = {0.f, 1.f, 3.f};
            icons[0].size = 0.7f;
            icons[0].texture = (texture ? &*texture.borrow() : nullptr);
            icons[1] = icons[0];
            icons[1].position.x = 1.f;
            const std::vector<EnhancedDrawItem> emptyDraws;
            const std::vector<EnhancedUIPass::Rect> emptyRects;
            const std::vector<EnhancedGizmoIconPass::Icon> emptyIcons;
            std::array<std::vector<float>, fixtures> references;
            std::array<uint32_t, 7> contributions{};
            std::vector<float> historyPixels;
            for (uint32_t policy = 0; policy < 3; ++policy)
            {
                for (uint32_t fixture = 0; fixture < fixtures; ++fixture)
                {
                    require(resources.BeginFrame(error), error);
                    Cleanup recording{[&] { resources.AbortFrame(); }};
                    textures.BeginFrame(policy * fixtures + fixture);
                    const bool history = fixture >= 40;
                    const bool enableFog = fixture < 32 ? (fixture & 1) != 0 : fixture == 36 || history;
                    const bool enableUi = fixture < 32 ? (fixture & 2) != 0 : fixture == 38;
                    const uint32_t editorMask = fixture < 32 ? ((fixture & 4) != 0 ? 15u : 0u) :
                                                fixture < 36 ? 1u << (fixture - 32) : fixture == 39 ? 15u : 0u;
                    if (!history || fixture == 40 || fixture == 42)
                    {
                        fog.ResetHistory();
                    }
                    lights.resize(history && fixture != 40 ? 0u : 1u);
                    if (!lights.empty())
                    {
                        lights[0].position = {0.f, 0.f, 0.f, 0.f};
                        lights[0].direction = {0.f, -1.f, 0.f, 0.f};
                        lights[0].color = {1.f, 1.f, 1.f, 1.f};
                        lights[0].attenuation = {1.f, 0.f, 0.f, 100.f};
                    }
                    auto tuning = fog.GetTuning();
                    tuning.previousFrameBlendFactor = history && fixture == 41 ? 1.f : 0.f;
                    fog.SetTuning(tuning);
                    fog.SetEnabled(enableFog);
                    fog.SetFrameIndex(0);
                    auto postTuning = post.GetTuning();
                    postTuning.bloomEnabled = fixture < 32 && (fixture & 16) != 0;
                    postTuning.fxaaEnabled = postTuning.bloomEnabled;
                    postTuning.toneMapEnabled = true;
                    postTuning.vignetteEnabled = true;
                    postTuning.gradingEnabled = true;
                    post.SetTuning(postTuning);
                    post.SetUseSeparatePasses(fixture < 32 && (fixture & 8) != 0);
                    frame.draws = editorMask & 2 ? &draws : &emptyDraws;
                    ui.SetRects(enableUi ? &rects : &emptyRects);
                    icon.SetIcons(editorMask & 4 ? &icons : &emptyIcons);
                    line.ResetLines();
                    if (editorMask & 8)
                    {
                        line.AddLine({-1.f, 1.f, 2.f}, {1.f, 1.f, 2.f}, {0.f, 1.f, 0.f, 1.f});
                    }
                    require(fog.PrepareFrame(frame, error) && post.PrepareFrame(frame, error) && ui.PrepareFrame(frame, error) &&
                            grid.PrepareFrame(frame, error) && wire.PrepareFrame(frame, error) && icon.PrepareFrame(frame, error) &&
                            line.PrepareFrame(frame, error), error);
                    EnhancedRenderGraph graph(resources,
                        policy == 0 ? RGSchedulingMode::DeclarationOrder : RGSchedulingMode::ExplicitVersioned,
                        policy == 1 ? RGOrderPolicy::PreserveDeclarationOrder : RGOrderPolicy::DependencyOrder);
                    const auto read = policy == 0 ? RGAccessMode::LegacyState : RGAccessMode::Read;
                    const auto write = policy == 0 ? RGAccessMode::LegacyState : RGAccessMode::Write;
                    const auto clear = [&](const char* name, RHIFormat format, std::array<float, 4> value, uint32_t layers = 1) {
                        RGTextureDesc desc{};
                        desc.width = desc.height = size;
                        desc.arraySize = layers;
                        desc.format = format;
                        desc.allowRenderTarget = true;
                        desc.name = name;
                        std::copy(value.begin(), value.end(), desc.clearColor);
                        auto handle = graph.CreateTexture(desc);
                        if (policy != 0)
                        {
                            handle = graph.Write(handle);
                        }
                        graph.AddPass(name, {{handle, RHIResourceState::RenderTarget, write}},
                            [&, handle, value, layers, format](const EnhancedRenderGraph::ExecuteContext& ec) {
                                for (uint32_t layer = 0; layer < layers; ++layer)
                                {
                                    const RHIColorTargetDesc colors[]{RHIColorTargetDesc::Slice(ec.ResolveHandle(handle), format, 0, layer)};
                                    const auto targets = resources.CreateRenderTargets(colors);
                                    ec.encoder->ClearRenderTargets(targets, value.data());
                                }
                            });
                        return handle;
                    };
                    auto color = clear("Final.Hdr", RHIFormat::RGBA16Float, {2.f, 0.25f, 0.125f, 1.f});
                    const auto fogDepth = clear("Final.FogDepth", RHIFormat::R32Float, {0.99f, 0.f, 0.f, 0.f});
                    const auto shadow = clear("Final.Shadow", RHIFormat::R32Float, {1.f, 0.f, 0.f, 0.f}, 3);
                    const auto cloudMap = clear("Final.Cloud", RHIFormat::RGBA16Float, {1.f, 1.f, 1.f, 1.f});
                    const auto noise = clear("Final.Noise", RHIFormat::R32Float, {0.5f, 0.f, 0.f, 0.f});
                    struct Readbacks
                    {
                        DX12DeviceResources& resources;
                        std::vector<RHIReadback> handles;
                        ~Readbacks()
                        {
                            for (auto& handle : handles)
                            {
                                resources.ReleaseReadback(handle);
                            }
                        }
                    } readbacks{resources};
                    std::vector<RGHandle> stages;
                    std::vector<RHIFormat> formats;
                    const auto capture = [&](RGHandle handle, RHIFormat format) {
                        RHIReadback readback;
                        require(handle.IsValid() && resources.CreateReadback(size, size, format, 1, readback, error), error);
                        readbacks.handles.push_back(readback);
                        stages.push_back(handle);
                        formats.push_back(format);
                        graph.AddPass("Final.StageCapture", {{handle, RHIResourceState::CopySource, read}},
                            [handle, readback](const EnhancedRenderGraph::ExecuteContext& ec) {
                                ec.encoder->CopyToReadback(readback, ec.ResolveHandle(handle));
                            }, true);
                    };
                    capture(color, RHIFormat::RGBA16Float);
                    fog.SetInputs({color, fogDepth, shadow, fixture == 36 ? RGHandle{} : cloudMap, noise});
                    fog.Declare(graph, frame);
                    color = fog.GetOutput();
                    capture(color, RHIFormat::RGBA16Float);
                    post.SetInputs({fixture == 37 ? RGHandle{} : color});
                    post.Declare(graph, frame);
                    require(fixture != 37 || !post.GetOutput().IsValid(), "Missing PostChain input accepted");
                    color = fixture == 37 ? clear("Final.Fallback", RHIFormat::RGBA8Unorm, {0.25f, 0.25f, 0.25f, 1.f}) : post.GetOutput();
                    capture(color, RHIFormat::RGBA8Unorm);
                    ui.SetInputs({fixture == 38 ? RGHandle{} : color});
                    ui.Declare(graph, frame);
                    color = ui.GetOutput();
                    capture(color, RHIFormat::RGBA8Unorm);
                    RGHandle depth;
                    const auto beforeEditor = color;
                    if (editorMask & 1)
                    {
                        grid.SetInputs({fixture == 39 ? RGHandle{} : color, {}});
                        grid.Declare(graph, frame);
                        color = grid.GetOutput();
                        depth = grid.GetDepth();
                    }
                    capture(color, RHIFormat::RGBA8Unorm);
                    if (editorMask & 2)
                    {
                        wire.SetInputs({color, depth});
                        wire.Declare(graph, frame);
                        color = wire.GetOutput();
                        depth = wire.GetDepth();
                    }
                    capture(color, RHIFormat::RGBA8Unorm);
                    if (editorMask & 4)
                    {
                        icon.SetInputs({color});
                        icon.Declare(graph, frame);
                        color = icon.GetOutput();
                    }
                    capture(color, RHIFormat::RGBA8Unorm);
                    if (editorMask & 8)
                    {
                        line.SetInputs({color});
                        line.Declare(graph, frame);
                        color = line.GetOutput();
                    }
                    capture(color, RHIFormat::RGBA8Unorm);
                    require(policy == 0 || editorMask == 0 || fixture == 39 || color.version > beforeEditor.version,
                            "Final output did not advance through Editor modifiers");
                    RHITextureDesc displayDesc{};
                    displayDesc.width = displayDesc.height = size;
                    displayDesc.format = RHIFormat::RGBA8Unorm;
                    RHITextureHandle displayTexture;
                    require(resources.CreateTexture(displayDesc, displayTexture, error), error);
                    Cleanup displayLifetime{[&] { resources.ReleaseTexture(displayTexture); }};
                    auto displayState = RHIResourceState::Common;
                    auto display = graph.ImportTexture(displayTexture, displayState, "Final.PresentCopy", &displayState);
                    if (policy != 0)
                    {
                        display = graph.Write(display);
                    }
                    graph.AddPass("Final.Present", {{color, RHIResourceState::CopySource, read}, {display, RHIResourceState::CopyDest, write}},
                        [color, display](const EnhancedRenderGraph::ExecuteContext& ec) {
                            ec.encoder->CopyTexture(ec.ResolveHandle(display), ec.ResolveHandle(color));
                        }, true);
                    capture(display, RHIFormat::RGBA8Unorm);
                    graph.RequireImportedFinalState(display, RHIResourceState::PixelShaderResource);
                    require(graph.Compile(error), "Final graph compile: " + error);
                    EnhancedRenderGraph::DiagnosticSnapshot snapshot;
                    require(graph.CaptureDiagnosticSnapshot(snapshot), "Final snapshot missing");
                    for (const auto& pass : snapshot.passes)
                    {
                        for (size_t i = 0; i < pass.usages.size(); ++i)
                        {
                            const auto& usage = pass.usages[i];
                            require(policy == 0 || usage.access != RGAccessMode::LegacyState, "Implicit final access");
                            for (size_t j = 0; j < i; ++j)
                            {
                                require(usage.resource != pass.usages[j].resource, "Duplicate final resource usage");
                            }
                            if (pass.name == "Final.Present" && usage.state == RHIResourceState::CopySource)
                            {
                                require(usage.resource == color.index && usage.version == color.version, "Present used stale output");
                            }
                        }
                    }
                    require(graph.Execute(error) && resources.EndFrame(error), "Final execute: " + error);
                    resources.WaitForGpu();
                    require(displayState == RHIResourceState::PixelShaderResource, "Present final state was not restored");
                    std::vector<std::vector<float>> pixels;
                    for (size_t i = 0; i < readbacks.handles.size(); ++i)
                    {
                        RHIReadbackImage image;
                        require(resources.MapReadback(readbacks.handles[i], image, error), error);
                        std::vector<float> values;
                        for (uint32_t y = 0; y < size; ++y)
                        {
                            for (uint32_t x = 0; x < size; ++x)
                            {
                                for (uint32_t channel = 0; channel < 4; ++channel)
                                {
                                    const auto value = image.At(x, y, channel);
                                    require(std::isfinite(value), "Nonfinite final pixel");
                                    values.push_back(value);
                                }
                            }
                        }
                        if (policy == 0)
                        {
                            references[fixture].insert(references[fixture].end(), values.begin(), values.end());
                        }
                        else
                        {
                            require(std::equal(values.begin(), values.end(), references[fixture].begin() + i * size * size * 4),
                                    "Final policy pixel mismatch fixture=" + std::to_string(fixture) + " stage=" + std::to_string(i));
                        }
                        pixels.push_back(std::move(values));
                    }
                    require(pixels[7] == pixels[8], "Capture and present differ");
                    require(enableFog && fixture != 36 || pixels[0] == pixels[1], "Fog bypass changed input");
                    if (policy == 0)
                    {
                        for (uint32_t i = 0; i < 7; ++i)
                        {
                            if (pixels[i] != pixels[i + 1])
                            {
                                ++contributions[i];
                            }
                        }
                        if (fixture == 40)
                        {
                            historyPixels = pixels[1];
                        }
                        if (fixture == 41)
                        {
                            require(pixels[1] == historyPixels, "Fog history was lost with lights off");
                        }
                        if (fixture == 42)
                        {
                            require(pixels[1] != historyPixels, "Fog reset retained prior light");
                        }
                    }
                    std::string messages;
                    const auto validationCount = resources.DrainDebugMessages(messages);
                    require(validationCount == 0, "Final GPU validation: " + messages);
                }
            }
            for (const auto count : contributions)
            {
                require(count != 0, "A final stage made no pixel contribution");
            }
            error.clear();
            return true;
        }
        catch (const std::exception& exception)
        {
            error = exception.what();
            return false;
        }
    }
}
