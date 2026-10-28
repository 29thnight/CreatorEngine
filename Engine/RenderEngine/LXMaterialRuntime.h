#include "../Containers/ContainerAPI.h"
#pragma once
#if defined(CE_CONTRACT_STANDALONE)
#include "../../Tools/build/ModuleAdapters/Creator.Core.Ownership.h"
#else
import Creator.Core.Ownership;
#endif

#include "MaterialPropertyValue.h"
#include "ShaderMetaHandle.h"
#include "ShaderMetaReflection.h"

#include "../Utility_Framework/Foundation/StandardLibrary.h"

namespace AssetDepot
{
    struct MaterialProgramAssetOrigin;
}

class Texture;
struct [[reflgen::runtime, reflgen::public_fields, reflgen::public_methods]] MaterialTextureOwner
{
    std::string propertyName;
    own::shared_owner<const Texture> textureOwner;
};

// Engine adapter for LX execution. Graph authoring remains renderer-neutral;
// ShaderMeta is a contract description, not the owner of material instances.
namespace LX::Runtime
{
    struct [[reflgen::runtime, reflgen::public_fields, reflgen::public_methods]] ShaderGeneration
    {
        std::filesystem::path metaPath;
        std::string document;
        std::string source;
        ShaderMeta meta;
        ShaderMetaBindingLayout layout;
        // Registry identity for authored Code inputs; graphics requests are LX-owned.
        ShaderMetaHandle codeHandle;
        own::shared_owner<const experiment::cooked::CodeProgram> codeProgram{};
        own::shared_owner<const AssetDepot::MaterialProgramAssetOrigin> assetOrigin{};
    };

    struct [[reflgen::runtime, reflgen::public_fields, reflgen::public_methods]] Instance
    {
        Instance() = default;
        Instance(const Instance& source)
            : shader(source.shader), properties(source.properties), uniforms(source.uniforms),
              textureOwners(source.textureOwners), keywordSelections(source.keywordSelections)
        {
        }
        Instance(Instance&&) noexcept = default;
        Instance& operator=(const Instance& source)
        {
            if (this != &source)
            {
                Instance candidate(source);
                *this = std::move(candidate);
            }
            return *this;
        }
        Instance& operator=(Instance&&) noexcept = default;

        // One ID for an immutable uniform/texture representation. Copying into
        // new editable construction state creates a fresh ID; moving preserves it.
        std::uint64_t representationId{ static_cast<std::uint64_t>(TypeTrait::MakeRuntimeResourceId()) };
        own::shared_owner<const ShaderGeneration> shader;
        ce::dynamic_array<MaterialPropertyValue> properties;
        ce::dynamic_array<std::uint8_t> uniforms;
        ce::dynamic_array<MaterialTextureOwner> textureOwners;
        ce::dynamic_array<std::uint16_t> keywordSelections;
    };

    [[nodiscard]] bool ValidateShaderGeneration(const ShaderGeneration& shader, std::string& error);

    bool CreateCodeShader(const ShaderMeta& meta, const ShaderMetaBindingLayout& layout,
                          ShaderMetaHandle handle, own::shared_owner<const ShaderGeneration>& result,
                          std::string& error);
    bool BuildInstance(own::shared_owner<const ShaderGeneration> shader,
                       std::span<const MaterialPropertyValue> values,
                       std::span<const std::uint16_t> keywords,
                       std::span<const MaterialTextureOwner> textures,
                       own::shared_owner<const Instance>& result, std::string& error);
    bool SetValue(const Instance& instance, const MaterialPropertyValue& value,
                  own::shared_owner<const Instance>& result, std::string& error);
    bool SetTextureOwner(const Instance& instance, std::string_view name, own::shared_owner<const Texture> owner,
                         own::shared_owner<const Instance>& result, std::string& error);
    bool SetKeyword(const Instance& instance, std::string_view axis, std::string_view value,
                    own::shared_owner<const Instance>& result, std::string& error);
} // namespace LX::Runtime
