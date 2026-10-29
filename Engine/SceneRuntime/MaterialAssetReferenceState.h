#pragma once
#include "../RenderEngine/Material.h"

// The last observed values distinguish an instance edit from an authored
// override that equals the next generation's default. Runtime metadata only;
// the persisted material schema and the reference's ownership are unchanged.
struct [[reflgen::runtime, reflgen::public_fields, reflgen::public_methods]] MaterialAssetReferenceState
{
    material_graph::InstanceDocument overrides;
    own::shared_owner<const material_graph::Instance> observed;
    bool overrideDoubleSided{};
    bool overrideBlendMode{};
    bool observedDoubleSided{};
    MaterialRenderingMode observedBlendMode = MaterialRenderingMode::Opaque;
};
