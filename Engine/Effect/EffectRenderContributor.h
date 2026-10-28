#pragma once
#include "../RenderEngine/Effects/EffectFrame.h"
struct IRenderFeatureContributor;
namespace ce::effects
{
own::shared_owner<IRenderFeatureContributor> MakeEffectRenderContributor(own::shared_owner<EffectBridgeCounters> counters,
    own::shared_owner<const std::atomic<std::uint64_t>> sceneEpoch);
}
