#pragma once

namespace
{
    bool ValidateRg5ScreenGpu(DX12DeviceResources &resources, std::string &error)
    {
        DX12PSOManager pso;
        DX12RootSignatureCache roots;
        DX12TextureCache textures;
        if (!pso.Initialize(&resources, L"dx12_rg5_screen.cache", error) || !roots.Initialize(&resources, error) ||
            !textures.Initialize(&resources, error))
        {
            return false;
        }
        EnhancedFrameContext frame{};
        frame.resources = &resources;
        frame.psoManager = &pso;
        frame.rootSignatures = &roots;
        frame.textureCache = &textures;
        frame.width = 16;
        frame.height = 16;
        EnhancedSpritePass sprite;
        EnhancedSSSPass sss;
        EnhancedSSRPass ssr;
        if (!sprite.Initialize(frame, error) || !sss.Initialize(frame, error) || !ssr.Initialize(frame, error))
        {
            return false;
        }
        auto sssTuning = sss.GetTuning();
        sssTuning.width = 0.2f;
        sss.SetTuning(sssTuning);
        auto ssrTuning = ssr.GetTuning();
        ssrTuning.stepSize = 0.1f;
        ssrTuning.maxThickness = 0.5f;
        ssrTuning.maxRayCount = 4;
        ssr.SetTuning(ssrTuning);
        ssr.SetTime(0.25f);
        const uint32_t white = 0xffffffff;
        std::unique_ptr<Texture> texture(
            Texture::CreateFromPixels(1, 1, "RG5.Screen.White", RHIFormat::RGBA8Unorm, &white));
        if (!texture)
        {
            return false;
        }
        std::array<std::vector<float>, 16> references;
        for (uint32_t policy = 0; policy < 3; ++policy)
        {
            for (uint32_t fixture = 0; fixture < 16; ++fixture)
            {
                const bool enableSss = fixture >= 8;
                const bool enableSsr = (fixture / 4) % 2 != 0;
                const uint32_t variant = fixture % 4;
                if (!resources.BeginFrame(error))
                {
                    return false;
                }
                struct RecordingScope
                {
                    DX12DeviceResources &resources;
                    ~RecordingScope()
                    {
                        resources.AbortFrame();
                    }
                } recording{resources};
                struct DepthScope
                {
                    DX12DeviceResources &resources;
                    RHITextureHandle handle;
                    ~DepthScope()
                    {
                        if (handle.IsValid())
                        {
                            resources.ReleaseTexture(handle);
                        }
                    }
                } depthResource{resources, {}};
                EnhancedRenderGraph graph(
                    resources, policy == 0 ? RGSchedulingMode::DeclarationOrder : RGSchedulingMode::ExplicitVersioned,
                    policy == 1 ? RGOrderPolicy::PreserveDeclarationOrder : RGOrderPolicy::DependencyOrder);
                const auto write = policy == 0 ? RGAccessMode::LegacyState : RGAccessMode::Write;
                const auto read = policy == 0 ? RGAccessMode::LegacyState : RGAccessMode::Read;
                const auto clearColor = [&](const char *name, RHIFormat format, const std::array<float, 4> &clear) {
                    RGTextureDesc desc{};
                    desc.width = 16;
                    desc.height = 16;
                    desc.format = format;
                    desc.allowRenderTarget = true;
                    desc.name = name;
                    std::copy(clear.begin(), clear.end(), desc.clearColor);
                    auto output = graph.CreateTexture(desc);
                    if (policy != 0)
                    {
                        output = graph.Write(output);
                    }
                    graph.AddPass(
                        name, {{output, RHIResourceState::RenderTarget, write}},
                        [output, clear, &resources](const EnhancedRenderGraph::ExecuteContext &ec) {
                            const RHITextureHandle colors[] = {ec.ResolveHandle(output)};
                            const auto targets = resources.CreateRenderTargets(colors);
                            ec.encoder->ClearRenderTargets(targets, clear.data());
                        },
                        true);
                    return output;
                };
                auto color = clearColor("RG5.Screen.Color", RHIFormat::RGBA16Float, {0.0625f, 0.125f, 0.25f, 1.f});
                const auto metal = clearColor("RG5.Screen.Metal", RHIFormat::RGBA16Float, {0.75f, 0.5f, 0.f, 0.f});
                const auto normal = clearColor("RG5.Screen.Normal", RHIFormat::RGBA16Float, {1.f, 0.5f, 0.5f, 0.f});
                const auto mask =
                    clearColor("RG5.Screen.Mask", RHIFormat::R32Uint, {variant == 2 ? 512.f : 0.f, 0.f, 0.f, 0.f});
                RHITextureDesc depthDesc{};
                depthDesc.width = 16;
                depthDesc.height = 16;
                depthDesc.format = EnhancedGBufferPass::kDepthFormat;
                depthDesc.allowDepthStencil = true;
                depthDesc.clearDepth = 0.5f;
                if (!resources.CreateTexture(depthDesc, depthResource.handle, error))
                {
                    return false;
                }
                auto depth = graph.ImportTexture(depthResource.handle, RHIResourceState::Common, "RG5.Screen.Depth");
                if (policy != 0)
                {
                    depth = graph.Write(depth);
                }
                graph.AddPass(
                    "RG5.Screen.DepthClear", {{depth, RHIResourceState::DepthWrite, write}},
                    [depth, &resources](const EnhancedRenderGraph::ExecuteContext &ec) {
                        const auto target =
                            RHIDepthTargetDesc::Depth(ec.ResolveHandle(depth), EnhancedGBufferPass::kDepthFormat);
                        const auto targets =
                            resources.CreateRenderTargets(std::span<const RHITextureHandle>{}, &target);
                        ec.encoder->ClearDepthTarget(targets, 0.5f);
                    },
                    true);
                std::vector<EnhancedSpritePass::Item> items;
                if (variant != 0)
                {
                    items.resize(1);
                    items[0].texture = texture.get();
                    items[0].color = {0.25f, 0.5f, 0.75f, 1.f};
                }
                sprite.SetItems(&items);
                sprite.SetInputs({color, depth});
                sss.SetEnabled(enableSss);
                ssr.SetEnabled(enableSsr);
                if (!sprite.PrepareFrame(frame, error) || !sss.PrepareFrame(frame, error) ||
                    !ssr.PrepareFrame(frame, error))
                {
                    return false;
                }
                sprite.Declare(graph, frame);
                color = sprite.GetOutput();
                sss.SetInputs({color, depth});
                sss.Declare(graph, frame);
                const auto scattered = sss.GetOutput();
                const auto horizontal = enableSss ? sss.GetHorizontal() : color;
                ssr.SetInputs({scattered, depth, metal, variant == 3 ? RGHandle{} : normal, mask});
                ssr.Declare(graph, frame);
                const auto output = ssr.GetOutput();
                const bool activeSsr = enableSsr && variant != 3;
                if (!scattered.IsValid() || !output.IsValid() ||
                    (enableSss && policy != 0 && (scattered.version != 0 || horizontal.version != 0)) ||
                    (activeSsr && policy != 0 && output.version != 0) ||
                    (!enableSss && (scattered.index != color.index || scattered.version != color.version)) ||
                    (!activeSsr && (output.index != scattered.index || output.version != scattered.version)))
                {
                    error = "RG5 screen output version or bypass identity mismatch";
                    return false;
                }
                const std::array<RGHandle, 4> handles = {color, horizontal, scattered, output};
                struct Readbacks
                {
                    DX12DeviceResources &resources;
                    std::array<RHIReadback, 4> values{};
                    ~Readbacks()
                    {
                        for (auto &value : values)
                        {
                            resources.ReleaseReadback(value);
                        }
                    }
                } readbacks{resources};
                std::vector<EnhancedRenderGraph::RGPassUsage> uses;
                for (uint32_t i = 0; i < 4; ++i)
                {
                    if (!resources.CreateReadback(16, 16, RHIFormat::RGBA16Float, 1, readbacks.values[i], error))
                    {
                        return false;
                    }
                    if (std::none_of(uses.begin(), uses.end(), [handle = handles[i]](const auto &usage) {
                            return usage.handle.index == handle.index && usage.handle.version == handle.version;
                        }))
                    {
                        uses.push_back({handles[i], RHIResourceState::CopySource, read});
                    }
                }
                graph.AddPass(
                    "RG5.Screen.Readback", uses,
                    [handles, &readbacks](const EnhancedRenderGraph::ExecuteContext &ec) {
                        for (uint32_t i = 0; i < 4; ++i)
                        {
                            ec.encoder->CopyToReadback(readbacks.values[i], ec.ResolveHandle(handles[i]));
                        }
                    },
                    true);
                // Changing inputs after declaration must not change recorded resource bindings.
                sss.SetInputs({});
                ssr.SetInputs({});
                EnhancedRenderGraph::DiagnosticSnapshot snapshot;
                if (!graph.Compile(error) || !graph.CaptureDiagnosticSnapshot(snapshot))
                {
                    return false;
                }
                size_t screenPasses = 0;
                for (const auto &pass : snapshot.passes)
                {
                    if (pass.name == "SSS.Horizontal" || pass.name == "SSS.Vertical" || pass.name == "SSR")
                    {
                        ++screenPasses;
                    }
                    for (const auto &usage : pass.usages)
                    {
                        if (policy != 0 && usage.access == RGAccessMode::LegacyState)
                        {
                            error = "RG5 screen chain retained legacy access";
                            return false;
                        }
                    }
                }
                if (screenPasses != (enableSss ? 2u : 0u) + (activeSsr ? 1u : 0u))
                {
                    error = "RG5 screen enabled/bypass pass count mismatch";
                    return false;
                }
                if (!graph.Execute(error) || !resources.EndFrame(error))
                {
                    return false;
                }
                resources.WaitForGpu();
                std::array<RHIReadbackImage, 4> images;
                for (uint32_t i = 0; i < 4; ++i)
                {
                    if (!resources.MapReadback(readbacks.values[i], images[i], error))
                    {
                        return false;
                    }
                }
                bool blurred = false;
                bool reflected = false;
                for (uint32_t i = 0; i < 4; ++i)
                {
                    for (uint32_t y = 0; y < 16; ++y)
                    {
                        for (uint32_t x = 0; x < 16; ++x)
                        {
                            for (uint32_t channel = 0; channel < 4; ++channel)
                            {
                                const float pixel = images[i].At(x, y, channel);
                                const size_t index = ((i * 16 + y) * 16 + x) * 4 + channel;
                                if (!std::isfinite(pixel) || (policy != 0 && pixel != references[fixture][index]))
                                {
                                    error = "RG5 screen policy image comparison failed";
                                    return false;
                                }
                                if (policy == 0)
                                {
                                    references[fixture].push_back(pixel);
                                }
                                if ((i == 1 || i == 2) && variant == 0 &&
                                    std::abs(pixel - images[0].At(x, y, channel)) > 0.002f)
                                {
                                    error = "RG5 SSS uniform color preservation failed";
                                    return false;
                                }
                                if (i == 2 && channel < 3 && std::abs(pixel - images[0].At(x, y, channel)) > 0.002f)
                                {
                                    blurred = true;
                                }
                                if (i == 3)
                                {
                                    if ((!activeSsr || variant == 2) && pixel != images[2].At(x, y, channel))
                                    {
                                        error = "RG5 SSR disabled/missing/mask bypass image mismatch";
                                        return false;
                                    }
                                    if (channel < 3 && pixel - images[2].At(x, y, channel) > 0.01f)
                                    {
                                        reflected = true;
                                    }
                                }
                            }
                        }
                    }
                }
                if ((enableSss && variant == 1 && !blurred) || (activeSsr && variant == 1 && !reflected))
                {
                    error = "RG5 screen enabled shader had no blur/reflection contribution";
                    return false;
                }
            }
        }
        sprite.Shutdown();
        sss.Shutdown();
        ssr.Shutdown();
        error.clear();
        return true;
    }
} // namespace