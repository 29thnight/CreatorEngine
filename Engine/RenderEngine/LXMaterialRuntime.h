#pragma once

#include "MaterialPropertyValue.h"
#include "ShaderMetaHandle.h"
#include "ShaderMetaReflection.h"

#include <memory>
#include <span>

class Texture;
struct MaterialTextureOwner
{
    std::string propertyName;
    std::shared_ptr<Texture> textureOwner;
};

// Engine adapter for LX execution. Graph authoring remains renderer-neutral;
// ShaderMeta is a contract description, not the owner of material instances.
namespace LX::Runtime
{
struct ShaderGeneration
{
    std::filesystem::path metaPath;
    std::string document;
    std::string source;
    ShaderMeta meta;
    ShaderMetaBindingLayout layout;
    // Registry identity for authored Code inputs; graphics requests are LX-owned.
    ShaderMetaHandle codeHandle;
};

struct Instance
{
    std::shared_ptr<const ShaderGeneration> shader;
    std::vector<MaterialPropertyValue> properties;
    std::vector<std::uint8_t> uniforms;
    std::vector<MaterialTextureOwner> textureOwners;
    std::vector<std::uint16_t> keywordSelections;
};

bool CreateCodeShader(const ShaderMeta& meta, const ShaderMetaBindingLayout& layout,
                      ShaderMetaHandle handle, std::shared_ptr<const ShaderGeneration>& result,
                      std::string& error);
bool BuildInstance(std::shared_ptr<const ShaderGeneration> shader,
                   std::span<const MaterialPropertyValue> values,
                   std::span<const std::uint16_t> keywords,
                   std::span<const MaterialTextureOwner> textures,
                   std::shared_ptr<const Instance>& result, std::string& error);
bool SetValue(const Instance& instance, const MaterialPropertyValue& value,
              std::shared_ptr<const Instance>& result, std::string& error);
bool SetTextureOwner(const Instance& instance, std::string_view name, std::shared_ptr<Texture> owner,
                     std::shared_ptr<const Instance>& result, std::string& error);
bool SetKeyword(const Instance& instance, std::string_view axis, std::string_view value,
                std::shared_ptr<const Instance>& result, std::string& error);
} // namespace LX::Runtime
