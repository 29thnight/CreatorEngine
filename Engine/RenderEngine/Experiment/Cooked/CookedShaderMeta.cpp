#include "CookedShaderMeta.h"

namespace experiment::cooked
{
    bool ReadShaderMetaArtifact(std::span<const std::byte> bytes,
        const AssetId& assetId, ShaderMeta& result, std::string& failure)
    {
        if (!IsAssetIdV4(assetId))
        {
            failure = "Authored shader metadata requires a canonical UUIDv4 identity.";
            return false;
        }
        return ShaderMetaLoader::ParseCookedMetadata(bytes, FileGuid{ assetId.value }, result, failure);
    }
}
