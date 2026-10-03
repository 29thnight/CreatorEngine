#pragma once

namespace
{
    // Actual Sprite GPU acceptance fixtures shared by the RenderGraph commandlet.
    bool ValidateRg5SpriteGpu(DX12DeviceResources &resources, std::string &error)
    {
        DX12PSOManager pso;
        DX12RootSignatureCache roots;
        DX12TextureCache textures;
        if (!pso.Initialize(&resources, L"dx12_rg5_sprite.cache", error) || !roots.Initialize(&resources, error) ||
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
        if (!sprite.Initialize(frame, error))
        {
            return false;
        }
        const uint32_t white = 0xffffffff;
        std::unique_ptr<Texture> texture(
            Texture::CreateFromPixels(1, 1, "RG5.Sprite.White", RHIFormat::RGBA8Unorm, &white));
        if (!texture)
        {
            error = "RG5 Sprite texture creation failed";
            return false;
        }
        std::array<std::vector<float>, 6> references;
        for (uint32_t policy = 0; policy < 3; ++policy)
        {
            for (uint32_t fixture = 0; fixture < 6; ++fixture)
            {
                const bool ownsColor = fixture < 3;
                const uint32_t depthCase = fixture % 3;
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
                RGTextureDesc colorDesc{};
                colorDesc.width = 16;
                colorDesc.height = 16;
                colorDesc.format = EnhancedSpritePass::kOutputFormat;
                colorDesc.allowRenderTarget = true;
                colorDesc.name = "RG5.Sprite.Color";
                RGHandle inputColor;
                if (!ownsColor)
                {
                    colorDesc.clearColor[0] = 0.0625f;
                    colorDesc.clearColor[1] = 0.125f;
                    colorDesc.clearColor[2] = 0.25f;
                    colorDesc.clearColor[3] = 1.f;
                    inputColor = graph.CreateTexture(colorDesc);
                    if (policy != 0)
                    {
                        inputColor = graph.Write(inputColor);
                    }
                    graph.AddPass(
                        "RG5.Sprite.Clear", {{inputColor, RHIResourceState::RenderTarget, write}},
                        [inputColor, &resources](const EnhancedRenderGraph::ExecuteContext &ec) {
                            const RHITextureHandle colors[] = {ec.ResolveHandle(inputColor)};
                            const auto targets = resources.CreateRenderTargets(colors);
                            constexpr float clear[] = {0.0625f, 0.125f, 0.25f, 1.f};
                            ec.encoder->ClearRenderTargets(targets, clear);
                        },
                        true);
                    graph.AddPass("RG5.Sprite.OldReader", {{inputColor, RHIResourceState::CopySource, read}}, nullptr,
                                  true);
                }
                RGHandle depth;
                if (depthCase != 0)
                {
                    RHITextureDesc desc{};
                    desc.width = 16;
                    desc.height = 16;
                    desc.format = EnhancedGBufferPass::kDepthFormat;
                    desc.allowDepthStencil = true;
                    desc.clearDepth = depthCase == 1 ? 1.f : 0.f;
                    desc.initialState = RHIResourceState::Common;
                    if (!resources.CreateTexture(desc, depthResource.handle, error))
                    {
                        return false;
                    }
                    depth = graph.ImportTexture(depthResource.handle, RHIResourceState::Common, "RG5.Sprite.Depth");
                    if (policy != 0)
                    {
                        depth = graph.Write(depth);
                    }
                    graph.AddPass(
                        "RG5.Sprite.DepthClear", {{depth, RHIResourceState::DepthWrite, write}},
                        [depth, depthCase, &resources](const EnhancedRenderGraph::ExecuteContext &ec) {
                            const auto target =
                                RHIDepthTargetDesc::Depth(ec.ResolveHandle(depth), EnhancedGBufferPass::kDepthFormat);
                            const auto targets =
                                resources.CreateRenderTargets(std::span<const RHITextureHandle>{}, &target);
                            ec.encoder->ClearDepthTarget(targets, depthCase == 1 ? 1.f : 0.f);
                        },
                        true);
                }
                std::vector<EnhancedSpritePass::Item> items(2);
                items[0].texture = texture.get();
                items[0].color = {0.125f, 0.25f, 0.375f, 1.f};
                items[1] = items[0];
                items[1].color = {0.25f, 0.5f, 0.75f, 1.f};
                items[1].enableDepth = true;
                items[1].layerOrder = 1;
                items[1].world(3, 2) = 0.5f;
                sprite.SetItems(&items);
                sprite.SetInputs({inputColor, depth});
                if (!sprite.PrepareFrame(frame, error))
                {
                    return false;
                }
                sprite.Declare(graph, frame);
                const auto output = sprite.GetOutput();
                if (!output.IsValid() || (policy != 0 && output.version != (ownsColor ? 0 : 1)))
                {
                    error = "RG5 Sprite output version mismatch";
                    return false;
                }
                RHIReadback readback;
                if (!resources.CreateReadback(16, 16, colorDesc.format, 1, readback, error))
                {
                    return false;
                }
                struct ReadbackScope
                {
                    DX12DeviceResources &resources;
                    RHIReadback &readback;
                    ~ReadbackScope()
                    {
                        resources.ReleaseReadback(readback);
                    }
                } release{resources, readback};
                graph.AddPass(
                    "RG5.Sprite.Readback", {{output, RHIResourceState::CopySource, read}},
                    [output, &readback](const EnhancedRenderGraph::ExecuteContext &ec) {
                        ec.encoder->CopyToReadback(readback, ec.ResolveHandle(output));
                    },
                    true);
                if (!graph.Compile(error))
                {
                    return false;
                }
                EnhancedRenderGraph::DiagnosticSnapshot snapshot;
                if (!graph.CaptureDiagnosticSnapshot(snapshot))
                {
                    return false;
                }
                if (policy != 0)
                {
                    size_t sourceReads = 0;
                    for (const auto &pass : snapshot.passes)
                    {
                        for (const auto &usage : pass.usages)
                        {
                            if (usage.access == RGAccessMode::LegacyState)
                            {
                                error = "RG5 Sprite chain retained legacy access";
                                return false;
                            }
                            if (pass.name == "Sprite" && usage.state == RHIResourceState::PixelShaderResource)
                            {
                                ++sourceReads;
                            }
                        }
                    }
                    if (!ownsColor)
                    {
                        bool hasWar = false;
                        for (const auto &edge : snapshot.versionEdges)
                        {
                            if (edge.resource == inputColor.index && edge.producer == 1 &&
                                snapshot.passes[edge.consumer].name == "Sprite" &&
                                edge.reason == EnhancedRenderGraph::DiagnosticSnapshot::VersionEdge::Reason::WAR)
                            {
                                hasWar = true;
                            }
                        }
                        if (!hasWar || output.index != inputColor.index)
                        {
                            error = "RG5 Sprite Modify lost old-color reader dependency";
                            return false;
                        }
                    }
                    if (sourceReads != 1)
                    {
                        error = "RG5 Sprite shared source import/read was not deduplicated";
                        return false;
                    }
                }
                if (!graph.Execute(error) || !resources.EndFrame(error))
                {
                    return false;
                }
                resources.WaitForGpu();
                RHIReadbackImage image;
                if (!resources.MapReadback(readback, image, error))
                {
                    return false;
                }
                const float expected[] = {depthCase == 2 ? 0.125f : 0.25f, depthCase == 2 ? 0.25f : 0.5f,
                                          depthCase == 2 ? 0.375f : 0.75f, ownsColor ? 0.f : 1.f};
                for (uint32_t channel = 0; channel < 4; ++channel)
                {
                    if (image.At(8, 8, channel) != expected[channel] ||
                        image.At(0, 0, channel) !=
                            (ownsColor ? 0.f : std::array<float, 4>{0.0625f, 0.125f, 0.25f, 1.f}[channel]))
                    {
                        error = "RG5 Sprite independent result mismatch policy=" + std::to_string(policy) +
                                " fixture=" + std::to_string(fixture) + " channel=" + std::to_string(channel) +
                                " center=" + std::to_string(image.At(8, 8, channel)) +
                                " expected=" + std::to_string(expected[channel]) +
                                " border=" + std::to_string(image.At(0, 0, channel));
                        return false;
                    }
                }
                for (uint32_t y = 0; y < 16; ++y)
                {
                    for (uint32_t x = 0; x < 16; ++x)
                    {
                        for (uint32_t channel = 0; channel < 4; ++channel)
                        {
                            const auto pixel = image.At(x, y, channel);
                            const size_t index = (y * 16 + x) * 4 + channel;
                            if (!std::isfinite(pixel) || (policy != 0 && pixel != references[fixture][index]))
                            {
                                error = "RG5 Sprite policy image comparison failed";
                                return false;
                            }
                            if (policy == 0)
                            {
                                references[fixture].push_back(pixel);
                            }
                        }
                    }
                }
                items.clear();
                if (!sprite.PrepareFrame(frame, error))
                {
                    return false;
                }
                sprite.SetInputs({output, depth});
                sprite.Declare(graph, frame);
                if (sprite.GetOutput().index != output.index || sprite.GetOutput().version != output.version)
                {
                    error = "RG5 empty Sprite did not preserve incoming color";
                    return false;
                }
            }
        }
        sprite.Shutdown();
        error.clear();
        return true;
    }
} // namespace
