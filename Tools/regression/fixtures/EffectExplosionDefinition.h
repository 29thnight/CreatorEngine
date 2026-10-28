#pragma once
#include "../../../Engine/Effect/EffectDefinition.h"

namespace ce::effects::fixtures
{
inline EffectDefinition MakeExplosionDefinition()
{
    EffectDefinition effect;
    effect.id = "example.explosion"; effect.name = "Explosion"; effect.seed = 42;
    effect.parameters = {{"tint", "Tint", Color4{1,.5f,.05f,1}}};
    EmitterDefinition emitter;
    emitter.id = "sparks"; emitter.name = "Sparks"; emitter.seed = 7; emitter.material = "example.spark.material";
    emitter.stacks = {
        {EffectStage::Spawn, {{"burst", "ce.burst"}}},
        {EffectStage::Initialize, {
            {"life", "ce.lifetime", 1, true, {{"seconds", RandomInput{.4f,1.2f}}}},
            {"position", "ce.position.sphere"}, {"velocity", "ce.velocity.cone"}, {"size", "ce.size"},
            {"color", "ce.color", 1, true, {{"color", ParameterInput{"tint"}}}}}},
        {EffectStage::Update, {
            {"gravity", "ce.gravity"}, {"drag", "ce.drag"},
            {"fade", "ce.color.life", 1, true, {{"color", GradientInput{{{0,{1,1,0,1}},{1,{1,.1f,0,0}}}}}}},
            {"shrink", "ce.size.life", 1, true, {{"size", CurveInput{{{0,.04f},{1,0}}}}}}}},
        {EffectStage::Render, {{"billboard", "ce.billboard"}}}
    };
    effect.emitters.push_back(std::move(emitter));
    return effect;
}
}
