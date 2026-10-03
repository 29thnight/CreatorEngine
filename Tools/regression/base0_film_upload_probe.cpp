#include "RHI/Vulkan/VulkanDeviceResources.h"
#include "RHI/Vulkan/VulkanCommandBufferPool.h"
#include "RHI/Vulkan/VulkanPipelineCache.h"
#include "RHI/RHIShaderCompiler.h"
#include "RHI/RHISubmissionThread.h"
#include "Render/Graph/EnhancedRenderGraph.h"
#include "MaterialGraphThinFilmSensitivity.h"
#include "PathFinder.h"
#include <iostream>
#include <stdexcept>

int main(int argc, char** argv)
{
    try
    {
        if (argc != 2) throw std::runtime_error("shader absolute path required");
        std::cout << std::unitbuf;
        const auto repo = std::filesystem::path(argv[1]).parent_path().parent_path().parent_path();
        auto* paths = InternalPath::GetInstance();
        paths->BaseProjectPath = repo / "Dynamic_CPP";
        paths->ShaderSourcePath = repo / "Dynamic_CPP/Assets/Shaders";
        paths->CacheRoot = repo / "Build/Obj/RenderBase0/FilmUploadProbe/Cache";
        paths->AssetAuthoringEnabled = true;
        std::string error;
        RHIShaderCompiler::VerifiedShader shader;
        RHIShaderCompileOptions options; options.strictMath = true;
        if (!RHIShaderCompiler::VerifyFile(argv[1], "VerifyFilmUpload", "cs_6_0",
                RHIShaderBinary::SpirV, {}, shader, error, options)) throw std::runtime_error(error);
        VulkanDeviceResources device;
        if (!device.Initialize(512, 4, true, error) || !device.IsValidationEnabled())
            throw std::runtime_error(error);
        thread_pool workers; job_scheduler jobs(workers); jobs.start(1);
        VulkanCommandBufferPool pool(jobs);
        if (!pool.Initialize(device, 1, VulkanDeviceResources::kFrameCount, error)) throw std::runtime_error(error);
        VulkanPipelineCache cache(jobs); cache.Initialize(device.GetDevice());
        device.SetPipelineCache(&cache);
        const RHIPipelineLayoutParam params[]{RHILayout::Cbv(1), RHILayout::UavTable(1, 0)};
        const auto layout = cache.GetOrCreate(RHIPipelineLayoutDesc{params, {}}, error);
        RHIComputePipelineDesc desc; desc.layout = layout;
        desc.csBytecode = shader.bytecode.Data(); desc.csSize = shader.bytecode.Size();
        const auto pipeline = cache.GetOrCreateCompute(desc, error);
        if (!pipeline.IsValid() || !device.BeginFrame(error)) throw std::runtime_error(error);
        pool.BeginFrame(0);
        const auto constants = device.UploadConstants(material_graph::kFilmSensitivityConstants.data(),
                                                      sizeof(material_graph::kFilmSensitivityConstants));
        if (!constants.IsValid()) throw std::runtime_error("Table upload failed");
        RHIReadback readback;
        if (!device.CreateReadback(512, 4, RHIFormat::RGBA32Float, 1, readback, error)) throw std::runtime_error(error);
        {
        EnhancedRenderGraph graph(static_cast<IRenderDeviceServices&>(device));
        RGTextureDesc target; target.width = 512; target.height = 4;
        target.format = RHIFormat::RGBA32Float; target.allowUnorderedAccess = true;
        const auto output = graph.CreateTexture(target);
        graph.AddPass("FilmUpload.Verify", {{output, RHIResourceState::UnorderedAccess}},
            [&](const auto& context) {
                const auto binding = RHIBindingDesc::Uav2D(context.ResolveHandle(output), RHIFormat::RGBA32Float);
                const auto table = device.CreateBindings({&binding, 1});
                if (!table.IsValid()) throw std::runtime_error("Output binding failed");
                context.encoder->SetPipeline(RHIBindPoint::Compute, pipeline);
                context.encoder->SetConstantBuffer(RHIBindPoint::Compute, 0, constants);
                context.encoder->SetBindings(RHIBindPoint::Compute, 1, table);
                context.encoder->Dispatch(16, 1, 1);
            });
        graph.AddPass("FilmUpload.Readback", {{output, RHIResourceState::CopySource}},
            [&](const auto& context) { context.encoder->CopyToReadback(readback, context.ResolveHandle(output)); }, true);
        if (!graph.Compile(error)) throw std::runtime_error(error);
        RHIRecordedBatchDesc batchDesc; batchDesc.frameId = 1;
        batchDesc.backendGeneration = GetRHISubmissionThread().GetOwnerGeneration(&device);
        batchDesc.lifetimeToken = std::make_shared<unsigned>(1);
        RHIRecordedBatch batch; RHISubmissionTicket ticket;
        if (!graph.RecordParallel(pool, 1, batchDesc, batch, error) ||
            !GetRHISubmissionThread().EnqueueRecordedBatch(&device, device, std::move(batch), ticket, error) ||
            !device.EndFrame(error) || !GetRHISubmissionThread().Wait(ticket, error)) throw std::runtime_error(error);
        device.WaitForGpu();
        RHIReadbackImage image;
        if (!device.MapReadback(readback, image, error)) throw std::runtime_error(error);
        for (unsigned i = 0; i < 512; ++i)
            for (unsigned row = 0; row < 4; ++row)
                for (unsigned c = 0; c < 4; ++c)
                    if (image.At(i, row, c) != material_graph::kFilmSensitivityConstants[(row / 2 * 512 + i) * 4 + c])
                        throw std::runtime_error("Original shader table / uploaded table differs");
        }
        if (device.GetUnimplementedCount() || device.GetEncoderUnimplementedCount() ||
            pool.GetEncoderUnimplementedCount()) throw std::runtime_error("Encoder call omitted");
        device.ReleaseReadback(readback); pool.Shutdown();
        device.SetPipelineCache(nullptr); cache.Shutdown(); device.Shutdown();
        std::string validation;
        if (device.DrainDebugMessages(validation)) throw std::runtime_error(validation);
        std::cout << "BASE0_FILM_UPLOAD_OK validation=0 rows=1024 originalShader=bit-exact\n";
        return 0;
    }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
