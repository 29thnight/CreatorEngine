#pragma once

#include "../Core/LXNodeDefinition.h"

#include <filesystem>
#include <string_view>

namespace LX
{
    std::shared_ptr<const LXNodeDefinitionRegistry> CreateInputDefinitions();
    const std::vector<std::string>& InputNodeTypes();

    class LXInputAsset final
    {
    public:
        LXInputAsset();

        // The canonical asset UUID, not a display name or an editor document ID.
        std::string graphId;
        LXGraph graph;

        // Exact canonical semantic text. Layout, presentation titles and allocator
        // next-ID state are deliberately excluded; no hash collision can hide an edit.
        std::string SemanticIdentity() const;
        bool Save(const std::filesystem::path& path, std::string* error = nullptr) const;
        static std::optional<LXInputAsset> Load(const std::filesystem::path& path, std::string* error = nullptr);
    };

    class LXInputArchive final
    {
    public:
        static constexpr std::uint32_t SchemaVersion = 1;
        static std::string Write(const LXInputAsset& asset);
        static std::optional<LXInputAsset> Read(std::string_view text, std::string* error = nullptr);
    };
}
