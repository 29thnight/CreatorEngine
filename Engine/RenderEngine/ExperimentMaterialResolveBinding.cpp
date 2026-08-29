#include "ExperimentMaterialResolveBinding.h"

#include "DataSystem.h"
#include "Experiment/Cooked/CookedAssetCatalog.h"

namespace experiment
{
    MaterialResolveServices MakeDataSystemMaterialResolveServices(
        const cooked::CookedAssetCatalog* catalog)
    {
        MaterialResolveServices services;
        services.loadShaderMetaHandle =
            [](const FileGuid& guid, std::string& outError)
            {
                return DataSystems->LoadShaderMetaHandle(guid, outError);
            };
        services.resolveShaderMeta = [](const ShaderMetaHandle& handle)
            {
                return DataSystems->ResolveShaderMeta(handle);
            };
        services.loadTexture =
            [](const std::filesystem::path& path, bool compress)
            {
                return DataSystems->LoadSharedMaterialTexture(
                    path.string(), compress);
            };
        services.resolveSourcePath = [](const FileGuid& guid)
            {
                return DataSystems->GetFilePath(guid);
            };
        if (catalog)
        {
            services.resolveCookedArtifactPath = [catalog](const AssetId& id)
                {
                    return catalog->ResolveArtifactPath(id);
                };
        }
        return services;
    }
}
