#pragma once

#include "Experiment/MaterialResolver.h"

namespace experiment::cooked
{
    class CookedAssetCatalog;
}
namespace assets
{
    class ModelAssetGeneration;
}

namespace experiment
{
    // MaterialResolveServices의 제품 바인딩 — DataSystem 정본(ShaderMeta handle
    // cache·shared texture 로더·AssetMetaRegistry 경로 해석)에 잇는다. catalog를
    // 주면 texture artifact를 cooked 우선으로 해석한다(CookedAssetCatalog의 생산
    // 소비 지점, I5-M2). catalog 수명은 호출자가 서비스보다 길게 보장해야 한다.
    // MBC7 — generation을 주면 그 closure의 embedded texture를 첫 축으로 푼다
    // (DataSystem::ResolveModelGenerationTexture). generation 수명도 호출자 몫이다
    // (drawPool의 generationSource가 sealing 동안 붙든다).
    [[nodiscard]] MaterialResolveServices MakeDataSystemMaterialResolveServices(
        const cooked::CookedAssetCatalog* catalog,
        const assets::ModelAssetGeneration* generation = nullptr);
}
