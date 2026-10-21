#pragma once
#include "RHI/RHIShaderCompiler.h"
#include "RHI/RHIShaderSource.h"
#include <chrono>
#include <cstdio>

namespace material_graph
{
// Compile the fixed scene helpers while the editor's loading window owns input.
// These use the same verification/options/permutations as their runtime consumers;
// no model, transient scene object, GPU draw or original material is manufactured.
inline bool WarmupModelShaders(std::string& error)
{
    const auto started=std::chrono::steady_clock::now();
    const auto backend=RHIShaderCompiler::GetOutput();
    unsigned requests=0;
    const auto compile=[&](const char* source,const char* entry,const char* profile,
                           bool strict=true,bool atomics=false,
                           std::initializer_list<const char*> defines={},
                           std::optional<RHIShaderBinary> target={}) {
        RHIShaderCompileOptions options; options.strictMath=strict; options.bufferInt64Atomics=atomics;
        RHIShaderPermutation permutation;
        for (const auto* define:defines) if (!permutation.Enable(define,error)) return false;
        RHIShaderCompiler::VerifiedShader shader;
        ++requests;
        return RHIShaderCompiler::VerifyFile(RHIShaderSource::Resolve(source).string(),entry,profile,
            target.value_or(backend),permutation,shader,error,options);
    };
    if (!compile("MaterialGraphMeshSurface.slang","LXTransformMesh","cs_6_0",false)) return false;
    for (const auto target:{RHIShaderBinary::Dxil,RHIShaderBinary::SpirV})
        for (const auto* entry:{"LXSceneLookupBake","LXSceneLookupClear"})
            if (!compile("MaterialGraphSceneLookup.slang",entry,"cs_6_0",true,false,{},target)) return false;
    for (const auto* entry:{"GeometryVisibilityResetCS","GeometryVisibilityCullCS","ShadowMeshletResetCS",
                           "ShadowMeshletCullCS","MeshletCountCS","MeshletFinalizeCS","MeshletScatterCS"})
        if (!compile("GeometryVisibility.slang",entry,"cs_6_0")) return false;
    for (const auto* entry:{"ShadowMeshletResetCS","ShadowMeshletCullCS","MeshletCountCS","MeshletFinalizeCS","MeshletScatterCS"})
        if (!compile("GeometryVisibility.slang",entry,"cs_6_0",true,false,{"GEOMETRY_VISIBILITY_SHARED_VIEWS"})) return false;
    if (!compile("GeometryVisibility.slang","ShadowMeshletCullCS","cs_6_0",true,false,
        {"GEOMETRY_VISIBILITY_SHARED_VIEWS","GEOMETRY_VISIBILITY_OCCLUSION"})) return false;
    if (!compile("GeometryVisibility.slang","ShadowMeshletCullCS","cs_6_0",true,false,{"GEOMETRY_VISIBILITY_OCCLUSION"})) return false;
    for (const auto* define:{"GEOMETRY_VISIBILITY_HISTORY","GEOMETRY_VISIBILITY_RECOVERY","GEOMETRY_VISIBILITY_CAMERA_PREDICTION"})
        if (!compile("GeometryVisibility.slang","ShadowMeshletCullCS","cs_6_0",true,false,{"GEOMETRY_VISIBILITY_OCCLUSION",define})) return false;
    if (!compile("GeometryOcclusionBuild.slang","GeometryOcclusionBuildCS","cs_6_0")) return false;
    for (const auto* entry:{"Initialize","PrepareClassify","Classify","PrepareSoftware","Rasterize"})
        if (!compile("MeteorHybridVisibility.slang",entry,"cs_6_6",true,true)) return false;
    for (const auto* entry:{"ResolveVS","ResolvePS","ResolveDepthPS"})
        if (!compile("MeteorHybridVisibility.slang",entry, std::string_view(entry)=="ResolveVS" ? "vs_6_0" : "ps_6_0",true,true)) return false;
    for (const auto* entry:{"LXBuildIndirectCommands","LXResetPacketBuckets","LXCountPacketBuckets","LXScanPacketRanges",
                           "LXAddPacketRangeOffsets","LXBuildPacketArguments","LXScatterPackets"})
        if (!compile("MaterialGraphIndirect.slang",entry,"cs_6_0")) return false;
    for (const auto* entry:{"ResetMaterialWork","CountMaterialTiles","ScanMaterialRanges","AddMaterialRangeOffsets","BuildMaterialRanges","ScatterMaterialTiles"})
        if (!compile("MaterialGraphWork.slang",entry,"cs_6_0")) return false;
    if (!compile("MeteorShadowDepth.slang","ShadowDepthVS","vs_6_0") ||
        !compile("MeteorShadowDepth.slang","ShadowDepthPS","ps_6_0")) return false;
    std::printf("[model.warmup] ready requests=%u totalMs=%.3f\n", requests,
        std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count());
    std::fflush(stdout);
    return true;
}
}
