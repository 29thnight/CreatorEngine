#include "TextureSourceProcessing.h"
#include "Interfaces/AssetAuthoringPort.h"
#include "Experiment/Cooked/CookedTerrain.h"
#include "Experiment/Cooked/CookedTexture.h"
#include "AuthoringParsedDocument.h"

#include <array>
#include <bit>
#include <cmath>
#include <fstream>
#include <limits>

namespace Authoring
{
    namespace
    {
        struct TerrainSourceLocks final
        {
            std::vector<HANDLE> files;
            ~TerrainSourceLocks()
            {
                for (const auto file : files)
                {
                    CloseHandle(file);
                }
            }
            bool Pin(const std::filesystem::path& path)
            {
                const auto handle = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ,
                    nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
                if (handle == INVALID_HANDLE_VALUE)
                {
                    return false;
                }
                try
                {
                    files.push_back(handle);
                }
                catch (...)
                {
                    CloseHandle(handle);
                    throw;
                }
                return true;
            }
        };

        bool ReadTerrainCookBytes(const std::filesystem::path& path, std::uint64_t maximum,
            std::vector<std::byte>& bytes)
        {
            std::ifstream input(path, std::ios::binary | std::ios::ate);
            const auto size = input.tellg();
            if (!input || size <= 0 || static_cast<std::uint64_t>(size) > maximum)
            {
                return false;
            }
            bytes.resize(static_cast<std::size_t>(size));
            input.seekg(0);
            input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
            return input && input.peek() == std::char_traits<char>::eof();
        }
    }

    bool CookTerrainSource(const std::filesystem::path& source, const std::filesystem::path& terrainRoot,
        std::vector<std::byte>& artifact, std::string& failure)
    {
        namespace ck = experiment::cooked;
        artifact.clear();
        failure.clear();
        const auto fail = [&](std::string message)
        {
            failure = std::move(message);
            return false;
        };
        try
        {
            TerrainSourceLocks locks;
            std::vector<std::byte> descriptor;
            if (!locks.Pin(source) || !ReadTerrainCookBytes(source, 16u * 1024u * 1024u, descriptor))
            {
                return fail("Cannot capture terrain descriptor: " + source.string());
            }
            const auto document = ParsedDocument::ParseText(
                std::string(reinterpret_cast<const char*>(descriptor.data()), descriptor.size()), failure);
            if (!document)
            {
                return false;
            }
            const auto root = document.Root();
            const auto layers = root["layers"];
            const auto splats = root["splatmaps"];
            if (!root.IsMap() || root["schemaVersion"].As<int>(0) != 1 || !root["heightmap"].IsScalar()
                || !layers.IsSequence() || !splats.IsSequence() || layers.Size() != splats.Size()
                || layers.Size() > ck::kCookedTerrainMaxLayers)
            {
                return fail("Terrain source schema is invalid.");
            }
            ck::CookedTerrainView view;
            view.terrainId = root["terrainID"].As<std::uint32_t>();
            view.width = root["width"].As<std::uint32_t>();
            view.height = root["height"].As<std::uint32_t>();
            view.minHeight = root["minHeight"].As<float>();
            view.maxHeight = root["maxHeight"].As<float>();
            view.layerCount = static_cast<std::uint32_t>(layers.Size());
            if (view.width == 0u || view.height == 0u || view.width > ck::kCookedTextureMaxDimension
                || view.height > ck::kCookedTextureMaxDimension || !std::isfinite(view.minHeight)
                || !std::isfinite(view.maxHeight) || view.minHeight > view.maxHeight)
            {
                return fail("Terrain source dimensions exceed the cooked limits.");
            }
            const auto resolve = [&](const ReadNode& value)
            {
                const auto stored = std::filesystem::u8path(value.AsString());
                return stored.is_absolute() ? stored : terrainRoot / stored;
            };
            const auto heightPath = resolve(root["heightmap"]);
            TerrainSourceImage heights;
            if (!locks.Pin(heightPath) || !AssetAuthoringPort::ReadTerrainSourceImage(
                heightPath, TerrainSourceImageKind::HeightBits, heights)
                || heights.width != view.width || heights.height != view.height)
            {
                return fail("Cannot capture exact terrain height image: " + heightPath.string());
            }
            std::vector<std::byte> heightBytes(heights.heights.size() * 4u);
            for (std::size_t index = 0u; index < heights.heights.size(); ++index)
            {
                const auto bits = std::bit_cast<std::uint32_t>(heights.heights[index]);
                for (std::size_t byte = 0u; byte < 4u; ++byte)
                {
                    heightBytes[index * 4u + byte] = static_cast<std::byte>((bits >> (byte * 8u)) & 0xffu);
                }
            }
            view.heights = heightBytes;
            std::array<TerrainSourceImage, ck::kCookedTerrainMaxLayers> masks;
            std::array<std::vector<std::byte>, ck::kCookedTerrainMaxLayers> textures;
            std::array<std::string, ck::kCookedTerrainMaxLayers> names;
            std::array<std::string, ck::kCookedTerrainMaxLayers> references;
            std::size_t index{};
            for (const auto layer : layers)
            {
                if (!layer.IsMap() || !layer["layerName"].IsScalar() || !layer["diffuseTexturePath"].IsScalar())
                {
                    return fail("Terrain layer schema is invalid.");
                }
                auto& output = view.layers[index];
                output.id = layer["layerID"].As<std::uint32_t>();
                output.tiling = layer["tiling"].As<float>();
                names[index] = layer["layerName"].AsString();
                references[index] = layer["diffuseTexturePath"].AsString();
                output.name = names[index];
                output.diffuseReference = references[index];
                const auto splatPath = resolve(splats.At(index));
                if (!locks.Pin(splatPath) || !AssetAuthoringPort::ReadTerrainSourceImage(
                    splatPath, TerrainSourceImageKind::Gray8, masks[index])
                    || masks[index].width != view.width || masks[index].height != view.height)
                {
                    return fail("Cannot capture terrain gray8 splat: " + splatPath.string());
                }
                output.gray = std::as_bytes(std::span(masks[index].gray));
                const auto diffusePath = resolve(layer["diffuseTexturePath"]);
                std::vector<std::byte> diffuse;
                if (!locks.Pin(diffusePath) || !ReadTerrainCookBytes(diffusePath, 512ull * 1024ull * 1024ull, diffuse))
                {
                    return fail("Cannot capture terrain diffuse source: " + diffusePath.string());
                }
                ck::TextureImportSettings settings;
                const auto metaPath = std::filesystem::path(diffusePath.wstring() + L".meta");
                if (std::filesystem::is_regular_file(metaPath))
                {
                    std::vector<std::byte> metadata;
                    if (!locks.Pin(metaPath) || !ReadTerrainCookBytes(metaPath, 16u * 1024u * 1024u, metadata)
                        || !ck::ParseTextureImportSettings(std::string_view(
                            reinterpret_cast<const char*>(metadata.data()), metadata.size()), settings, failure))
                    {
                        return fail("Cannot capture terrain diffuse import settings: " + metaPath.string() + ": " + failure);
                    }
                }
                if (!ck::CookTexture(diffuse, settings, textures[index], failure))
                {
                    return fail("Terrain diffuse cook failed: " + diffusePath.string() + ": " + failure);
                }
                output.texture = textures[index];
                ++index;
            }
            return ck::EncodeCookedTerrain(view, artifact, failure);
        }
        catch (const std::exception& error)
        {
            return fail("Terrain source cook failed: " + std::string(error.what()));
        }
    }
}
