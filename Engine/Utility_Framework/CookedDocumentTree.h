#pragma once
#include "StandardLibrary.h"

// CEDO is an engine runtime format. This decoder owns its tree and has no
// authoring parser, reflection writer, renderer, or diagnostic backend.
namespace Cedo
{
    inline constexpr std::uint16_t version = 1;
    inline constexpr std::size_t header_bytes = 16;
    inline constexpr std::size_t max_document_bytes = 64u * 1024u * 1024u;
    inline constexpr std::uint32_t max_nodes = 1'000'000;
    inline constexpr std::uint32_t max_scalar_bytes = 16u * 1024u * 1024u;
    inline constexpr std::uint32_t max_depth = 512;

    enum class Kind : std::uint8_t { Null = 1, Scalar, Map, Sequence };

    struct Node
    {
        Kind kind = Kind::Null;
        std::string key;
        std::string value;
        std::vector<Node> children;

        const Node* Find(std::string_view name) const noexcept
        {
            if (kind != Kind::Map) return nullptr;
            for (const auto& child : children)
                if (child.key == name) return &child;
            return nullptr;
        }
    };

    struct Document
    {
        Node root;
        std::uint32_t nodes = 0;
    };

    [[nodiscard]] std::optional<Document> Decode(std::span<const std::byte> bytes, std::string& error);
}
