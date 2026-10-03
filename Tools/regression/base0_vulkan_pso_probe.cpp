#include "PathFinder.h"
#include "RHI/RHIShaderCompiler.h"
#include "RHI/Vulkan/VulkanDeviceResources.h"
#include "RHI/Vulkan/VulkanPipelineCache.h"
#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>

int main(int argc, char** argv)
{
    std::cout << std::unitbuf;
    try
    {
        if (argc != 5) throw std::runtime_error("repo shader output mode(normal/disable-opt/compile-only)");
        const std::filesystem::path repo(argv[1]), shader(argv[2]), output(argv[3]);
        const std::string mode(argv[4]);
        if (mode != "normal" && mode != "disable-opt" && mode != "compile-only")
            throw std::runtime_error("Unknown probe mode");
        auto* paths = InternalPath::GetInstance();
        paths->BaseProjectPath = repo / "Dynamic_CPP";
        paths->ShaderSourcePath = shader.parent_path().parent_path();
        paths->CacheRoot = output / "Cache";
        paths->AssetAuthoringEnabled = true;
        std::filesystem::create_directories(output);
        RHIShaderCompileOptions options;
        options.strictMath = true;
        RHIShaderCompiler::VerifiedShader bake;
        std::string error;
        const auto started = std::chrono::steady_clock::now();
        std::cout << "BASE0_PSO_SHADER_BEGIN\n";
        if (!RHIShaderCompiler::VerifyFile(shader.string(), "LXSceneLookupBake", "cs_6_0",
                RHIShaderBinary::SpirV, {}, bake, error, options)) throw std::runtime_error(error);
        std::ofstream binary(output / "bake.spv", std::ios::binary);
        binary.write(static_cast<const char*>(bake.bytecode.Data()), bake.bytecode.Size());
        binary.close();
        std::cout << "BASE0_PSO_SHADER_OK bytes=" << bake.bytecode.Size() << " ms="
                  << std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count() << '\n';
        if (mode == "compile-only") return 0;
        VulkanDeviceResources device;
        if (!device.Initialize(16, 16, true, error) || !device.IsValidationEnabled()) throw std::runtime_error(error);
        thread_pool workers;
        job_scheduler jobs(workers);
        jobs.start(1);
        VulkanPipelineCache cache(jobs);
        cache.Initialize(device.GetDevice());
        const RHIPipelineLayoutParam parameters[]{RHILayout::Cbv(0), RHILayout::SrvTable(24, 0),
            RHILayout::UavBufferTable(2, 0), RHILayout::Srv(24), RHILayout::SrvTable(6, 25), RHILayout::Cbv(1)};
        const RHIStaticSamplerDesc samplers[]{
            {RHISampler::Point(RHIAddressMode::Clamp), 0}, {RHISampler::Linear(RHIAddressMode::Clamp), 1}};
        const auto layout = cache.GetOrCreate({parameters, samplers}, error);
        if (!layout.IsValid()) throw std::runtime_error(error);
        const auto* words = static_cast<const uint32_t*>(bake.bytecode.Data());
        std::string entry;
        for (size_t i = 5; i < bake.bytecode.Size() / 4;)
        {
            const auto count = words[i] >> 16;
            if (!count || i + count > bake.bytecode.Size() / 4)
                throw std::runtime_error("Invalid SPIR-V instruction");
            if ((words[i] & 0xffff) == 15 && count > 3 && words[i + 1] == 5)
                entry = reinterpret_cast<const char*>(words + i + 3);
            i += count;
        }
        if (entry.empty()) throw std::runtime_error("Compute entry missing");
        VkShaderModuleCreateInfo moduleInfo{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
        moduleInfo.codeSize = bake.bytecode.Size(); moduleInfo.pCode = words;
        VkShaderModule module{};
        if (VulkanApi::vkCreateShaderModule(device.GetDevice(), &moduleInfo, nullptr, &module) != VK_SUCCESS)
            throw std::runtime_error("Shader module failed");
        VkComputePipelineCreateInfo info{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
        info.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        info.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
        info.stage.module = module; info.stage.pName = entry.c_str();
        info.layout = cache.Resolve(layout).layout;
        if (mode == "disable-opt") info.flags = VK_PIPELINE_CREATE_DISABLE_OPTIMIZATION_BIT;
        const auto psoStarted = std::chrono::steady_clock::now();
        std::cout << "BASE0_PSO_DRIVER_BEGIN mode=" << mode << " adapter=" << device.GetAdapterName() << '\n';
        VkPipeline pipeline{};
        const auto result = VulkanApi::vkCreateComputePipelines(device.GetDevice(), VK_NULL_HANDLE, 1, &info, nullptr, &pipeline);
        std::cout << "BASE0_PSO_DRIVER_END result=" << result << " ms="
                  << std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - psoStarted).count() << '\n';
        if (pipeline) VulkanApi::vkDestroyPipeline(device.GetDevice(), pipeline, nullptr);
        VulkanApi::vkDestroyShaderModule(device.GetDevice(), module, nullptr);
        cache.Shutdown(); device.Shutdown();
        std::string validation;
        if (device.DrainDebugMessages(validation)) throw std::runtime_error(validation);
        if (result != VK_SUCCESS) throw std::runtime_error("PSO create failed");
        std::cout << "BASE0_PSO_PROBE_OK validation=0\n";
        return 0;
    }
    catch (const std::exception& exception)
    {
        std::cerr << "BASE0_PSO_PROBE_FAILED " << exception.what() << '\n';
        return 1;
    }
}
