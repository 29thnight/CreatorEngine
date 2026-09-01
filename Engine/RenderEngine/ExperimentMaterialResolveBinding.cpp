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
            // I7-C2 — 해석은 catalog가 아니라 DataSystem 창구를 탄다. 신선도
            // (stale 집합)는 마운트가 판정한 DataSystem 상태이지 표의 성질이
            // 아니기 때문이다. catalog 인자는 여전히 "cooked 해석을 켤 것인가"의
            // 정본이다 — nullptr이면 이 서비스가 아예 없다(M2 계약, 자가 검증이
            // 그 대우를 단정한다).
            services.resolveCookedArtifactPath = [](const AssetId& id)
                {
                    return DataSystems->ResolveCookedArtifact(id);
                };
        }
        return services;
    }
}
