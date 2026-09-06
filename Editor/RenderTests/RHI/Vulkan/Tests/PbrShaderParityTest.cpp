#include "../VulkanSelfTest.h"
#include "RHI/Vulkan/VulkanDeviceResources.h"
#include "RHI/Vulkan/VulkanPipelineCache.h"
#include "RHI/DX12/DX12DeviceResources.h"
#include "RHI/DX12/DX12PSOManager.h"
#include "RHI/DX12/DX12RootSignatureCache.h"
#include "RHI/DX12/DX12MeshCache.h"
#include "RHI/DX12/DX12TextureCache.h"
#include "RHI/RHIShaderCompiler.h"
#include "Render/Passes/Geometry/EnhancedGBufferPass.h"
#include "Render/Passes/Geometry/EnhancedDeferredPass.h"
#include "Render/Passes/Geometry/EnhancedForwardPass.h"
#include "Render/Graph/EnhancedRenderGraph.h"
#include "FrameCameraSnapshot.h"
#include "Mesh.h"
#include "Texture.h"
#include "Experiment/Import/GltfImporter.h"
#include "Experiment/Import/SceneToModelDraft.h"
#include "Render/Scene/ExperimentMaterialSealing.h"
#include "Render/Passes/Geometry/EnhancedShadowPass.h"
#include <mathematics/transform.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <functional>
#include <fstream>
#include <chrono>
#include <memory>
#include <vector>

namespace
{
    constexpr uint32_t kPbrSize = 32;
    struct PbrParityCapture
    {
        std::vector<float> rgb;
        float maxRouteDelta{};
        uint32_t cases{};
    };

    // Generate two alternate shaders from the actual product sources. They use
    // six textures over eight registers, shuffled semantics and two holes.
    struct PbrAoFixture
    {
        std::array<ShaderMeta, 4> metas;
        std::array<ShaderMetaHandle, 4> handles{{{801, 1}, {802, 1}, {803, 1}, {804, 1}}};
        std::array<std::shared_ptr<const ShaderMetaBindingLayout>, 4> layouts;
        std::array<RHIShaderPermutationKey, 4> permutations;
        std::vector<uint16_t> keywords{0};
        std::array<std::filesystem::path, 2> files;
        std::array<std::shared_ptr<Texture>, 4> textures;
        ~PbrAoFixture()
        {
            for (const auto& path : files) if (!path.empty())
            { std::error_code ignored; std::filesystem::remove(path, ignored); }
        }
        bool Initialize(EnhancedGBufferPass& gb, EnhancedForwardPass& fw,
            const EnhancedFrameContext& context, std::string& error)
        {
            const std::filesystem::path root = "Dynamic_CPP/Assets/Shaders/DefaultPassShader";
            for (uint32_t route = 0; route < 2; ++route)
            {
                const auto name = route == 0 ? "GBuffer" : "Forward";
                if (!ShaderMetaLoader::LoadFile(root / (std::string(name) + ".shadermeta"),
                        FileGuid("10000000-0000-4000-8000-000000000801"), metas[route], error)) return false;
                std::ifstream input(root / metas[route].source, std::ios::binary);
                std::string source((std::istreambuf_iterator<char>(input)), {});
                const auto replace = [&](std::string_view from, std::string_view to) {
                    const auto at = source.find(from);
                    if (at == std::string::npos) return false;
                    source.replace(at, from.size(), to); return true;
                };
                if (!replace("register(t16)", "register(t22)")
                    || !replace("register(t19)", "register(t16)")
                    || !replace("register(t20)", "register(t23)"))
                { error = "AO test source register markers missing"; return false; }
                if (!replace("MaterialUv(16,", "MaterialUv(22,")
                    || !replace("MaterialUv(19,", "MaterialUv(16,")
                    || !replace("MaterialUv(20,", "MaterialUv(23,")) return false;
                const auto uv = route == 0 ? "MaterialUv(23, input.uv, input.uv1)"
                    : "MaterialUv(23, resolvedUv, resolvedUv1)";
                const std::string sample = "aoMap.Sample(gSampler, " + std::string(uv) + ").r";
                if (!replace(sample, "(" + sample + " * aoDetailMap.Sample(gSampler, " + uv + ").r)"))
                { error = "AO test source sample marker missing"; return false; }
                source = "Texture2D aoDetailMap : register(t20);\n" + source;
                files[route] = root / ("PbrAoFixture." + std::string(name) + "."
                    + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".slang");
                std::ofstream output(files[route], std::ios::binary);
                output << source; output.close();
                if (!output) { error = "AO test shader write failed"; return false; }
                auto& extended = metas[route + 2];
                extended = metas[route]; extended.source = files[route].filename();
                extended.properties.push_back({"aoDetailMap", "AO detail", ShaderPropertyType::Texture2D, {}});
                std::reverse(extended.properties.begin(), extended.properties.end());
            }
            for (uint32_t i = 0; i < metas.size(); ++i)
            {
                const bool ready = i % 2 == 0
                    ? gb.EnsureShaderMetaVariant(context, handles[i], metas[i], keywords, permutations[i], layouts[i], error)
                    : fw.EnsureShaderMetaVariant(context, handles[i], metas[i], keywords, permutations[i], layouts[i], error);
                if (!ready) return false;
                MaterialTextureTable::Schema schema;
                if (!MaterialTextureTable::FromLayout(*layouts[i], schema, error)
                    || schema.size() != (i < 2 ? 5u : 8u)) return false;
            }
            // Bad ShaderMeta must not publish over a working generation.
            for (uint32_t i = 0; i < 2; ++i)
            {
                auto missing = metas[i];
                std::erase_if(missing.properties, [](const auto& p) { return p.name == "aoMap"; });
                std::string rejected;
                RHIShaderPermutationKey unused;
                std::shared_ptr<const ShaderMetaBindingLayout> layout;
                const auto old = i == 0 ? gb.GetShaderVariantPipeline(handles[i], permutations[i])
                    : fw.GetShaderVariantPipeline(handles[i], permutations[i], keywords, true);
                const bool accepted = i == 0
                    ? gb.EnsureShaderMetaVariant(context, {handles[i].slot, 2}, missing, keywords, unused, layout, rejected)
                    : fw.EnsureShaderMetaVariant(context, {handles[i].slot, 2}, missing, keywords, unused, layout, rejected);
                const auto retained = i == 0 ? gb.GetShaderVariantPipeline(handles[i], permutations[i])
                    : fw.GetShaderVariantPipeline(handles[i], permutations[i], keywords, true);
                if (accepted || rejected.empty() || !old.IsValid() || old != retained)
                { error = "AO invalid schema replaced the valid variant"; return false; }
            }
            const uint8_t pixels[4][4] = {
                {255, 255, 255, 255}, {0, 255, 255, 255}, {64, 0, 255, 255}, {0, 0, 0, 255}};
            for (uint32_t i = 0; i < textures.size(); ++i)
            {
                textures[i].reset(Texture::CreateFromPixels(1, 1, "PbrAo." + std::to_string(i),
                    RHIFormat::RGBA8Unorm, pixels[i], 4));
                if (!textures[i]) { error = "AO texture creation failed"; return false; }
            }
            return true;
        }
        static float Expected(uint32_t index)
        { return index == 3 ? .5f : index == 4 || index == 6 ? 0.f : index == 5 ? 64.f / 255.f : 1.f; }
        bool Prepare(uint32_t index, std::vector<EnhancedDrawItem>& draws, std::string& error)
        {
            using namespace ExperimentMaterialSealing;
            SealSource source;
            source.material.properties = {
                {"baseColor", math::vector4{.25f, .5f, .75f, 1.f}},
                {"metallic", .5f}, {"roughness", .5f},
                {"occlusionStrength", index == 2 ? 0.f : index == 3 ? .5f : 1.f},
                {"emissive", math::vector3{.125f, .125f, .125f}}};
            const auto ao = index == 0 || index == 7 ? nullptr
                : index == 1 ? textures[0] : index == 5 ? textures[2] : textures[1];
            source.textures = {{"aoMap", ao}, {"ormMap", index >= 6 ? textures[1] : textures[0]},
                {"emissiveMap", textures[0]}, {"aoDetailMap", textures[0]}};
            const auto draw = draws.front();
            draws.assign(3, draw);
            for (uint32_t band = 0; band < draws.size(); ++band)
            {
                draws[band].worldMatrix = math::scaling_matrix(math::vector3{1.f / 3.f, 1.f, 1.f})
                    * math::translation_matrix(math::vector3{(static_cast<float>(band) - 1.f) * .5f, 0.f, 0.f});
                const uint32_t first = band == 1 ? 2 : 0;
                auto gb = std::make_shared<EnhancedMaterialDrawSnapshot>();
                auto fw = std::make_shared<EnhancedForwardMaterialDrawSnapshot>();
                const auto seal = [&](auto& packet, uint32_t route) {
                    packet.shaderMetaHandle = handles[route]; packet.permutationKey = permutations[route];
                    packet.keywordSelections = keywords; packet.bindingLayout = *layouts[route];
                    if (!SealCore(source, metas[route], *layouts[route], packet.propertyBytes,
                            packet.textureBindings, error) || !packet.IsValid()) return false;
                    // Owner input order is deliberately unrelated to register order.
                    std::reverse(packet.textureBindings.begin(), packet.textureBindings.end());
                    if (!MaterialTextureTable::ValidateSnapshot(packet, error)) return false;
                    for (uint32_t corrupt = 0; corrupt < 5; ++corrupt)
                    {
                        auto bad = packet;
                        if (corrupt == 0) bad.textureBindings.pop_back();
                        if (corrupt == 1) bad.textureBindings.push_back(bad.textureBindings.front());
                        if (corrupt == 2) bad.textureBindings.front().registerIndex = 128;
                        if (corrupt == 3) bad.textureBindings.front().registerSpace = 1;
                        if (corrupt == 4) bad.textureBindings.front().propertyName = "wrongOwner";
                        std::string rejected;
                        if (MaterialTextureTable::ValidateSnapshot(bad, rejected) || rejected.empty())
                        { error = "AO table accepted a corrupt owner schema"; return false; }
                    }
                    return true;
                };
                if (!seal(*gb, first) || !seal(*fw, first + 1)) return false;
                draws[band].materialSnapshot = gb; draws[band].forwardMaterialSnapshot = fw;
                // Legacy aliases must not override the owning material.
                draws[band].occRoughMetal = textures[3].get();
                draws[band].emissive = textures[3].get();
                draws[band].baseColorFactor = math::color(0, 0, 0, 1);
            }
            return true;
        }
    };
    struct PbrEmissionFixture
    {
        std::array<std::shared_ptr<Texture>, 5> textures;
        bool Initialize(std::string& error)
        {
            const uint8_t gray[] = {128, 64, 192, 255};
            textures[0].reset(Texture::CreateFromPixels(1, 1, "Emission.encoded",
                RHIFormat::RGBA8Unorm, gray, 4));
            textures[1] = Texture::WithColorSpace(textures[0], true);
            textures[2] = Texture::WithColorSpace(textures[1], false);
            const uint8_t black[] = {0, 0, 0, 255};
            textures[3].reset(Texture::CreateFromPixels(1, 1, "Emission.black",
                RHIFormat::RGBA8Unorm, black, 4));
            const uint8_t redBlock[16] = {255, 255, 0, 0, 0, 0, 0, 0, 0, 248, 0, 248, 0, 0, 0, 0};
            auto bc3 = std::shared_ptr<Texture>(Texture::CreateFromPixels(4, 4, "Emission.BC3",
                RHIFormat::BC3Unorm, redBlock, 16));
            textures[4] = Texture::WithColorSpace(bc3, true);
            if (!textures[4] || textures[4]->GetImageView().Format() != RHIFormat::BC3UnormSrgb)
            { error = "Emission BC3 sRGB format failed"; return false; }
            if (!textures[0] || !textures[1] || !textures[2] || !textures[3]
                || textures[0]->m_assetId == textures[1]->m_assetId
                || textures[0]->GetImageView().Format() != RHIFormat::RGBA8Unorm
                || textures[1]->GetImageView().Format() != RHIFormat::RGBA8UnormSrgb
                || textures[2]->GetImageView().Format() != RHIFormat::RGBA8Unorm
                || textures[0]->GetImageView().At(0)->pixels != textures[1]->GetImageView().At(0)->pixels
                || Texture::WithColorSpace(textures[1], true) != textures[1])
            { error = "Emission color-space owner isolation failed"; return false; }
            return true;
        }
        static float Expected(uint32_t index, uint32_t c)
        {
            if (index == 12) return c == 0 ? .25f : 0.f;
            if (index == 0 || index == 1 || index == 4 || index == 5) return 0.f;
            const float factors[] = {.25f, .5f, .75f};
            const float encoded[] = {128.f / 255.f, 64.f / 255.f, 192.f / 255.f};
            float sample = index >= 7 && index <= 10 ? encoded[c] : 1.f;
            if (index == 8 || index == 10)
                sample = sample <= .04045f ? sample / 12.92f : std::pow((sample + .055f) / 1.055f, 2.4f);
            const float strength = index == 3 || index == 10 ? 8.f : index == 11 ? 32.f : 1.f;
            return (index == 11 && c == 1 ? 0.f : factors[c]) * sample * strength;
        }
        bool Prepare(PbrAoFixture& fixture, uint32_t index,
            std::vector<EnhancedDrawItem>& draws, std::string& error)
        {
            // AO=0 deliberately: it must never multiply emission.
            if (!fixture.Prepare(4, draws, error)) return false;
            ExperimentMaterialSealing::SealSource source;
            source.material.properties = {{"baseColor", math::vector4{.25f, .5f, .75f, 1.f}},
                {"metallic", .5f}, {"roughness", .5f}, {"occlusionStrength", 1.f}};
            if (index >= 2) source.material.properties.push_back({"emissive",
                math::vector3{.25f, index == 11 ? 0.f : .5f, .75f}});
            if (index == 3 || index == 4 || index == 10 || index == 11)
                source.material.properties.push_back({"emissiveStrength", index == 4 ? 0.f : index == 11 ? 32.f : 8.f});
            const auto texture = index == 1 || index == 6 ? fixture.textures[0]
                : index == 5 ? textures[3] : index == 7 ? textures[0]
                : index == 8 || index == 10 ? textures[1] : index == 9 ? textures[2] : index == 12 ? textures[4] : nullptr;
            source.textures = {{"aoMap", fixture.textures[1]}, {"emissiveMap", texture},
                {"aoDetailMap", fixture.textures[0]}};
            for (uint32_t band = 0; band < draws.size(); ++band)
            {
                const uint32_t first = band == 1 ? 2 : 0;
                auto gb = std::make_shared<EnhancedMaterialDrawSnapshot>(*draws[band].materialSnapshot);
                auto fw = std::make_shared<EnhancedForwardMaterialDrawSnapshot>(*draws[band].forwardMaterialSnapshot);
                const auto seal = [&](auto& packet, uint32_t route) {
                    return ExperimentMaterialSealing::SealCore(source, fixture.metas[route],
                        *fixture.layouts[route], packet.propertyBytes, packet.textureBindings, error)
                        && packet.IsValid();
                };
                if (!seal(*gb, first) || !seal(*fw, first + 1)) return false;
                draws[band].materialSnapshot = gb; draws[band].forwardMaterialSnapshot = fw;
            }
            return true;
        }
    };
    struct PbrTransformFixture
    {
        struct Transform { float x, y, z, shear; bool singular; };
        static constexpr std::array<Transform, 8> transforms{{
            {1, 1, 1, 0}, {1, 1, 2, 0}, {2, 1, 1, 0}, {1, 1, 1, 1},
            {1, 1, -2, 0}, {1e-5f, 1e-5f, 1e-5f, 0},
            {1, 1, 0, 0, true}, {1, 1, 1e-10f, 0, true}}};
        static constexpr uint32_t kCases = transforms.size() * 2 * 4;
        struct PackedSkinVertex { std::array<float, 12> core; uint8_t joints[4]; float weights[4]; };
        static_assert(sizeof(PackedSkinVertex) == assets::StrideOf(
            assets::kCoreVertexAttributes | assets::kSkinVertexAttributes));
        std::array<std::vector<std::byte>, kCases> vertices;
        const std::array<uint32_t, 6> indices{0, 1, 2, 0, 2, 3};
        std::array<math::matrix4x4, 2> bones;
        std::shared_ptr<Texture> normalMap;
        math::vector3 expected;

        bool Initialize(std::string& error)
        {
            // Weighted palette is diag(1,1,-2); reflection must preserve authored B.
            bones[0] = bones[1] = math::matrix4x4::identity();
            bones[0](2, 2) = -1.f; bones[1](2, 2) = -3.f;
            const float texel[] = {.65f, .7f, (std::sqrt(.75f) + 1.f) * .5f, 1.f};
            normalMap.reset(Texture::CreateFromPixels(1, 1, "Pbr.Transform.Normal",
                RHIFormat::RGBA32Float, texel, sizeof(texel)));
            if (!normalMap) { error = "Transform normal texture creation failed"; return false; }
            for (uint32_t index = 0; index < kCases; ++index)
            {
                const auto& matrix = transforms[index / 8];
                const bool skin = (index / 4) % 2 != 0;
                const uint32_t variant = index % 4;
                const auto stride = assets::StrideOf(assets::kCoreVertexAttributes
                    | (skin ? assets::kSkinVertexAttributes : 0));
                auto& bytes = vertices[index]; bytes.resize(4 * stride);
                for (uint32_t v = 0; v < 4; ++v)
                {
                    // Compensate XY scale to keep the same raster footprint. Local
                    // z=0 makes shear/skin affect the normal without moving the plane.
                    PackedSkinVertex vertex{{
                        (v < 2 ? -.75f : .75f) / matrix.x,
                        (v == 0 || v == 3 ? -.75f : .75f) / matrix.y, 0.f,
                        .6f, 0.f, .8f, .5f, .5f,
                        variant == 3 ? 0.f : .8f, 0.f, variant == 3 ? 0.f : -.6f,
                        variant == 2 ? -1.f : 1.f}, {0, 1, 0, 0}, {.5f, .5f, 0, 0}};
                    std::memcpy(bytes.data() + v * stride, &vertex, stride);
                }
            }
            return true;
        }
        void Prepare(uint32_t index, EnhancedDrawItem& draw)
        {
            const auto& matrix = transforms[index / 8];
            const bool skin = (index / 4) % 2 != 0;
            const uint32_t variant = index % 4;
            const float boneZ = skin ? -2.f : 1.f;
            draw.worldMatrix = math::matrix4x4::identity();
            draw.worldMatrix(0, 0) = matrix.x; draw.worldMatrix(1, 1) = matrix.y;
            draw.worldMatrix(2, 2) = matrix.z; draw.worldMatrix(2, 0) = matrix.shear;
            draw.worldMatrix(3, 2) = .5f;
            draw.bonePalette = skin ? bones.data() : nullptr;
            draw.boneCount = skin ? 2 : 0; draw.animatorKey = skin ? 907 : 0;
            draw.useNormalMap = variant != 0; draw.normalMap = normalMap.get();
            draw.metallic = .25f; draw.roughness = .7f;
            draw.geometryKey = 0x57370000ull + index;
            auto& view = draw.modelMeshView;
            view.handle.modelId = Uuid::Parse("10000000-0000-8000-8000-000000000907");
            view.handle.meshId = Uuid::Parse("10000000-0000-8000-8000-000000000908");
            view.handle.generation = index + 1;
            view.vertexData = vertices[index].data(); view.vertexBytes = vertices[index].size();
            view.vertexAttributeMask = assets::kCoreVertexAttributes | (skin ? assets::kSkinVertexAttributes : 0);
            view.vertexStride = assets::StrideOf(view.vertexAttributeMask);
            view.vertexLayoutHash = assets::VertexLayoutHash(view.vertexAttributeMask);
            view.indexData = indices.data(); view.indexCount = indices.size();

            // Analytic plane equation for diagonal scale and z->x shear. This does
            // not use the shader's cofactor/inverse implementation or route parity.
            const float nx = .6f / matrix.x;
            expected = matrix.singular
                ? math::normalize(math::vector3{.6f * matrix.x + .8f * boneZ * matrix.shear,
                    0.f, .8f * boneZ * matrix.z})
                : math::normalize(math::vector3{nx, 0.f, (.8f / boneZ - matrix.shear * nx) / matrix.z});
            if (variant != 0 && variant != 3 && !matrix.singular)
            {
                const math::vector3 transformedT{.8f * matrix.x - .6f * boneZ * matrix.shear,
                    0.f, -.6f * boneZ * matrix.z};
                const auto tangent = math::normalize(transformedT - expected * math::dot(expected, transformedT));
                // Both geometric vectors lie in XZ, so B is exactly the authored
                // +/- Y direction even when bone/world determinants change sign.
                expected = math::normalize(tangent * .3f + math::vector3{0, variant == 2 ? -.4f : .4f, 0}
                    + expected * std::sqrt(.75f));
            }
        }
    };

    bool CheckPbrUvImport(std::string& error)
    {
        namespace im = experiment::importer;
        const auto directory = std::filesystem::temp_directory_path() /
            ("creator-uv-import-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        if (!std::filesystem::create_directory(directory)) { error = "UV fixture directory collision"; return false; }
        struct Cleanup {
            std::filesystem::path directory;
            ~Cleanup() { std::error_code ignored; for (const auto name : {"mesh.bin", "mesh.gltf"})
                std::filesystem::remove(directory / name, ignored); std::filesystem::remove(directory, ignored); }
        } cleanup{directory};
        const float data[] = {-.5f,-.5f,0, .5f,-.5f,0, -.5f,.5f,0,
            0,0,1, 0,0,1, 0,0,1, 0,0, 1,0, 0,1, 0,0, 0,1, 1,0};
        { std::ofstream output(directory / "mesh.bin", std::ios::binary);
          output.write(reinterpret_cast<const char*>(data), sizeof(data)); if (!output) return false; }
        { std::ofstream output(directory / "mesh.gltf"); output << R"json({
          "asset":{"version":"2.0"},
          "extensionsUsed":["KHR_texture_transform","KHR_materials_emissive_strength"],
          "extensionsRequired":["KHR_texture_transform"],
          "buffers":[{"uri":"mesh.bin","byteLength":120}],
          "bufferViews":[{"buffer":0,"byteOffset":0,"byteLength":36},
            {"buffer":0,"byteOffset":36,"byteLength":36},{"buffer":0,"byteOffset":72,"byteLength":24},
            {"buffer":0,"byteOffset":96,"byteLength":24}],
          "accessors":[{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3"},
            {"bufferView":1,"componentType":5126,"count":3,"type":"VEC3"},
            {"bufferView":2,"componentType":5126,"count":3,"type":"VEC2"},
            {"bufferView":3,"componentType":5126,"count":3,"type":"VEC2"}],
          "images":[{"uri":"fixture.png"}],"textures":[{"source":0}],
          "materials":[{"pbrMetallicRoughness":{"baseColorTexture":{"index":0,"texCoord":0,
            "extensions":{"KHR_texture_transform":{"texCoord":1,"offset":[0.25,-0.5],"scale":[-2,0.75],"rotation":0.4}}}},
            "normalTexture":{"index":0,"texCoord":1},"emissiveFactor":[1,1,1],
            "extensions":{"KHR_materials_emissive_strength":{"emissiveStrength":8}}}],
          "meshes":[{"primitives":[{"attributes":{"POSITION":0,"NORMAL":1,"TEXCOORD_0":2,"TEXCOORD_1":3},"material":0}]}],
          "nodes":[{"mesh":0}],"scenes":[{"nodes":[0]}],"scene":0
        })json"; if (!output) return false; }
        im::ImportRequest request; request.sourcePath = directory / "mesh.gltf";
        im::GltfImporter importer;
        const auto imported = importer.Import(request);
        if (!imported.Succeeded() || imported.scene->materials.size() != 1 || imported.scene->meshes.size() != 1)
        { error = "UV glTF import failed"; return false; }
        const auto& material = imported.scene->materials[0];
        const auto& slot = material.baseColor;
        if (slot.uvSet != 1 || slot.offset.x != .25f || slot.offset.y != -.5f
            || slot.tiling.x != -2.f || slot.tiling.y != .75f || slot.rotation != .4f || material.emissiveStrength != 8.f)
        { error = "glTF extension values were lost"; return false; }
        const auto& tangents = imported.scene->meshes[0].streams.tangents;
        if (tangents.empty()) { error = "UV1 tangent generation missing"; return false; }
        for (const auto tangent : tangents)
            if (std::fabs(tangent.x) > .0001f || std::fabs(tangent.y - 1.f) > .0001f)
            { error = "Generated normal-map tangent used UV0 instead of UV1"; return false; }
        im::ConversionOptions options;
        options.modelAssetId.value = Uuid::Parse("10000000-0000-8000-8000-000000000937");
        options.shaderAssetId.value = Uuid::Parse("10000000-0000-4000-8000-000000000938");
        options.resolveMaterialAsset = [](const auto&, size_t) { return experiment::AssetId{Uuid::Parse("10000000-0000-8000-8000-000000000939")}; };
        options.resolveTextureAsset = [](const auto&) { return experiment::AssetId{Uuid::Parse("10000000-0000-4000-8000-000000000940")}; };
        const auto converted = im::ConvertToModelDraft(*imported.scene, options);
        if (!converted.Succeeded()) { error = "UV model draft conversion failed"; return false; }
        const auto& properties = converted.draft->materials[0].properties;
        const auto found = std::find_if(properties.begin(), properties.end(), [](const auto& p) { return p.name == "baseColorMap"; });
        if (found == properties.end() || std::get<experiment::TextureReference>(found->value).coordinates
            != assets::TextureCoordinates{1, {.25f,-.5f}, {-2.f,.75f}, .4f})
        { error = "UV coordinates lost in model draft"; return false; }
        return true;
    }

    struct PbrUvFixture
    {
        static constexpr uint32_t kCases = 32;
        const std::array<assets::TextureCoordinates, 8> cases{{
            {}, {1}, {0, {.5f, .25f}}, {0, {}, {2.f, .5f}},
            {0, {1.f, 0.f}, {1.f, 1.f}, 1.5707963268f},
            {0, {1.f, 0.f}, {-1.f, 1.f}},
            {0, {.25f, -.3f}, {.7f, 1.3f}, .4f},
            {1, {0.f, 1.f}, {1.f, 1.f}, -1.5707963268f}}};
        std::array<experiment::VertexBuffer, 4> vertices;
        const std::array<uint32_t, 6> indices{0, 1, 2, 0, 2, 3};
        std::array<float, 64> pixels{};
        std::shared_ptr<Texture> texture;
        math::matrix4x4 bone = math::matrix4x4::identity();
        bool Initialize(std::string& error)
        {
            for (uint32_t y = 0; y < 4; ++y)
            for (uint32_t x = 0; x < 4; ++x)
            {
                const float texel[] = {.125f + x * .125f, .125f + y * .125f,
                    .625f + (x + y) * .03125f, 1.f};
                std::copy_n(texel, 4, pixels.data() + (y * 4 + x) * 4);
            }
            texture.reset(Texture::CreateFromPixels(4, 4, "Pbr.UV", RHIFormat::RGBA32Float, pixels.data(), 64));
            if (!texture) { error = "UV texture creation failed"; return false; }
            for (uint32_t mask = 0; mask < 4; ++mask)
            {
                if (!vertices[mask].SetLayout(assets::kModelVertexMasks[mask + 4])) return false;
                for (uint32_t v = 0; v < 4; ++v)
                {
                    experiment::Vertex vertex;
                    vertex.position = {v < 2 ? -.75f : .75f, v == 0 || v == 3 ? -.75f : .75f, .5f};
                    vertex.normal = {0, 0, 1}; vertex.tangent = {1, 0, 0, 1};
                    vertex.uv0 = {.125f, .375f};
                    vertex.boneIndices = {0, 0, 0, 0}; vertex.boneWeights = {1, 0, 0, 0};
                    const math::vector2 uv1{.625f, .875f}; const math::vector4 color{1, 1, 1, 1};
                    if (!vertices[mask].Append(vertex, &uv1, &color)) return false;
                }
            }
            return true;
        }
        assets::TextureCoordinates Coordinates(uint32_t test, uint32_t band, uint32_t role) const
        { return cases[(test % 8 + band + role) % cases.size()]; }
        float Sample(const assets::TextureCoordinates& coordinates, uint32_t channel) const
        {
            const float u = coordinates.set ? .625f : .125f, v = coordinates.set ? .875f : .375f;
            const float x = u * coordinates.scale[0], y = v * coordinates.scale[1];
            const float c = std::cos(coordinates.rotation), s = std::sin(coordinates.rotation);
            const float tx = (coordinates.offset[0] + c * x - s * y) * 4.f - .5f;
            const float ty = (coordinates.offset[1] + s * x + c * y) * 4.f - .5f;
            const int ix = int(std::floor(tx)), iy = int(std::floor(ty));
            const float fx = tx - std::floor(tx), fy = ty - std::floor(ty);
            const auto read = [&](int px, int py) { return pixels[(((py % 4 + 4) % 4) * 4 + (px % 4 + 4) % 4) * 4 + channel]; };
            return (read(ix, iy) * (1 - fx) + read(ix + 1, iy) * fx) * (1 - fy)
                + (read(ix, iy + 1) * (1 - fx) + read(ix + 1, iy + 1) * fx) * fy;
        }
        bool Prepare(PbrAoFixture& fixture, uint32_t test,
            std::vector<EnhancedDrawItem>& draws, std::string& error)
        {
            if (!fixture.Prepare(0, draws, error)) return false;
            const auto& mesh = vertices[test / 8];
            constexpr const char* roles[] = {"baseColorMap", "ormMap", "aoMap", "emissiveMap", "normalMap"};
            for (uint32_t band = 0; band < 3; ++band)
            {
                auto& draw = draws[band];
                auto& view = draw.modelMeshView;
                draw.geometryKey = 0x57570000ull + test / 8;
                view.handle.modelId = Uuid::Parse("10000000-0000-8000-8000-000000000917");
                view.handle.meshId = Uuid::Parse("10000000-0000-8000-8000-000000000918");
                view.handle.generation = test / 8 + 1;
                view.vertexData = mesh.Bytes().data(); view.vertexBytes = mesh.ByteSize();
                view.vertexAttributeMask = mesh.AttributeMask(); view.vertexStride = mesh.Stride();
                view.vertexLayoutHash = assets::VertexLayoutHash(view.vertexAttributeMask);
                view.indexData = indices.data(); view.indexCount = indices.size();
                draw.bonePalette = test / 8 >= 2 ? &bone : nullptr;
                draw.boneCount = test / 8 >= 2 ? 1 : 0; draw.animatorKey = draw.boneCount ? 917 : 0;
                ExperimentMaterialSealing::SealSource source;
                source.material.properties = {{"baseColor", math::vector4{.25f, .5f, .75f, 1.f}},
                    {"metallic", .5f}, {"roughness", .5f}, {"emissive", math::vector3{.125f, .125f, .125f}}};
                for (uint32_t role = 0; role < std::size(roles); ++role)
                {
                    experiment::TextureReference reference;
                    reference.coordinates = Coordinates(test, band, role);
                    source.material.properties.push_back({roles[role], reference});
                    source.textures.push_back({roles[role], texture});
                }
                source.textures.push_back({"aoDetailMap", fixture.textures[0]});
                auto gb = std::make_shared<EnhancedMaterialDrawSnapshot>(*draw.materialSnapshot);
                auto fw = std::make_shared<EnhancedForwardMaterialDrawSnapshot>(*draw.forwardMaterialSnapshot);
                const uint32_t first = band == 1 ? 2 : 0;
                const auto seal = [&](auto& packet, uint32_t route) {
                    packet.useNormalMap = 1;
                    return ExperimentMaterialSealing::SealCore(source, fixture.metas[route], *fixture.layouts[route],
                        packet.propertyBytes, packet.textureBindings, error) && packet.IsValid();
                };
                if (!seal(*gb, first) || !seal(*fw, first + 1)) return false;
                draw.materialSnapshot = gb; draw.forwardMaterialSnapshot = fw;
                // Absent UV1 is rejected rather than silently falling back to UV0.
                std::string rejected;
                auto missingUv = *gb; missingUv.textureBindings.front().coordinates.set = 1;
                if (MaterialTextureTable::ValidateMeshCoordinates(missingUv, assets::kCoreVertexAttributes, rejected)
                    || rejected.empty()) { error = "Missing UV1 accepted"; return false; }
            }
            return true;
        }
        bool Check(uint32_t test, const std::array<RHIReadbackImage, 6>& images, std::string& error) const
        {
            for (uint32_t y = 8; y < 24; ++y)
            for (uint32_t x = 8; x < 24; ++x)
            {
                const uint32_t band = x < 12 ? 0 : x < 20 ? 1 : 2;
                const auto sample = [&](uint32_t role, uint32_t c) { return Sample(Coordinates(test, band, role), c); };
                const auto n = math::normalize(math::vector3{sample(4, 0)*2-1, sample(4, 1)*2-1, sample(4, 2)*2-1});
                const float normal[] = {n.x, n.y, n.z};
                const float factors[] = {.25f, .5f, .75f};
                const float mr[] = {sample(2, 0), sample(1, 1)*.5f, sample(1, 2)*.5f};
                for (uint32_t c = 0; c < 3; ++c)
                {
                    const float expected[] = {mr[c], sample(3, c)*.125f, normal[c]*.5f+.5f, sample(0, c)*factors[c]};
                    for (uint32_t attachment = 2; attachment < 6; ++attachment)
                        if (!std::isfinite(images[attachment].At(x, y, c))
                            || std::fabs(images[attachment].At(x, y, c) - expected[attachment-2]) > .002f)
                        { error = "UV attachment mismatch test=" + std::to_string(test) + " band=" + std::to_string(band)
                            + " attachment=" + std::to_string(attachment) + " channel=" + std::to_string(c)
                            + " actual=" + std::to_string(images[attachment].At(x,y,c)) + " expected=" + std::to_string(expected[attachment-2]); return false; }
                }
            }
            return true;
        }
    };

    // Compare actual pre-tone outputs with identical lighting. Semantic tests
    // additionally check attachment values against independent CPU expectations.
    template <typename TResources>
    bool CapturePbrParity(TResources& resources, IRenderPipelineCache& pipelines,
        IRenderRootSignatureCache& roots, IRenderMeshCache& meshes,
        IRenderTextureCache& textures, const std::function<void()>& beginCaches,
        PbrParityCapture& capture, std::string& error, uint32_t mode)
    {
        const bool aoTest = mode == 1, emissionTest = mode == 2, transformTest = mode == 3, uvTest = mode == 4;
        std::array<std::unique_ptr<Mesh>, 2> quads;
        for (uint32_t variant = 0; variant < quads.size(); ++variant)
        {
            std::vector<Vertex> vertices(4);
            const math::vector3 positions[] = {
                {-0.75f, -0.75f, 0.5f}, {-0.75f, 0.75f, 0.5f},
                {0.75f, 0.75f, 0.5f}, {0.75f, -0.75f, 0.5f} };
            for (uint32_t i = 0; i < vertices.size(); ++i)
            {
                vertices[i].position = positions[i];
                vertices[i].normal = {0.f, 0.f, 1.f};
                vertices[i].tangent = variant == 0
                    ? math::vector3{1.f, 0.f, 0.f} : math::vector3{0.f, 0.f, 1.f};
                vertices[i].bitangent = {0.f, 1.f, 0.f};
            }
            quads[variant] = std::make_unique<Mesh>("PbrParity." + std::to_string(variant),
                std::move(vertices), std::vector<uint32>{0, 1, 2, 0, 2, 3});
        }
        FrameCameraSnapshot camera{};
        camera.view = camera.projection = camera.inverseView =
            camera.inverseProjection = math::matrix4x4::identity();
        camera.eyePosition = {0.f, 0.f, 2.f};
        camera.forward = {0.f, 0.f, 1.f};
        camera.right = {1.f, 0.f, 0.f};
        camera.up = {0.f, 1.f, 0.f};
        camera.nearPlane = 0.f;
        camera.farPlane = 1.f;
        std::vector<EnhancedDrawItem> draws(1);
        draws[0].worldMatrix = math::matrix4x4::identity();
        draws[0].baseColorFactor = math::color(0.25f, 0.5f, 0.75f, 1.f);
        std::vector<EnhancedLight> lights;
        EnhancedFrameContext context{};
        context.resources = &resources;
        context.psoManager = &pipelines;
        context.rootSignatures = &roots;
        context.meshCache = &meshes;
        context.textureCache = &textures;
        context.camera = &camera;
        context.draws = context.forwardDraws = &draws;
        context.lights = &lights;
        context.width = context.height = kPbrSize;
        EnhancedGBufferPass gbuffer;
        EnhancedDeferredPass deferred;
        EnhancedForwardPass forward;
        std::array<RHIReadback, 6> readbacks{};
        PbrAoFixture ao;
        PbrEmissionFixture emission;
        PbrTransformFixture transform;
        PbrUvFixture uv;
        std::array<RHITextureHandle, 3> ibl{};
        bool frameOpen = false;
        const auto cleanup = [&] {
            if (frameOpen) resources.AbortFrame();
            resources.WaitForGpu();
            forward.Shutdown(); deferred.Shutdown(); gbuffer.Shutdown();
            for (auto& readback : readbacks) resources.ReleaseReadback(readback);
            for (auto texture : ibl) if (texture.IsValid()) resources.ReleaseTexture(texture);
        };
        const auto fail = [&](const std::string& message) {
            error = message; cleanup(); return false;
        };
        if (!gbuffer.Initialize(context, error) || !deferred.Initialize(context, error)
            || !forward.Initialize(context, error)) return fail(error);
        forward.SetUseReferencePath(true);
        if ((aoTest || emissionTest || uvTest) && !ao.Initialize(gbuffer, forward, context, error)) return fail(error);
        if (emissionTest && !emission.Initialize(error)) return fail(error);
        if (transformTest && !transform.Initialize(error)) return fail(error);
        if (uvTest && !uv.Initialize(error)) return fail(error);
        for (auto& readback : readbacks)
            if (!resources.CreateReadback(kPbrSize, kPbrSize, RHIFormat::RGBA16Float,
                    1, readback, error)) return fail(error);

        // Constant cubes with a nontrivial DFG make the old Forward ambient and
        // missing direct-light compensation observably different from Deferred.
        constexpr float iblValues[3][4] = {
            {0.5f, 0.25f, 0.125f, 1.f}, {0.125f, 0.25f, 0.5f, 1.f},
            {0.5f, 0.125f, 0.f, 1.f} };
        for (uint32_t i = 0; i < ibl.size(); ++i)
        {
            RHITextureDesc desc{};
            desc.width = desc.height = 1;
            desc.depthOrArraySize = i < 2 ? 6 : 1;
            desc.format = RHIFormat::RGBA16Float;
            desc.allowRenderTarget = true;
            std::copy_n(iblValues[i], 4, desc.clearColor);
            if (!resources.CreateTexture(desc, ibl[i], error)) return fail(error);
        }
        if (!resources.BeginFrame(error)) return fail(error);
        frameOpen = true;
        {
            EnhancedRenderGraph graph(static_cast<IRenderDeviceServices&>(resources));
            std::array<RGHandle, 3> handles{};
            for (uint32_t i = 0; i < ibl.size(); ++i)
            {
                handles[i] = graph.ImportTexture(ibl[i], RHIResourceState::Common, "Pbr.IBL");
                graph.AddPass("Pbr.IBL.Clear", {{handles[i], RHIResourceState::RenderTarget}},
                    [&, i](const EnhancedRenderGraph::ExecuteContext& execute) {
                        for (uint32_t face = 0; face < (i < 2 ? 6u : 1u); ++face)
                        {
                            const auto target = RHIColorTargetDesc::Slice(ibl[i],
                                RHIFormat::RGBA16Float, 0, face);
                            const auto binding = resources.CreateRenderTargets({&target, 1});
                            if (!binding.IsValid()) { error = "IBL clear target invalid"; return; }
                            execute.encoder->BindRenderTargets(binding);
                            execute.encoder->ClearRenderTargets(binding, iblValues[i]);
                        }
                    });
            }
            graph.AddPass("Pbr.IBL.Ready", {
                {handles[0], RHIResourceState::PixelShaderResource},
                {handles[1], RHIResourceState::PixelShaderResource},
                {handles[2], RHIResourceState::PixelShaderResource}},
                [](const EnhancedRenderGraph::ExecuteContext&) {}, true);
            if (!graph.Compile(error) || !graph.Execute(error) || !error.empty()) return fail(error);
            if (!resources.EndFrame(error)) return fail(error);
            frameOpen = false;
            resources.WaitForGpu();
        }

        constexpr float materials[6][2] = {
            {0.f, 0.f}, {0.f, 1.f}, {1.f, 0.f}, {1.f, 1.f}, {0.5f, 0.5f}, {0.5f, 0.5f} };
        for (uint32_t lightCase = 0; lightCase < (transformTest || uvTest ? 2u : 6u); ++lightCase)
        for (uint32_t material = 0; material < (uvTest ? PbrUvFixture::kCases : transformTest ? PbrTransformFixture::kCases : emissionTest ? 13u : aoTest ? 8u : 6u); ++material)
        {
            const uint32_t lighting = transformTest || uvTest ? (lightCase == 0 ? 1u : 5u) : lightCase;
            // 0 unlit, 1 sun, 2 point, 3 spot, 4 IBL, 5 sun+IBL.
            lights.clear();
            if (lighting != 0 && lighting != 4)
            {
                EnhancedLight light{};
                light.position = math::vector4(0.f, 0.f, 1.5f,
                    lighting == 2 ? 1.f : lighting == 3 ? 2.f : 0.f);
                light.direction = math::vector4(0.f, 0.f, -1.f, 1.5f);
                light.color = math::color(1.f, 0.75f, 0.5f, 2.f);
                light.attenuation = math::vector4(1.f, 0.25f, 0.125f, 4.f);
                lights.push_back(light);
            }
            const bool hasIbl = lighting >= 4;
            deferred.SetIBL(hasIbl ? ibl[0] : RHITextureHandle{}, ibl[1], 1, ibl[2]);
            forward.SetIBL(hasIbl ? ibl[0] : RHITextureHandle{}, ibl[1], 1, ibl[2]);
            draws[0].mesh = quads[mode == 0 && material == 5 ? 1 : 0].get();
            if (uvTest)
            { if (!uv.Prepare(ao, material, draws, error)) return fail(error); }
            else if (emissionTest)
            {
                if (!emission.Prepare(ao, material, draws, error)) return fail(error);
            }
            else if (aoTest)
            {
                if (!ao.Prepare(material, draws, error)) return fail(error);
            }
            else if (transformTest) transform.Prepare(material, draws[0]);
            else
            {
                draws[0].metallic = materials[material][0];
                draws[0].roughness = materials[material][1];
                draws[0].useNormalMap = material == 5 ? 1 : 0;
            }
            if (!resources.BeginFrame(error)) return fail(error);
            frameOpen = true;
            beginCaches();
            if (!gbuffer.PrepareFrame(context, error) || !deferred.PrepareFrame(context, error)
                || !forward.PrepareFrame(context, error)) return fail(error);
            EnhancedRenderGraph graph(static_cast<IRenderDeviceServices&>(resources));
            gbuffer.Declare(graph, context);
            deferred.SetInputs(gbuffer.GetOutputs());
            deferred.Declare(graph, context);
            // A separate depth target lets Forward shade the identical surface
            // without being rejected by its own GBuffer depth (LESS comparison).
            RGTextureDesc depthDesc{};
            depthDesc.width = depthDesc.height = kPbrSize;
            depthDesc.format = RHIFormat::D32Float;
            depthDesc.allowDepthStencil = true;
            const auto depth = graph.CreateTexture(depthDesc);
            graph.AddPass("Pbr.Forward.Depth", {{depth, RHIResourceState::DepthWrite}},
                [&](const EnhancedRenderGraph::ExecuteContext& execute) {
                    const auto desc = RHIDepthTargetDesc::Depth(execute.ResolveHandle(depth), RHIFormat::D32Float);
                    const auto targets = resources.CreateRenderTargets(std::span<const RHITextureHandle>{}, &desc);
                    if (!targets.IsValid()) { error = "Forward depth target invalid"; return; }
                    execute.encoder->BindRenderTargets(targets);
                    execute.encoder->ClearDepthTarget(targets, 1.f);
                });
            EnhancedForwardPass::Inputs inputs{};
            inputs.depth = depth;
            forward.SetInputs(inputs);
            forward.Declare(graph, context);
            const std::array outputs{deferred.GetOutput(), forward.GetOutput(), gbuffer.GetOutputs().metalRough, gbuffer.GetOutputs().emissive, gbuffer.GetOutputs().normal, gbuffer.GetOutputs().diffuse};
            if (!outputs[0].IsValid() || !outputs[1].IsValid()) return fail("Missing PBR output");
            graph.AddPass("Pbr.Compare", {{outputs[0], RHIResourceState::CopySource},
                {outputs[1], RHIResourceState::CopySource}, {outputs[2], RHIResourceState::CopySource}, {outputs[3], RHIResourceState::CopySource}, {outputs[4], RHIResourceState::CopySource}, {outputs[5], RHIResourceState::CopySource}},
                [&](const EnhancedRenderGraph::ExecuteContext& execute) {
                    for (uint32_t i = 0; i < outputs.size(); ++i)
                        execute.encoder->CopyToReadback(readbacks[i], execute.ResolveHandle(outputs[i]));
                }, true);
            if (!graph.Compile(error) || !graph.Execute(error) || !error.empty()) return fail(error);
            if (gbuffer.GetLastDrawCount() != draws.size() || forward.GetLastDrawCount() != draws.size())
                return fail("PBR fixture did not draw both routes");
            if (!resources.EndFrame(error)) return fail(error);
            frameOpen = false;
            resources.WaitForGpu();
            std::array<RHIReadbackImage, 6> images;
            for (uint32_t i = 0; i < images.size(); ++i)
                if (!resources.MapReadback(readbacks[i], images[i], error)) return fail(error);
            if (uvTest && !uv.Check(material, images, error)) return fail(error);
            if (transformTest)
            {
                if (gbuffer.GetLastSkinnedCount() != (draws[0].boneCount ? 1u : 0u))
                    return fail("Transform fixture omitted skin draw");
                const float expected[] = {transform.expected.x, transform.expected.y, transform.expected.z};
                for (uint32_t y = 8; y < 24; ++y)
                for (uint32_t x = 8; x < 24; ++x)
                for (uint32_t c = 0; c < 3; ++c)
                {
                    const float normal = images[4].At(x, y, c) * 2.f - 1.f;
                    if (!std::isfinite(normal) || std::fabs(normal - expected[c]) > .0015f)
                        return fail("Transform normal mismatch case=" + std::to_string(material)
                            + " channel=" + std::to_string(c) + " actual=" + std::to_string(normal)
                            + " expected=" + std::to_string(expected[c]));
                }
            }
            if (aoTest)
            {
                for (uint32_t y = 8; y < 24; ++y)
                for (uint32_t x = 8; x < 24; ++x)
                {
                    const float expected[] = {PbrAoFixture::Expected(material), .5f, .5f, 1.f};
                    for (uint32_t c = 0; c < 4; ++c)
                        if (!std::isfinite(images[2].At(x, y, c))
                            || std::fabs(images[2].At(x, y, c) - expected[c]) > .0005f)
                            return fail("AO GBuffer packing mismatch case=" + std::to_string(material)
                                + " pixel=" + std::to_string(x) + "," + std::to_string(y)
                                + " channel=" + std::to_string(c) + " actual=" + std::to_string(images[2].At(x, y, c)));
                }
            }
            float peak = 0.f;
            for (uint32_t y = 8; y < 24; ++y)
            for (uint32_t x = 8; x < 24; ++x)
            for (uint32_t c = 0; c < 3; ++c)
            {
                const float a = images[0].At(x, y, c), b = images[1].At(x, y, c);
                const float delta = std::fabs(a - b);
                if (!std::isfinite(a) || !std::isfinite(b)
                    || delta > 0.002f + 0.005f * (std::max)(std::fabs(a), std::fabs(b)))
                {
                    char message[256]{};
                    std::snprintf(message, sizeof(message),
                        "PBR route mismatch light=%u material=%u pixel=%u,%u,%u Deferred=%g Forward=%g",
                        lighting, material, x, y, c, a, b);
                    return fail(message);
                }
                capture.maxRouteDelta = (std::max)(capture.maxRouteDelta, delta);
                peak = (std::max)(peak, a);
                capture.rgb.push_back(a); capture.rgb.push_back(b);
            }
            if (mode == 0 && ((lighting == 0 && peak != 0.f) || (lighting != 0 && peak < 0.001f)))
                return fail("PBR black/lit response missing: " + std::to_string(lighting));
            // A rough metal under this intensity-2 sun stays below 1. A null
            // LUT interpreted as DFG=(0,0) produces hundreds despite route parity.
            if (mode == 0 && lighting == 1 && material == 3 && peak > 1.f)
                return fail("PBR no-IBL direct light amplified by an absent LUT");
            if (mode == 0 && material == 5)
            {
                constexpr std::size_t sampleCount = 16 * 16 * 3 * 2;
                const std::size_t start = capture.rgb.size() - sampleCount;
                for (std::size_t i = 0; i < sampleCount; ++i)
                    if (capture.rgb[start + i] != capture.rgb[start - sampleCount + i])
                        return fail("PBR degenerate tangent did not preserve geometric normal");
            }
            if (aoTest)
            {
                constexpr size_t samples = 16 * 16 * 3 * 2;
                const auto current = capture.rgb.size() - samples;
                const auto neutral = current - material * samples;
                for (size_t i = 0; i < samples; ++i)
                {
                    const float value = capture.rgb[current + i], full = capture.rgb[neutral + i];
                    // AO cannot attenuate direct light or emission. Missing AO,
                    // white AO and strength=0 must match the neutral material.
                    if ((lighting < 4 || PbrAoFixture::Expected(material) == 1.f)
                        && value != full) return fail("AO changed direct/emission/neutral response");
                    if (lighting == 0 && std::fabs(value - .125f) > .0005f)
                        return fail("AO changed emission-only output");
                    if (lighting == 4 && PbrAoFixture::Expected(material) == 0.f
                        && std::fabs(value - .125f) > .0005f)
                        return fail("AO=0 left ambient radiance");
                    if (lighting >= 4 && PbrAoFixture::Expected(material) < 1.f
                        && value >= full) return fail("AO did not reduce ambient radiance");
                }
            }
            if (emissionTest)
            {
                constexpr size_t samples = 16 * 16 * 3 * 2;
                const auto current = capture.rgb.size() - samples;
                const auto baseline = current - material * samples;
                for (uint32_t y = 8; y < 24; ++y)
                for (uint32_t x = 8; x < 24; ++x)
                for (uint32_t c = 0; c < 3; ++c)
                {
                    const float expected = PbrEmissionFixture::Expected(material, c);
                    const float actual = images[3].At(x, y, c);
                    const float tolerance = .002f + .001f * expected;
                    if (!std::isfinite(actual) || std::fabs(actual - expected) > tolerance)
                        return fail("Emission GBuffer mismatch case=" + std::to_string(material)
                            + " channel=" + std::to_string(c) + " actual=" + std::to_string(actual)
                            + " expected=" + std::to_string(expected));
                    const size_t offset = ((y - 8) * 16 * 3 + (x - 8) * 3 + c) * 2;
                    for (uint32_t route = 0; route < 2; ++route)
                        if (std::fabs(capture.rgb[current + offset + route]
                            - capture.rgb[baseline + offset + route] - expected) > tolerance)
                            return fail("Emission is not additive under light/AO case="
                                + std::to_string(material) + " lighting=" + std::to_string(lighting));
                }
            }
            ++capture.cases;
        }
        cleanup();
        return true;
    }
}

static bool RunPbrMaterialTest(std::string& outLog, uint32_t mode)
{
    std::array<PbrParityCapture, 2> captures;
    std::string error, validation;
    {
        DX12DeviceResources resources;
        DX12PSOManager pipelines;
        DX12RootSignatureCache roots;
        DX12MeshCache meshes;
        DX12TextureCache textures;
        if (!resources.Initialize(kPbrSize, kPbrSize, error)
            || !pipelines.Initialize(&resources, L"dx12_pbr_parity.cache", error)
            || !roots.Initialize(&resources, error) || !meshes.Initialize(&resources, error)
            || !textures.Initialize(&resources, error)) { outLog += error; return false; }
        const bool captured = CapturePbrParity(resources, pipelines, roots, meshes, textures,
            [&] { meshes.BeginFrame(0); textures.BeginFrame(0); }, captures[0], error, mode);
        const auto problems = resources.DrainDebugMessages(validation);
        textures.Shutdown(); meshes.Shutdown(); roots.Shutdown(); pipelines.Shutdown(); resources.Shutdown();
        if (!captured || problems != 0) { outLog += "DX12: " + error + validation; return false; }
    }
    {
        VulkanDeviceResources resources;
        VulkanPipelineCache pipelines;
        VulkanMeshCache meshes;
        VulkanTextureCache textures;
        if (!VulkanApi::LoadLoader(error) || !resources.Initialize(kPbrSize, kPbrSize, true, error))
        { outLog += error; return false; }
        pipelines.Initialize(resources.GetDevice());
        resources.SetPipelineCache(&pipelines);
        if (!meshes.Initialize(&resources, error) || !textures.Initialize(&resources, error))
        { outLog += error; return false; }
        RHIShaderCompiler::ScopedOutput output{RHIShaderBinary::SpirV};
        const bool captured = CapturePbrParity(resources, pipelines, pipelines, meshes, textures,
            [&] { meshes.BeginFrame(0); }, captures[1], error, mode);
        const auto stubs = resources.GetUnimplementedCount() + resources.GetEncoderUnimplementedCount();
        const auto problems = resources.DrainDebugMessages(validation);
        textures.Shutdown(); meshes.Shutdown(); pipelines.Shutdown(); resources.Shutdown();
        if (!captured || problems != 0 || stubs != 0)
        { outLog += "Vulkan: " + error + validation; return false; }
    }
    const uint32_t expectedCases = mode == 4 ? PbrUvFixture::kCases * 2 : mode == 3 ? PbrTransformFixture::kCases * 2 : mode == 2 ? 78 : mode == 1 ? 48 : 36;
    if (captures[0].cases != expectedCases || captures[1].cases != expectedCases
        || captures[0].rgb.size() != captures[1].rgb.size()) return false;
    float maxBackendDelta = 0.f;
    for (std::size_t i = 0; i < captures[0].rgb.size(); ++i)
    {
        const float a = captures[0].rgb[i], b = captures[1].rgb[i];
        const float delta = std::fabs(a - b);
        maxBackendDelta = (std::max)(maxBackendDelta, delta);
        if (delta > 0.002f + 0.005f * (std::max)(std::fabs(a), std::fabs(b)))
        { outLog += "PBR backend mismatch at " + std::to_string(i); return false; }
    }
    char line[320]{};
    std::snprintf(line, sizeof(line),
        "PBR %s: %u cases/backend, route max delta DX12=%g Vulkan=%g, backend=%g; validation=0\n",
        mode == 4 ? "UV0/UV1 transforms, 5/8-slot tables" : mode == 3 ? "normal transform, typed static/skin" : mode == 2 ? "emission/color space/HDR" : mode == 1 ? "AO, 5/8-slot alternating tables" : "native Slang", expectedCases, captures[0].maxRouteDelta, captures[1].maxRouteDelta, maxBackendDelta);
    outLog += line;
    return true;
}


bool RunPbrShaderParityTest(std::string& outLog) { return RunPbrMaterialTest(outLog, 0); }
bool RunPbrOcclusionTest(std::string& outLog) { return RunPbrMaterialTest(outLog, 1); }

bool RunPbrEmissionTest(std::string& outLog) { return RunPbrMaterialTest(outLog, 2); }
bool RunPbrTransformTest(std::string& outLog) { return RunPbrMaterialTest(outLog, 3); }
bool RunPbrUvTest(std::string& outLog)
{
    std::string error;
    if (!CheckPbrUvImport(error)) { outLog += error; return false; }
    outLog += "UV glTF extension/import/draft and UV1 tangent generation PASS\n";
    return RunPbrMaterialTest(outLog, 4);
}

namespace
{
    // Drive real GBuffer, Forward and cascaded Shadow passes with identical
    // coverage inputs. Read pixels after the GPU fence, including discarded
    // depth and front/back normal orientation; repeat with a skin palette.
    template <typename TResources>
    bool CapturePbrCoverage(TResources& resources, IRenderPipelineCache& pipelines,
        IRenderRootSignatureCache& roots, IRenderMeshCache& meshes,
        IRenderTextureCache& textures, const std::function<void()>& beginCaches,
        PbrParityCapture& capture, std::string& error)
    {
        using Coverage = EnhancedMaterialCoverage;
        constexpr uint32_t opaque = Coverage::Enabled | Coverage::DoubleSided;
        constexpr uint32_t masked = opaque | Coverage::Masked;
        struct Case { uint32_t flags; float alpha, cutoff; bool holes, back; float visible; };
        const Case cases[] = {
            {opaque, 0.f, .5f, false, false, 1.f},
            {masked, .49f, .5f, false, false, 0.f},
            {masked, .5f, .5f, false, false, 1.f},
            {masked, 1.f, .5f, true, false, .5f},
            {masked, .25f, .5f, true, false, 0.f},
            {Coverage::Enabled, 1.f, .5f, false, false, 1.f},
            {Coverage::Enabled, 1.f, .5f, false, true, 0.f},
            {opaque, 1.f, .5f, false, true, 1.f},
            {opaque | Coverage::Blended, .25f, .5f, false, false, 1.f},
            {masked, 0.f, 0.f, false, false, 1.f},
        };
        std::array<std::unique_ptr<Mesh>, 2> quads;
        std::array<experiment::VertexBuffer, 2> uvMeshes;
        const std::array<std::array<uint32_t, 6>, 2> uvIndices{{{0,1,2,0,2,3}, {0,2,1,0,3,2}}};
        const assets::TextureCoordinates maskUv{1, {.25f, .1f}, {.5f, .75f}};
        for (uint32_t back = 0; back < 2; ++back)
        {
            std::vector<Vertex> vertices(4);
            if (!uvMeshes[back].SetLayout(assets::kCoreVertexAttributes | assets::kSkinVertexAttributes
                | assets::Bit(assets::VertexAttribute::Uv1))) return false;
            const math::vector3 positions[] = {
                {-.75f, -.75f, .5f}, {-.75f, .75f, .5f},
                {.75f, .75f, .5f}, {.75f, -.75f, .5f}};
            const math::vector2 uv[] = {{0, 1}, {0, 0}, {1, 0}, {1, 1}};
            for (uint32_t i = 0; i < 4; ++i)
            {
                vertices[i].position = positions[i]; vertices[i].uv0 = uv[i];
                vertices[i].normal = {0, 0, 1}; vertices[i].tangent = {1, 0, 0};
                vertices[i].bitangent = {0, 1, 0};
                vertices[i].boneIndices = {0, 0, 0, 0};
                vertices[i].boneWeights = {1, 0, 0, 0};
                experiment::Vertex typed;
                typed.position = positions[i]; typed.normal = {0,0,1}; typed.tangent = {1,0,0,1};
                typed.uv0 = {.125f, .125f}; // Poison UV0; only transformed UV1 reconstructs the mask.
                typed.boneIndices = {0,0,0,0}; typed.boneWeights = {1,0,0,0};
                const math::vector2 uv1{(uv[i].x - .25f) / .5f, (uv[i].y - .1f) / .75f};
                if (!uvMeshes[back].Append(typed, &uv1)) return false;
            }
            quads[back] = std::make_unique<Mesh>("PbrCoverage." + std::to_string(back),
                std::move(vertices), back ? std::vector<uint32>{0, 2, 1, 0, 3, 2}
                    : std::vector<uint32>{0, 1, 2, 0, 2, 3});
        }
        std::array<uint8_t, 8 * 8 * 4> pixels;
        pixels.fill(255);
        for (uint32_t y = 0; y < 8; ++y)
            for (uint32_t x = 0; x < 4; ++x) pixels[(y * 8 + x) * 4 + 3] = 0;
        std::shared_ptr<Texture> holes(Texture::CreateFromPixels(8, 8, "PbrCoverage.Holes",
            RHIFormat::RGBA8Unorm, pixels.data(), 8 * 4));
        const uint8_t whitePixel[] = {255, 255, 255, 255};
        std::shared_ptr<Texture> white(Texture::CreateFromPixels(1, 1, "PbrCoverage.Emission",
            RHIFormat::RGBA8Unorm, whitePixel, 4));
        if (!holes || !white) { error = "Coverage fixture textures missing"; return false; }
        FrameCameraSnapshot camera{};
        camera.view = camera.projection = camera.inverseView = camera.inverseProjection =
            math::matrix4x4::identity();
        camera.eyePosition = {0, 0, 2}; camera.forward = {0, 0, 1};
        camera.right = {1, 0, 0}; camera.up = {0, 1, 0};
        camera.nearPlane = .1f; camera.farPlane = 3.f;
        auto shadowCamera = camera;
        shadowCamera.projection = math::perspective_fov_lh(1.5707963f, 1.f, .1f, 3.f);
        std::vector<EnhancedDrawItem> draws(1), opaqueDraws;
        EnhancedLight sun{};
        sun.direction = math::vector4(0, 0, 1, 0);
        sun.color = math::color(0, 0, 0, 1); // caster selection, no direct radiance
        std::vector<EnhancedLight> lights{sun};
        EnhancedFrameContext context{};
        context.resources = &resources; context.psoManager = &pipelines;
        context.rootSignatures = &roots; context.meshCache = &meshes; context.textureCache = &textures;
        context.camera = &camera; context.draws = &opaqueDraws; context.forwardDraws = &draws;
        context.lights = &lights; context.width = context.height = kPbrSize;
        auto shadowContext = context;
        shadowContext.camera = &shadowCamera;
        // Include BLEND here deliberately to verify Shadow's defensive exclusion.
        shadowContext.draws = &draws;
        EnhancedGBufferPass gbuffer;
        EnhancedForwardPass forward;
        EnhancedShadowPass shadow;
        // diffuse, normal, GBuffer depth, Forward HDR, shadow depth
        std::array<RHIReadback, 5> readbacks{};
        bool frameOpen = false;
        const auto cleanup = [&] {
            if (frameOpen) resources.AbortFrame();
            resources.WaitForGpu();
            shadow.Shutdown(); forward.Shutdown(); gbuffer.Shutdown();
            for (auto& readback : readbacks) resources.ReleaseReadback(readback);
        };
        const auto fail = [&](const std::string& message) { error = message; cleanup(); return false; };
        if (!gbuffer.Initialize(context, error) || !forward.Initialize(context, error)
            || !shadow.Initialize(shadowContext, error)) return fail(error);
        forward.SetUseReferencePath(true);
        for (uint32_t i = 0; i < readbacks.size(); ++i)
        {
            const auto size = i == 4 ? EnhancedShadowPass::kShadowMapSize : kPbrSize;
            if (!resources.CreateReadback(size, size, i == 2 || i == 4 ? RHIFormat::D32Float
                    : RHIFormat::RGBA16Float, 1, readbacks[i], error)) return fail(error);
        }
        std::array<ShaderMeta, 2> metas;
        const std::array<ShaderMetaHandle, 2> handles{{{701, 1}, {702, 1}}};
        std::array<std::shared_ptr<const ShaderMetaBindingLayout>, 2> layouts;
        std::array<RHIShaderPermutationKey, 2> permutations;
        const std::vector<uint16_t> keywords{0};
        const std::filesystem::path shaderRoot = "Dynamic_CPP/Assets/Shaders/DefaultPassShader";
        if (!ShaderMetaLoader::LoadFile(shaderRoot / "GBuffer.shadermeta",
                FileGuid("10000000-0000-4000-8000-000000000701"), metas[0], error)
            || !ShaderMetaLoader::LoadFile(shaderRoot / "Forward.shadermeta",
                FileGuid("10000000-0000-4000-8000-000000000702"), metas[1], error)
            || !gbuffer.EnsureShaderMetaVariant(context, handles[0], metas[0], keywords,
                permutations[0], layouts[0], error)
            || !forward.EnsureShaderMetaVariant(context, handles[1], metas[1], keywords,
                permutations[1], layouts[1], error)) return fail(error);
        const auto bone = math::matrix4x4::identity();
        uint32_t fullShadowPixels = 0;
        for (uint32_t owned = 0; owned < 2; ++owned)
        for (uint32_t skin = 0; skin < 2; ++skin)
        for (uint32_t index = 0; index < std::size(cases); ++index)
        {
            const auto& test = cases[index];
            const bool blend = 0 != (test.flags & Coverage::Blended);
            auto& draw = draws[0];
            draw.mesh = quads[test.back ? 1 : 0].get();
            draw.worldMatrix = math::matrix4x4::identity();
            draw.baseColorFactor = math::color(1, 1, 1, test.alpha);
            draw.coverage = {test.flags, test.cutoff, test.alpha};
            draw.baseColor = test.holes ? holes.get() : nullptr;
            draw.emissive = white.get();
            draw.bonePalette = skin ? &bone : nullptr;
            draw.animatorKey = skin ? 123 : 0; draw.boneCount = skin;
            draw.materialSnapshot.reset(); draw.forwardMaterialSnapshot.reset();
            draw.modelMeshView = {}; draw.geometryKey = 0;
            if (owned)
            {
                const uint32_t back = test.back ? 1 : 0;
                const auto& mesh = uvMeshes[back];
                auto& view = draw.modelMeshView;
                view.handle.modelId = Uuid::Parse("10000000-0000-8000-8000-000000000927");
                view.handle.meshId = Uuid::Parse("10000000-0000-8000-8000-000000000928");
                view.handle.generation = back + 1;
                view.vertexData = mesh.Bytes().data(); view.vertexBytes = mesh.ByteSize();
                view.vertexAttributeMask = mesh.AttributeMask(); view.vertexStride = mesh.Stride();
                view.vertexLayoutHash = assets::VertexLayoutHash(mesh.AttributeMask());
                view.indexData = uvIndices[back].data(); view.indexCount = 6;
                draw.geometryKey = 0x57570100ull + back;
                ExperimentMaterialSealing::SealSource source;
                source.material.blendMode = blend ? experiment::MaterialBlendMode::Transparent
                    : test.flags & Coverage::Masked ? experiment::MaterialBlendMode::Masked
                    : experiment::MaterialBlendMode::Opaque;
                source.material.properties = {{"baseColor", math::vector4(1, 1, 1, test.alpha)},
                    {"alphaCutoff", test.cutoff}, {"emissive", math::vector3(1, 1, 1)},
                    {"doubleSided", bool(test.flags & Coverage::DoubleSided)}};
                experiment::TextureReference maskReference; maskReference.coordinates = maskUv;
                source.material.properties.push_back({"baseColorMap", maskReference});
                source.textures = {{"baseColorMap", test.holes ? holes : nullptr}, {"emissiveMap", white}};
                auto gbPacket = std::make_shared<EnhancedMaterialDrawSnapshot>();
                auto fwPacket = std::make_shared<EnhancedForwardMaterialDrawSnapshot>();
                const auto seal = [&](auto& packet, uint32_t route) {
                    packet.shaderMetaHandle = handles[route]; packet.permutationKey = permutations[route];
                    packet.keywordSelections = keywords; packet.bindingLayout = *layouts[route];
                    return ExperimentMaterialSealing::SealCore(source, metas[route], *layouts[route],
                            packet.propertyBytes, packet.textureBindings, error)
                        && ExperimentMaterialSealing::SealCoverage(source, *layouts[route],
                            packet.propertyBytes, packet.coverage, error) && packet.IsValid();
                };
                if (!seal(*gbPacket, 0) || !seal(*fwPacket, 1)) return fail(error);
                draw.materialSnapshot = gbPacket; draw.forwardMaterialSnapshot = fwPacket;
                // Poison legacy fields to prove the sealed packet supplies alpha/texture.
                draw.baseColorFactor.a = .875f; draw.baseColor = nullptr;
                draw.coverage = {};
            }
            opaqueDraws = blend ? std::vector<EnhancedDrawItem>{} : draws;
            if (!resources.BeginFrame(error)) return fail(error);
            frameOpen = true; beginCaches();
            if (!gbuffer.PrepareFrame(context, error) || !forward.PrepareFrame(context, error)
                || !shadow.PrepareFrame(shadowContext, error)) return fail(error);
            EnhancedRenderGraph graph(static_cast<IRenderDeviceServices&>(resources));
            gbuffer.Declare(graph, context);
            shadow.Declare(graph, shadowContext);
            RGTextureDesc depthDesc{};
            depthDesc.width = depthDesc.height = kPbrSize;
            depthDesc.format = RHIFormat::D32Float; depthDesc.allowDepthStencil = true;
            const auto depth = graph.CreateTexture(depthDesc);
            graph.AddPass("Coverage.Forward.Depth", {{depth, RHIResourceState::DepthWrite}},
                [&](const EnhancedRenderGraph::ExecuteContext& execute) {
                    const auto desc = RHIDepthTargetDesc::Depth(execute.ResolveHandle(depth), RHIFormat::D32Float);
                    const auto target = resources.CreateRenderTargets(std::span<const RHITextureHandle>{}, &desc);
                    if (!target.IsValid()) { error = "Coverage depth target invalid"; return; }
                    execute.encoder->BindRenderTargets(target); execute.encoder->ClearDepthTarget(target, 1.f);
                });
            EnhancedForwardPass::Inputs inputs{}; inputs.depth = depth;
            forward.SetInputs(inputs); forward.Declare(graph, context);
            const auto& gb = gbuffer.GetOutputs();
            const std::array outputs{gb.diffuse, gb.normal, gb.depth, forward.GetOutput(), shadow.GetShadowMap()};
            graph.AddPass("Coverage.Readback", {{outputs[0], RHIResourceState::CopySource},
                {outputs[1], RHIResourceState::CopySource}, {outputs[2], RHIResourceState::CopySource},
                {outputs[3], RHIResourceState::CopySource}, {outputs[4], RHIResourceState::CopySource}},
                [&](const EnhancedRenderGraph::ExecuteContext& execute) {
                    for (uint32_t i = 0; i < outputs.size(); ++i)
                        execute.encoder->CopyToReadback(readbacks[i], execute.ResolveHandle(outputs[i]), 0, 0);
                }, true);
            if (!graph.Compile(error) || !graph.Execute(error) || !error.empty()) return fail(error);
            if (forward.GetLastDrawCount() != 1 || (!blend && shadow.GetLastDrawCount() == 0)
                || (skin && !blend && shadow.GetLastSkinnedDrawCount() == 0))
                return fail("Coverage fixture omitted a pass/skin draw");
            if (!resources.EndFrame(error)) return fail(error);
            frameOpen = false; resources.WaitForGpu();
            std::array<RHIReadbackImage, 5> images;
            for (uint32_t i = 0; i < images.size(); ++i)
                if (!resources.MapReadback(readbacks[i], images[i], error)) return fail(error);
            for (uint32_t y = 8; y < 24; ++y)
            for (uint32_t x = 8; x < 24; ++x)
            {
                const bool visible = test.visible == 1.f || (test.visible == .5f && x >= 16);
                const float color = visible ? (blend ? test.alpha : 1.f) : 0.f;
                const float depthValue = visible && !blend ? .5f : 1.f;
                const auto check = [&](float actual, float expected, const char* attachment) {
                    if (std::isfinite(actual))
                        capture.maxRouteDelta = (std::max)(capture.maxRouteDelta, std::fabs(actual - expected));
                    if (std::isfinite(actual) && std::fabs(actual - expected) <= .002f) return true;
                    error = "Coverage owned=" + std::to_string(owned) + " case=" + std::to_string(index) + " skin=" + std::to_string(skin)
                        + " " + attachment + " pixel=" + std::to_string(x) + "," + std::to_string(y)
                        + " actual=" + std::to_string(actual) + " expected=" + std::to_string(expected);
                    return false;
                };
                if (!check(images[0].At(x, y, 0), visible && !blend ? 1.f : 0.f, "GBuffer")
                    || !check(images[0].At(x, y, 3), visible && !blend ? 1.f : 0.f, "GBuffer alpha")
                    || !check(images[2].At(x, y, 0), depthValue, "depth")
                    || !check(images[3].At(x, y, 0), color, "Forward")) return fail(error);
                if (visible && !blend && !check(images[1].At(x, y, 2), test.back ? 0.f : 1.f, "normal"))
                    return fail(error);
                capture.rgb.push_back(images[0].At(x, y, 0));
                capture.rgb.push_back(images[2].At(x, y, 0));
                capture.rgb.push_back(images[3].At(x, y, 0));
            }
            uint32_t written = 0;
            for (uint32_t y = 0; y < images[4].height; ++y)
            for (uint32_t x = 0; x < images[4].width; ++x)
            {
                const float value = images[4].At(x, y, 0);
                if (!std::isfinite(value)) return fail("Nonfinite shadow coverage depth");
                if (value < .99999f) ++written;
            }
            if (index == 0) fullShadowPixels = written;
            const float ratio = fullShadowPixels ? float(written) / fullShadowPixels : -1.f;
            if (fullShadowPixels < 100 || std::fabs(ratio - (blend ? 0.f : test.visible)) > .015f)
                return fail("Coverage shadow case=" + std::to_string(index) + " ratio=" + std::to_string(ratio));
            capture.rgb.push_back(ratio);
            ++capture.cases;
        }
        cleanup(); return true;
    }
}

bool RunPbrCoverageTest(std::string& outLog)
{
    std::array<PbrParityCapture, 2> captures;
    std::string error, validation;
    {
        DX12DeviceResources resources;
        DX12PSOManager pipelines;
        DX12RootSignatureCache roots;
        DX12MeshCache meshes;
        DX12TextureCache textures;
        if (!resources.Initialize(kPbrSize, kPbrSize, error)
            || !pipelines.Initialize(&resources, L"dx12_pbr_coverage.cache", error)
            || !roots.Initialize(&resources, error) || !meshes.Initialize(&resources, error)
            || !textures.Initialize(&resources, error)) { outLog += error; return false; }
        const bool captured = CapturePbrCoverage(resources, pipelines, roots, meshes, textures,
            [&] { meshes.BeginFrame(0); textures.BeginFrame(0); }, captures[0], error);
        const auto problems = resources.DrainDebugMessages(validation);
        textures.Shutdown(); meshes.Shutdown(); roots.Shutdown(); pipelines.Shutdown(); resources.Shutdown();
        if (!captured || problems != 0) { outLog += "DX12: " + error + validation; return false; }
    }
    {
        VulkanDeviceResources resources;
        VulkanPipelineCache pipelines;
        VulkanMeshCache meshes;
        VulkanTextureCache textures;
        if (!VulkanApi::LoadLoader(error) || !resources.Initialize(kPbrSize, kPbrSize, true, error))
        { outLog += error; return false; }
        pipelines.Initialize(resources.GetDevice());
        resources.SetPipelineCache(&pipelines);
        if (!meshes.Initialize(&resources, error) || !textures.Initialize(&resources, error))
        { outLog += error; return false; }
        RHIShaderCompiler::ScopedOutput output{RHIShaderBinary::SpirV};
        const bool captured = CapturePbrCoverage(resources, pipelines, pipelines, meshes, textures,
            [&] { meshes.BeginFrame(0); }, captures[1], error);
        const auto stubs = resources.GetUnimplementedCount() + resources.GetEncoderUnimplementedCount();
        const auto problems = resources.DrainDebugMessages(validation);
        textures.Shutdown(); meshes.Shutdown(); pipelines.Shutdown(); resources.Shutdown();
        if (!captured || problems != 0 || stubs != 0)
        { outLog += "Vulkan: " + error + validation; return false; }
    }
    if (captures[0].cases != 40 || captures[1].cases != 40
        || captures[0].rgb.size() != captures[1].rgb.size()) return false;
    float maxBackendDelta = 0.f;
    for (std::size_t i = 0; i < captures[0].rgb.size(); ++i)
    {
        const float a = captures[0].rgb[i], b = captures[1].rgb[i];
        const float delta = std::fabs(a - b);
        maxBackendDelta = (std::max)(maxBackendDelta, delta);
        if (delta > 0.002f + 0.005f * (std::max)(std::fabs(a), std::fabs(b)))
        { outLog += "PBR coverage backend mismatch at " + std::to_string(i); return false; }
    }
    char line[320]{};
    std::snprintf(line, sizeof(line),
        "PBR coverage: 40 cases/backend (legacy/owned, static/skin), expected max error DX12=%g Vulkan=%g, backend=%g; validation=0\n",
        captures[0].maxRouteDelta, captures[1].maxRouteDelta, maxBackendDelta);
    outLog += line;
    return true;
}
