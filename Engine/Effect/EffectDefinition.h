#pragma once
#include "../Utility_Framework/Foundation/StandardLibrary.h"

namespace ce::effects
{
// Persist these opaque IDs, not display names or positions. v1 IDs use 1..128
// ASCII letters/digits/._:-; collisions are rejected within their owning scope.
using DefinitionId = std::string;
inline constexpr std::uint32_t EffectSchemaVersion = 1;
enum class EffectStage : std::uint8_t { Spawn, Initialize, Update, Render };
enum class InputType : std::uint8_t { Boolean, UInt, Float, Float3, Color };
struct Float3 { float x{}, y{}, z{}; };
struct Color4 { float r{1}, g{1}, b{1}, a{1}; };
using EffectValue = std::variant<bool, std::uint32_t, float, Float3, Color4>;
enum class RandomSampling : std::uint8_t { AtSpawn, EachTick };
struct ConstantInput { EffectValue value{0.f}; };
struct RandomInput { EffectValue minimum{0.f}, maximum{1.f}; RandomSampling sampling{RandomSampling::AtSpawn}; };
struct CurveKey { float time{}, value{}; };
struct GradientKey { float time{}; Color4 value{}; };
struct CurveInput { ce::dynamic_array<CurveKey> keys; };
struct GradientInput { ce::dynamic_array<GradientKey> keys; };
struct ParameterInput { DefinitionId parameter; };
using InputExpression = std::variant<ConstantInput, RandomInput, CurveInput, GradientInput, ParameterInput>;
struct InputBinding { DefinitionId port; InputExpression expression; };
struct EffectParameter { DefinitionId id; std::string name; EffectValue defaultValue{0.f}; };

enum class ParticleAttribute : std::uint32_t
{
    Age = 1, Lifetime = 2, Position = 4, Velocity = 8, Size = 16, Color = 32
};
using AttributeMask = std::uint32_t;
constexpr AttributeMask Attribute(ParticleAttribute value) { return static_cast<AttributeMask>(value); }
enum class ModuleOperation : std::uint8_t { Attributes, Emit, Draw };
enum class InputDomain : std::uint8_t { Finite, Nonnegative, Positive };
struct ModuleInputDefinition
{
    DefinitionId id;
    InputType type{InputType::Float};
    EffectValue defaultValue{0.f};
    InputDomain domain{InputDomain::Finite};
};
struct ModuleDefinition
{
    DefinitionId id;
    std::uint32_t version{1};
    EffectStage stage{EffectStage::Initialize};
    ce::dynamic_array<ModuleInputDefinition> inputs;
    AttributeMask reads{}, writes{};
    ModuleOperation operation{ModuleOperation::Attributes};
};
struct ModuleInstance
{
    DefinitionId id;
    DefinitionId definition;
    std::uint32_t definitionVersion{1};
    bool enabled{true};
    ce::dynamic_array<InputBinding> inputs;
};
struct ModuleStack { EffectStage stage{EffectStage::Initialize}; ce::dynamic_array<ModuleInstance> modules; };
enum class SpawnOverflow : std::uint8_t { DropNewest };
enum class SimulationSpace : std::uint8_t { Local, World };
struct EmitterDefinition
{
    DefinitionId id;
    std::string name;
    std::uint32_t capacity{1024};
    std::uint64_t seed{};
    double startSeconds{};
    SpawnOverflow overflow{SpawnOverflow::DropNewest};
    SimulationSpace space{SimulationSpace::Local};
    DefinitionId material; // Asset resolution belongs to the cook/runtime consumer.
    ce::dynamic_array<ModuleStack> stacks;
};
// Fixed simulation time, independent of view count. Pause emits no ticks; Step
// emits exactly one fixed tick. maxSubsteps limits work, retaining all remaining
// time as backlog. No silent time discard; timeScale applies to incoming time.
struct EffectClock
{
    double fixedStepSeconds{1.0 / 60.0};
    double timeScale{1.0};
    std::uint32_t maxSubsteps{8};
};
struct EffectDefinition
{
    std::uint32_t schemaVersion{EffectSchemaVersion};
    DefinitionId id;
    std::string name;
    std::uint64_t seed{};
    EffectClock clock;
    ce::dynamic_array<EffectParameter> parameters;
    ce::dynamic_array<EmitterDefinition> emitters;
};
struct EffectDiagnostic { std::string code, path, message; };
bool IsEffectDefinitionId(std::string_view id);
InputType TypeOf(const EffectValue& value);
ce::dynamic_array<ModuleDefinition> BuiltinEffectModules();
ce::dynamic_array<EffectDiagnostic> ValidateEffectInput(const InputExpression&, const ModuleInputDefinition&,
    EffectStage, const ce::dynamic_array<EffectParameter>&, const std::string& path);
ce::dynamic_array<EffectDiagnostic> ValidateEffectDefinition(const EffectDefinition& effect,
    const ce::dynamic_array<ModuleDefinition>& catalog);

// Wire-version 1 deterministic sample key, separate from random-value execution.
// AtSpawn ignores tick. EachTick includes it. View/frame IDs and stack indices
// are deliberately absent. Stable IDs survive renaming and stack reordering.
std::uint64_t EffectRandomKey(std::uint64_t effectSeed, std::uint64_t emitterSeed,
    std::uint64_t particleId, std::string_view emitterId, std::string_view moduleId,
    std::string_view portId, RandomSampling sampling, std::uint64_t simulationTick);
}
