#include "Experiment/Cooked/CookedCodeMaterial.h"
#include "Experiment/Cooked/MaterialAssetSetProducer.h"
#include "Experiment/Cooked/CookedShaderMeta.h"
#include "Experiment/MaterialAuthoringCodec.h"
#include "ShaderPermutationDomain.h"
#include "RHI/ModelVertexInputLayout.h"
#include "AuthoringCookedDocument.h"
#include "AuthoringParsedDocument.h"

#include <algorithm>
#include <map>
#include <string>
#include <tuple>

namespace RenderTest
{
    namespace
    {
        namespace ck = experiment::cooked;
        struct CodeMaterialCheck final
        {
            std::string& log;
            std::size_t passed{}, failed{};
            void Check(bool value, const char* label)
            {
                if (value)
                {
                    ++passed;
                }
                else { ++failed; log += std::string("    [failed] ") + label + "\n"; }
            }
        };

        experiment::AssetId CodeId(std::uint8_t value)
        {
            experiment::AssetId id;
            id.value.data[6] = 0x40u; id.value.data[8] = 0x80u; id.value.data[15] = value;
            return id;
        }
        std::string CodeIdText(std::uint8_t value) { return FileGuid{CodeId(value).value}.ToString(); }
        std::span<const std::byte> CodeBytes(const std::string& value)
        { return std::as_bytes(std::span(value.data(), value.size())); }

        struct CodeFixture final
        {
            std::map<std::string, std::string> files;
            std::vector<ck::CodeProgramCapturedInput> captures;
            ck::CodeProgram program;
            std::vector<std::byte> bytes;
            std::vector<ck::AssetDependency> dependencies;
            std::string recipe, recipeMeta, materialText, materialMeta;

            bool Prepare(std::string& failure)
            {
                program.programAssetId = CodeId(2);
                program.shaderMetaAssetId = CodeId(3);
                program.compilerStamp = std::string(64u, 'a');
                program.metadataPath = "Shaders/probe.shadermeta";
                program.rootSourcePath = "Shaders/probe.hlsl";
                files[program.metadataPath] =
                    "schema: 1\nname: CodeProbe\nsource: probe.hlsl\n"
                    "properties:\n"
                    "  - {name: roughness, type: float, default: 0.5}\n"
                    "  - {name: colorMap, type: texture2d, default: " + CodeIdText(4) + "}\n"
                    "keywords: []\npasses:\n"
                    "  - name: GBuffer\n    vs: {entry: VSMain}\n    ps: {entry: PSMain}\n    queue: opaque\n";
                files[program.metadataPath + ".meta"] = "guid: " + CodeIdText(3) + "\n";
                files[program.rootSourcePath] = "#include \"common.hlsl\"\n// SYNTHETIC_SOURCE_MUST_NOT_SHIP\n";
                files[program.rootSourcePath + ".meta"] = "guid: " + CodeIdText(5) + "\n";
                files["Shaders/common.hlsl"] = "// synthetic captured include\n";
                for (const auto& [path, text] : files)
                {
                    ck::CodeProgramInput input{path, text.size(), {}};
                    if (!ck::ComputeSha256(CodeBytes(text), input.sha256, failure))
                    {
                        return false;
                    }
                    program.inputs.push_back(input);
                    captures.push_back({path, CodeBytes(text)});
                }
                const auto parsed = Authoring::ParsedDocument::ParseText(files.at(program.metadataPath), failure);
                if (!parsed || !Authoring::EncodeCookedDocument(parsed.Root(), program.metadataBytes, failure) ||
                    !ck::ReadShaderMetaArtifact(program.metadataBytes, program.shaderMetaAssetId, program.meta, failure))
                {
                    return false;
                }
                std::vector<std::uint32_t> masks{0u};
                masks.insert(masks.end(), assets::kModelVertexMasks.begin(), assets::kModelVertexMasks.end());
                std::ranges::sort(masks);
                for (const auto backend : {RHIShaderBinary::Dxil, RHIShaderBinary::SpirV})
                {
                    for (const auto mask : masks)
                    {
                        ck::CodeProgramVariant variant;
                        variant.backend = backend; variant.vertexAttributeMask = mask;
                        ShaderMetaPermutation selected;
                        if (!ShaderPermutationDomain::Resolve(program.meta, 0u, {}, selected, failure))
                        {
                            return false;
                        }
                        variant.permutation = selected.defines;
                        if (mask && !ModelVertexInput::ApplyShaderPermutation(mask, variant.permutation, failure))
                        {
                            return false;
                        }
                        for (const auto stageKind : {RHIShaderStage::Vertex, RHIShaderStage::Pixel})
                        {
                            ck::CodeProgramStage stage;
                            stage.stage = stageKind;
                            stage.entry = stageKind == RHIShaderStage::Vertex ? "VSMain" : "PSMain";
                            stage.profile = stageKind == RHIShaderStage::Vertex ? "vs_6_0" : "ps_6_0";
                            // Value-codec fixture only: never a compilable or executable shader.
                            const std::uint8_t payload[]{1u, 2u, 3u, 4u};
                            stage.bytecode.Assign(payload, sizeof(payload));
                            stage.reflection.stage = stageKind;
                            RHIShaderResourceReflection buffer;
                            buffer.name = "MaterialConstants"; buffer.kind = RHIShaderResourceKind::ConstantBuffer;
                            buffer.byteSize = 16u;
                            buffer.fields.push_back({"roughness", {RHIShaderScalarKind::Float32, 1, 1, 1}, 0u, 4u});
                            stage.reflection.resources.push_back(buffer);
                            RHIShaderResourceReflection texture;
                            texture.name = "colorMap"; texture.kind = RHIShaderResourceKind::Texture;
                            stage.reflection.resources.push_back(texture);
                            variant.stages.push_back(std::move(stage));
                        }
                        program.variants.push_back(std::move(variant));
                    }
                }
                recipe = "code_program:\n  schema: 1\n  shaderMetaAssetId: " + CodeIdText(3) +
                    "\n  metadata: " + program.metadataPath + "\n  source: " + program.rootSourcePath +
                    "\n  compilerSha256: " + program.compilerStamp + "\n";
                recipeMeta = "guid: " + CodeIdText(2) + "\n";
                materialMeta = "guid: " + CodeIdText(1) + "\n";
                experiment::Material material;
                material.assetId = CodeId(1); material.shaderAssetId = CodeId(3); material.name = "AuthoredProbe";
                material.properties.push_back({"roughness", 0.25f});
                material.properties.push_back({"doubleSided", true});
                Authoring::WriteDocument source;
                if (!experiment::SerializeMaterialAuthoring(material, source.Root(), failure))
                {
                    return false;
                }
                materialText = source.Root().Dump();
                return ck::EncodeCodeProgramArtifact(program, bytes, dependencies, failure);
            }
        };
    }

    // Synthetic source regression only. No compiler, device, file I/O, AOT,
    // player/editor initialization or executable shader is used by this test.
    bool RunExperimentCodeMaterialCodecSelfTest(std::string& log)
    {
        CodeMaterialCheck check{log};
        CodeFixture fixture;
        std::string failure;
        if (!fixture.Prepare(failure))
        {
            check.Check(false, "prepare complete code-program fixture");
            log += failure + "\n";
            return false;
        }
        const ck::TypedAssetReference programRef{{CodeId(2), {}}, ck::CookedAssetKind::MaterialProgram};
        ck::CodeProgram decoded;
        check.Check(ck::ReadCodeProgramArtifact(fixture.bytes, programRef, fixture.dependencies, decoded, failure),
            "source-free complete DXIL/SPIR-V and vertex-mask program round trip");
        check.Check(decoded.variants.size() == 18u && decoded.layout.properties.size() == 2u,
            "reflection-derived immutable layout and complete variants survive");
        check.Check(ck::CodeProgramRetainedBytes(decoded) >= fixture.bytes.size(), "owned code capacities charged");
        auto truncated = fixture.bytes; truncated.pop_back();
        decoded.programAssetId = CodeId(99);
        check.Check(!ck::ReadCodeProgramArtifact(truncated, programRef, fixture.dependencies, decoded, failure) &&
            decoded.programAssetId == CodeId(99), "truncated read preserves prior output");
        auto trailing = fixture.bytes; trailing.push_back(std::byte{});
        check.Check(!ck::ReadCodeProgramArtifact(trailing, programRef, fixture.dependencies, decoded, failure), "trailing bytes rejected");
        auto edges = fixture.dependencies; edges[0].kind = ck::AssetDependencyKind::Loadable;
        check.Check(!ck::ReadCodeProgramArtifact(fixture.bytes, programRef, edges, decoded, failure), "program soft metadata/default edge rejected");
        edges = fixture.dependencies; edges[0].target.kind = ck::CookedAssetKind::Material;
        check.Check(!ck::ReadCodeProgramArtifact(fixture.bytes, programRef, edges, decoded, failure), "program wrong typed edge rejected");
        edges = fixture.dependencies;
        for (auto& edge : edges)
        {
            edge.scope = ck::AssetDependencyScope::External;
        }
        check.Check(ck::ReadCodeProgramArtifact(fixture.bytes, programRef, edges, decoded, failure), "explicit external exact hard edges accepted");
        std::vector<std::byte> rejected;
        auto invalid = fixture.program; invalid.variants.pop_back();
        check.Check(!ck::EncodeCodeProgramArtifact(invalid, rejected, edges, failure), "incomplete backend/mask coverage rejected");
        invalid = fixture.program; invalid.variants[0].stages[0].profile = "ps_6_0";
        check.Check(!ck::EncodeCodeProgramArtifact(invalid, rejected, edges, failure), "incorrect stage profile rejected");
        invalid = fixture.program; invalid.abiStamp += "-wrong";
        check.Check(!ck::EncodeCodeProgramArtifact(invalid, rejected, edges, failure), "unsupported code ABI rejected");
        invalid = fixture.program; invalid.compilerStamp.clear();
        check.Check(!ck::EncodeCodeProgramArtifact(invalid, rejected, edges, failure), "missing compiler provenance rejected");
        invalid = fixture.program; invalid.variants[1].stages[0].reflection.resources[0].fields[0].byteOffset = 4u;
        check.Check(!ck::EncodeCodeProgramArtifact(invalid, rejected, edges, failure), "incompatible variant reflection rejected");

        invalid = fixture.program;
        for (auto& variant : invalid.variants)
        {
            for (auto& stage : variant.stages)
            {
                stage.reflection.resources[0].byteSize = 65537u;
            }
        }
        check.Check(!ck::EncodeCodeProgramArtifact(invalid, rejected, edges, failure) &&
            failure.find("LX runtime") != std::string::npos, "oversized common layout rejected by shared runtime invariant");
        const auto addMetadataProperty = [&](ck::CodeProgram& program, const std::string& property)
        {
            auto text = fixture.files.at(fixture.program.metadataPath);
            text.insert(text.find("keywords:"), "  - " + property + "\n");
            const auto parsed = Authoring::ParsedDocument::ParseText(text, failure);
            return parsed && Authoring::EncodeCookedDocument(parsed.Root(), program.metadataBytes, failure);
        };
        invalid = fixture.program;
        const bool overlapMetadata = addMetadataProperty(invalid, "{name: metallic, type: float, default: 0.0}");
        for (auto& variant : invalid.variants)
        {
            for (auto& stage : variant.stages)
            {
                stage.reflection.resources[0].fields.push_back(
                    {"metallic", {RHIShaderScalarKind::Float32, 1, 1, 1}, 0u, 4u});
            }
        }
        check.Check(overlapMetadata && !ck::EncodeCodeProgramArtifact(invalid, rejected, edges, failure) &&
            failure.find("overlap") != std::string::npos, "overlapping common numeric layout rejected before publication");
        invalid = fixture.program;
        const bool aliasMetadata = addMetadataProperty(invalid, "{name: otherMap, type: texture2d}");
        for (auto& variant : invalid.variants)
        {
            for (auto& stage : variant.stages)
            {
                auto texture = stage.reflection.resources[1];
                texture.name = "otherMap";
                stage.reflection.resources.push_back(std::move(texture));
            }
        }
        check.Check(aliasMetadata && !ck::EncodeCodeProgramArtifact(invalid, rejected, edges, failure) &&
            failure.find("texture binding") != std::string::npos, "aliased texture registers rejected before publication");

        const auto cookProgram = [&]()
        {
            return ck::BuildMaterialProgramAssetSetProduct({CodeId(2), CodeBytes(fixture.recipe),
                CodeBytes(fixture.recipeMeta), fixture.bytes, {}, fixture.captures});
        };
        check.Check(cookProgram().Succeeded(), "explicit code recipe validates full captured source/include/sidecar closure");
        fixture.captures.back().bytes = {};
        check.Check(!cookProgram().Succeeded(), "changed captured input rejected before publication");
        fixture.captures.back().bytes = CodeBytes(fixture.files.at(fixture.captures.back().path));
        const auto originalRecipe = fixture.recipe;
        fixture.recipe.replace(fixture.recipe.find(fixture.program.compilerStamp), 64u, std::string(64u, 'b'));
        check.Check(!cookProgram().Succeeded(), "recipe compiler stamp mismatch rejected");
        fixture.recipe = originalRecipe;
        const auto verifyChangedSource = [&](std::string text)
        {
            auto changed = fixture.program;
            auto captures = fixture.captures;
            for (std::size_t i = 0; i < changed.inputs.size(); ++i)
            {
                if (changed.inputs[i].path == changed.rootSourcePath)
                {
                    changed.inputs[i].byteSize = text.size();
                    if (!ck::ComputeSha256(CodeBytes(text), changed.inputs[i].sha256, failure))
                    {
                        return false;
                    }
                    captures[i].bytes = CodeBytes(text);
                }
            }
            std::vector<std::byte> changedBundle;
            std::vector<ck::AssetDependency> changedEdges;
            if (!ck::EncodeCodeProgramArtifact(changed, changedBundle, changedEdges, failure))
            {
                return false;
            }
            return ck::BuildMaterialProgramAssetSetProduct({CodeId(2), CodeBytes(fixture.recipe),
                CodeBytes(fixture.recipeMeta), changedBundle, {}, captures}).Succeeded();
        };
        check.Check(!verifyChangedSource("#include \"missing.hlsl\"\n"), "claimed bundle cannot omit literal include closure");
        check.Check(!verifyChangedSource("#define SOURCE \"common.hlsl\"\n#include SOURCE\n"), "macro include closure is rejected explicitly");
        check.Check(!verifyChangedSource("// no includes\n"), "unreachable source inventory is rejected");
        check.Check(!verifyChangedSource("import\nOther;\n"), "multiline module import is rejected");
        auto material = ck::BuildMaterialAssetSetProduct({CodeId(1), CodeBytes(fixture.materialText),
            CodeBytes(fixture.materialMeta), CodeId(2), {}, fixture.bytes, fixture.captures});
        check.Check(material.Succeeded(), "existing flat schema1 material cooks through explicit code program");
        if (material.Succeeded())
        {
            const auto& product = *material.product;
            ck::AuthoredMaterialDocument document;
            check.Check(product.representation == ck::kAuthoredMaterialRepresentation && product.dependencies.size() == 3u &&
                ck::ReadAuthoredMaterialArtifact(product.artifactBytes, product.asset, product.dependencies, document, failure),
                "authored document preserves exact code/metadata/default texture hard closure");
            check.Check(ck::ValidateAuthoredMaterialBinding(document, decoded, failure), "decoded authored/code binding validates");
            document.defaultTextureAssetIds.clear();
            check.Check(!ck::ValidateAuthoredMaterialBinding(document, decoded, failure), "omitted default texture closure rejected");
            document.defaultTextureAssetIds = {CodeId(4)};
            document.material.properties.push_back({"unknown", 1.0f});
            check.Check(!ck::ValidateAuthoredMaterialBinding(document, decoded, failure), "unknown authored property rejected");
            document.material.properties.pop_back();
            document.material.properties.back().value = std::string("wrong-raster-type");
            check.Check(!ck::ValidateAuthoredMaterialBinding(document, decoded, failure), "wrong doubleSided raster policy type rejected");
            document.material.properties.back().value = true;
            document.material.properties[0].value = std::string("wrong");
            check.Check(!ck::ValidateAuthoredMaterialBinding(document, decoded, failure), "wrong authored property type rejected");
        }
        check.Check(!ck::BuildMaterialAssetSetProduct({CodeId(1), CodeBytes(fixture.materialText),
            CodeBytes(fixture.materialMeta)}).Succeeded(), "authored code material cannot silently use metadata-only readiness");
        log += "  Code material codec: " + std::to_string(check.passed) + " passed, " + std::to_string(check.failed) + " failed\n";
        return check.failed == 0u;
    }
}
