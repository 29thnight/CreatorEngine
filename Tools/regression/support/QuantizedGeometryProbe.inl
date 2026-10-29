void RunQuantizedGeometry(RecordingChangeDevice& device, ProbeRoots& roots, ProbePipelines& pipelines,
    const std::filesystem::path& root)
{
    using namespace assets::quantized;
    std::string error;
    const RHIPipelineLayoutParam params[]{RHILayout::Cbv(0), RHILayout::Srv(0), RHILayout::UavBufferTable(1, 0)};
    const auto layout = roots.GetOrCreate({params, {}}, error);
    Check(layout.IsValid(), "Quantized geometry layout " + error);
    LX::Runtime::CompiledCompute shader;
    RHIShaderCompileOptions options;
    options.strictMath = true;
    Check(LX::Runtime::CompileCompute((root / "Tools/regression/support/QuantizedGeometryProbe.slang").string(),
        "QuantizedGeometryCS", {}, options, shader, error), "Quantized GPU decoder compile " + error);
    LX::Runtime::ComputePipeline kernel;
    RHIComputePipelineDesc pipeline;
    pipeline.layout = layout;
    pipeline.csBytecode = shader.stage.bytecode.Data();
    pipeline.csSize = shader.stage.bytecode.Size();
    Check(kernel.Create(pipelines, pipeline, std::move(shader.description), error), "Quantized GPU decoder PSO " + error);
    unsigned cases{}, corruptions{};
    std::set<unsigned> widths;
    std::uint64_t rawBytes{}, packedBytes{}, wordsChecked{};
    for (auto mask : assets::kModelVertexMasks)
    {
        for (unsigned count : {1u, 1031u})
        {
            const auto stride = assets::StrideOf(mask);
            ce::dynamic_array<std::byte> original(std::size_t(count) * stride);
            for (unsigned vertex = 0; vertex < count; ++vertex)
            {
                for (unsigned channel = 0; channel < stride / 4; ++channel)
                {
                    std::uint32_t bits{};
                    if (assets::Has(mask, assets::VertexAttribute::BoneIndices) && channel * 4 == assets::OffsetOf(mask, assets::VertexAttribute::BoneIndices))
                    {
                        bits = 0xffff0000u | (vertex & 255u);
                    }
                    else
                    {
                        float value{};
                        switch (channel % 6)
                        {
                        case 0: value = float(vertex % 256) * .25f - 32; break;
                        case 1: value = float(vertex % 1024) * .0625f - 16; break;
                        case 2: value = float(std::uint64_t(vertex) * 65535 / (count > 1 ? count - 1 : 1)) * .125f; break;
                        case 3: value = float(std::uint64_t(vertex) * 0xffffffu / (count > 1 ? count - 1 : 1)) * .03125f; break;
                        case 4: value = vertex % 2 ? .1234567f + vertex * .001f : -.9876543f - vertex * .00017f; break;
                        default: value = -0.f; break;
                        }
                        bits = std::bit_cast<std::uint32_t>(value);
                        if (channel % 6 == 4 && vertex < 4)
                        {
                            constexpr std::uint32_t edgeBits[]{1u, 0x80000001u, 0x7f7fffffu, 0x00800000u};
                            bits = edgeBits[vertex];
                        }
                    }
                    Put(original, std::size_t(vertex) * stride + channel * 4, bits);
                }
            }
            ce::dynamic_array<std::byte> packet, roundtrip;
            Check(Encode(mask, count, original, packet, error), "Quantized CPU encode " + error);
            Check(Decode(packet, mask, count, roundtrip, error) && roundtrip == original, "Quantized CPU bit-exact roundtrip " + error);
            if (count > 1)
            {
                Check(Word(packet, 8) == 0 && packet.size() < original.size(), "Real quantization reduces fixture storage");
                for (unsigned channel = 0; channel < stride / 4; ++channel)
                {
                    widths.insert(Word(packet, HeaderBytes + channel * DescriptorBytes + 4));
                }
                rawBytes += original.size();
                packedBytes += packet.size();
            }
            for (unsigned mutation = 0; mutation < 8; ++mutation)
            {
                auto bad = packet;
                if (mutation == 0)
                {
                    bad.pop_back();
                }
                else if (mutation <= 5)
                {
                    Put(bad, (mutation - 1) * 4, 0xffffffffu);
                }
                else
                {
                    Put(bad, mutation == 6 ? 24 : 28, 0xffffffffu);
                }
                Check(!Decode(bad, mask, count, roundtrip, error) && roundtrip.empty(), "Reject corrupt geometry header transactionally");
                ++corruptions;
            }
            if (Word(packet, 8) == 0)
            {
                for (unsigned mutation = 0; mutation < 5; ++mutation)
                {
                    auto bad = packet;
                    if (mutation == 0)
                    {
                        Put(bad, HeaderBytes, 3);
                    }
                    else if (mutation == 1)
                    {
                        Put(bad, HeaderBytes + 4, 7);
                    }
                    else if (mutation == 2)
                    {
                        Put(bad, HeaderBytes + 16, 0xfffffffc);
                    }
                    else if (mutation == 3)
                    {
                        Put(bad, HeaderBytes + 12, 0x7fc00000);
                    }
                    else
                    {
                        const auto offset = Word(bad, HeaderBytes + 16);
                        const auto words = Word(bad, HeaderBytes + 20);
                        Put(bad, offset + (words - 1) * 4, 1);
                    }
                    Check(!Decode(bad, mask, count, roundtrip, error) && roundtrip.empty(),
                        "Reject invalid stream, nonfinite scale and noncanonical guard");
                    ++corruptions;
                }
            }
            auto nonfinite = original;
            Put(nonfinite, 0, 0x7fc00000);
            ce::dynamic_array<std::byte> rejected;
            Check(!Encode(mask, count, nonfinite, rejected, error) && rejected.empty(), "Reject nonfinite authored position");
            ++corruptions;
            Check(device.BeginFrame(error), "Quantized GPU begin");
            const auto input = device.AllocateUpload({packet.size(), RHIUploadUsage::Raw, 256});
            Check(input.IsWritable(), "Quantized packet upload");
            std::memcpy(input.cpuAddress, packet.data(), packet.size());
            const std::array<std::uint32_t, 4> constants{static_cast<unsigned>(original.size() / 4), stride / 4, 0, 0};
            const auto uniform = device.UploadConstants(constants.data(), sizeof(constants));
            RHIBufferDesc desc;
            desc.bytes = original.size();
            desc.allowUnorderedAccess = true;
            RHIBufferHandle output;
            Check(device.CreateBuffer(desc, output, error), "Quantized GPU output");
            const auto binding = RHIBindingDesc::UavBuffer(output, static_cast<unsigned>(original.size() / 4), 4);
            const auto table = device.CreateBindings({&binding, 1});
            auto& encoder = device.GetImmediateEncoder();
            const RHIBufferTransition before{output, RHIResourceState::Common, RHIResourceState::UnorderedAccess};
            encoder.ResourceBarriers({{}, {&before, 1}});
            encoder.SetPipeline(RHIBindPoint::Compute, kernel.GetGeneration()->GetHandle());
            encoder.SetConstantBuffer(RHIBindPoint::Compute, 0, uniform);
            encoder.SetRootBuffer(RHIBindPoint::Compute, 1, input);
            encoder.SetBindings(RHIBindPoint::Compute, 2, table);
            encoder.Dispatch((constants[0] + 63) / 64, 1, 1);
            RHIReadback readback;
            Check(device.CreateBufferReadback(original.size(), readback, error), "Quantized GPU readback");
            const RHIBufferTransition after{output, RHIResourceState::UnorderedAccess, RHIResourceState::CopySource};
            encoder.ResourceBarriers({{}, {&after, 1}});
            encoder.CopyBufferToReadback(readback, output);
            Check(device.EndFrame(error), "Quantized GPU submit");
            device.WaitForGpu();
            RHIReadbackImage image;
            Check(device.MapReadback(readback, image, error), "Quantized GPU map");
            Check(image.data.size() >= original.size() && std::memcmp(original.data(), image.data.data(), original.size()) == 0,
                "GPU reconstructs original IEEE bits, signed zero and bone indices");
            wordsChecked += original.size() / 4;
            device.ReleaseReadback(readback);
            device.ReleaseBuffer(output);
            ++cases;
        }
    }
    Check(widths.contains(0) && widths.contains(8) && widths.contains(10) && widths.contains(16) && widths.contains(24) && widths.contains(32),
        "Every scalar representation exercised on GPU");
    std::string messages;
    Check(device.DrainDebugMessages(messages) == 0, "Quantized geometry GPU validation " + messages);
    std::cout << "METEOR_QUANTIZED_GEOMETRY_OK cases=" << cases << " corruptions=" << corruptions
        << " words=" << wordsChecked << " rawBytes=" << rawBytes << " packedBytes=" << packedBytes << " validation=0\n";
}
