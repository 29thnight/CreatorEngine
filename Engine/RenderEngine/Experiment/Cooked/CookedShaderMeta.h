#pragma once

#include "CookedAssetManifest.h"
#include "../../ShaderMeta.h"
#include "AuthoringCookedDocument.h"

namespace experiment::cooked
{
    inline constexpr std::uint32_t kShaderMetaDocumentRepresentation = 1u;
    inline constexpr std::uint32_t kShaderMetaDocumentVersion =
        (ShaderMeta::kSchemaVersion << 16u) | Authoring::kCookedDocumentVersion;
    inline constexpr std::size_t kShaderMetaDocumentMaxBytes = 1024u * 1024u;

    // CEMF v3 identity is supplied by the resolved record. CEDO contains only
    // descriptor values, so compatible bytes may serve multiple stable IDs.
    // This is CPU metadata readiness, not a shader compiler or GPU/PSO loader.
    [[nodiscard]] bool ReadShaderMetaArtifact(std::span<const std::byte> bytes,
        const AssetId& assetId, ShaderMeta& result, std::string& failure);
}
