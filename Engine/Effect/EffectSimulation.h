#pragma once
#include "EffectModuleIR.h"
#include "../RenderEngine/Render/Graph/EnhancedRenderGraph.h"

namespace ce::effects
{
inline constexpr std::uint32_t EffectGpuMaxParticles = 4096;
// Minimal fixed-kernel ABI, separate from the general module IR/GPU compiler.
struct EffectGpuParticle
{
    Float3 position; float age{};
    Float3 velocity; float lifetime{};
    Color4 color;
    float size{}; std::uint32_t alive{}, id{}, reserved{};
};
struct EffectGpuStep
{
    std::uint32_t capacity{}, requested{}, flags{}, reserved{}; // flags: Gravity=1, Drag=2
    float deltaSeconds{}, lifetime{1}, size{1}, drag{};
    Float3 velocity; float reserved2{};
    Float3 acceleration; float reserved3{};
};
static_assert(sizeof(EffectGpuParticle)==64 && sizeof(EffectGpuStep)==64);
enum class EffectGpuPhase { Initialize, Update };
// Caller retains graph, device and pipelines through recording/completion. The
// pass captures only value parameters; no component or GT state is borrowed.
bool AddEffectSimulationPass(EnhancedRenderGraph&, RGHandle particles, IRenderDeviceServices&,
    RHIPipelineHandle pipeline, EffectGpuPhase, const EffectGpuStep&, std::string& error);
}
