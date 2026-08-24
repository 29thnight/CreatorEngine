#include "ShaderReflectionSelfTest.h"

#include "DataSystem.h"
#include "RHI/RHIShaderCompiler.h"
#include "RHI/RHIShaderSource.h"
#include "ShaderMetaReflection.h"
#include "ShaderPermutationDomain.h"

#include <array>
#include <filesystem>
#include <vector>

namespace
{
    struct ReflectionStageRequest
    {
        std::string entry;
        const char* profile{};
    };
}

bool RenderTest::RunShaderReflectionSelfTest(std::string& outLog)
{
    const std::filesystem::path metaPath = RHIShaderSource::Resolve(
        "SelfTest/ShaderMetaFixture.shadermeta");
    const FileGuid guid = DataSystems->GetFileGuid(metaPath);
    ShaderMeta meta;
    std::string error;
    if (FileGuid{} == guid || !DataSystems->LoadShaderMetaGUID(guid, meta, error))
    {
        outLog += "[shader reflection] ShaderMeta load 실패: " + error + "\n";
        return false;
    }

    std::error_code pathError;
    const std::filesystem::path sourcePath = meta.ResolveSource(metaPath);
    const std::filesystem::path shaderRoot = RHIShaderSource::Resolve("");
    const std::filesystem::path relativeSource = std::filesystem::relative(
        sourcePath, shaderRoot, pathError);
    if (pathError || relativeSource.empty())
    {
        outLog += "[shader reflection] source 상대 경로 계산 실패\n";
        return false;
    }

    const std::array<std::uint16_t, 1> keywordSelection{ 1 };
    ShaderMetaPermutation permutation;
    if (!ShaderPermutationDomain::Resolve(meta, 0, keywordSelection,
        permutation, error))
    {
        outLog += "[shader reflection] permutation resolve 실패: " + error + "\n";
        return false;
    }

    std::vector<ReflectionStageRequest> requests;
    const ShaderPassDesc& pass = meta.passes[0];
    if (pass.vertex) requests.push_back({ pass.vertex->entry, "vs_5_0" });
    if (pass.pixel) requests.push_back({ pass.pixel->entry, "ps_5_0" });
    if (pass.compute) requests.push_back({ pass.compute->entry, "cs_5_0" });

    std::vector<RHIShaderReflection> dxilStages;
    std::vector<RHIShaderReflection> spirvStages;
    dxilStages.reserve(requests.size());
    spirvStages.reserve(requests.size());
    for (const ReflectionStageRequest& request : requests)
    {
        RHIShaderReflection dxil;
        RHIShaderReflection spirv;
        const std::string sourceName = relativeSource.generic_string();
        if (!RHIShaderCompiler::ReflectFile(sourceName, request.entry,
            request.profile, RHIShaderBinary::Dxil, permutation.defines,
            dxil, error)
            || !RHIShaderCompiler::ReflectFile(sourceName, request.entry,
                request.profile, RHIShaderBinary::SpirV, permutation.defines,
                spirv, error))
        {
            outLog += "[shader reflection] Slang target layout 추출 실패: "
                + error + "\n";
            return false;
        }
        if (!AreShaderReflectionsEquivalent(dxil, spirv, error))
        {
            outLog += "[shader reflection] DXIL/SPIR-V 불일치: " + error + "\n";
            return false;
        }
        dxilStages.push_back(std::move(dxil));
        spirvStages.push_back(std::move(spirv));
    }

    ShaderMetaBindingLayout layout;
    if (!ShaderMetaReflection::Resolve(meta, dxilStages, layout, error))
    {
        outLog += "[shader reflection] ShaderMeta layout 대조 실패: " + error + "\n";
        return false;
    }

    const bool layoutMatches = "MaterialProperties" == layout.constantBufferName
        && 2 == layout.constantBufferRegister
        && 0 == layout.constantBufferSpace
        && 32 == layout.constantBufferByteSize
        && 3 == layout.properties.size()
        && "tint" == layout.properties[0].name
        && 0 == layout.properties[0].byteOffset
        && 16 == layout.properties[0].byteSize
        && "roughness" == layout.properties[1].name
        && 16 == layout.properties[1].byteOffset
        && 4 == layout.properties[1].byteSize
        && "albedoMap" == layout.properties[2].name
        && RHIShaderResourceKind::Texture == layout.properties[2].resourceKind
        && 3 == layout.properties[2].registerIndex;
    if (!layoutMatches)
    {
        outLog += "[shader reflection] material property offset/binding 계약 불일치\n";
        return false;
    }

    ShaderMeta mismatchedMeta = meta;
    mismatchedMeta.properties[1].type = ShaderPropertyType::Float4;
    ShaderMetaBindingLayout invalidLayout;
    std::string typeError;
    const bool typeMismatchRejected = !ShaderMetaReflection::Resolve(
        mismatchedMeta, dxilStages, invalidLayout, typeError)
        && std::string::npos != typeError.find("type");

    RHIShaderReflection mismatchedTarget = spirvStages.front();
    if (mismatchedTarget.resources.empty())
    {
        outLog += "[shader reflection] target mismatch 음성 대조 resource가 없다\n";
        return false;
    }
    ++mismatchedTarget.resources.front().registerIndex;
    std::string targetError;
    const bool targetMismatchRejected = !AreShaderReflectionsEquivalent(
        dxilStages.front(), mismatchedTarget, targetError);
    if (!typeMismatchRejected || !targetMismatchRejected)
    {
        outLog += "[shader reflection] schema/target mismatch 거부 계약 불일치\n";
        return false;
    }

    outLog += "[shader reflection] Slang DXIL/SPIR-V "
        + std::to_string(requests.size())
        + " stages 동등 · b2/32B · tint@0 · roughness@16 · albedoMap@t3 통과\n";
    return true;
}
