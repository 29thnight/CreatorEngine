#pragma once

#include "AssetLink.h"
#include "AssetRequest.h"
#include "../Experiment/Cooked/CookedAssetCatalog.h"
#include "../../Utility_Framework/InputGraph.h"

#include <map>
#include <vector>

namespace AssetDepot
{
    struct InputGraphAssetKey final
    {
        experiment::cooked::TypedAssetReference asset{};
        experiment::cooked::AssetBlobRecord blob{};
        std::uint64_t resolverRevision{};
        auto operator<=>(const InputGraphAssetKey&) const = default;
    };

    struct InputGraphAssetWork final
    {
        InputGraphAssetKey key{};
        experiment::cooked::ResolvedAssetEntry resolved{};
        std::uint64_t epoch{};
        std::vector<own::weak_owner<AssetRequestState<Input::InputGraphProgram>>> consumers;
        job_handle completion{};
        bool complete{};
    };

    struct InputGraphAssetEntry final
    {
        own::weak_owner<const Input::InputGraphProgram> live;
        own::shared_owner<InputGraphAssetWork> inFlight;
    };
}
