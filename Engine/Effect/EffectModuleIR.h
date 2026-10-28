#pragma once
#include "../Containers/ContainerAPI.h"
#include "EffectDefinition.h"

namespace ce::effects
{
// v1 is an in-memory compiler contract, not a serialized/cooked asset format.
enum class ModuleOpcode : std::uint8_t { Constant, Input, ReadAttribute, DeltaSeconds, Add, Multiply, ExpNeg };
struct ModuleNode
{
    DefinitionId id;
    ModuleOpcode opcode{ModuleOpcode::Constant};
    ce::dynamic_array<DefinitionId> arguments;
    EffectValue literal{0.f};
    DefinitionId port;
    ParticleAttribute attribute{ParticleAttribute::Velocity};
};
struct ModuleOutput { ParticleAttribute attribute; DefinitionId node; };
struct ModuleGraph { ce::dynamic_array<ModuleNode> nodes; ce::dynamic_array<ModuleOutput> outputs; };
struct ParticleValues
{
    float age{}, lifetime{1}, size{1};
    Float3 position{}, velocity{};
    Color4 color{};
    AttributeMask initialized{Attribute(ParticleAttribute::Age)};
};
struct ModuleEvaluationContext
{
    float deltaSeconds{};
    ce::dynamic_array<EffectParameter> parameters; // Typed overrides; omitted IDs use compiled defaults.
    std::uint64_t effectSeed{}, emitterSeed{}, particleId{}, simulationTick{};
    DefinitionId emitterId;
};
struct ModuleInstruction
{
    DefinitionId node;
    ModuleOpcode opcode;
    InputType type;
    ce::dynamic_array<std::size_t> arguments;
    EffectValue literal;
    DefinitionId port;
    ParticleAttribute attribute;
};
struct ModuleCompileResult;
class CompiledEffectModule
{
public:
    const ce::dynamic_array<ModuleInstruction>& Instructions() const { return instructions_; }
    EffectStage Stage() const { return stage_; }
private:
    friend struct ModuleCompileResult;
    friend ModuleCompileResult CompileEffectModule(const ModuleDefinition&, const ModuleInstance&,
        const ModuleGraph&, const ce::dynamic_array<EffectParameter>&);
    friend ce::dynamic_array<EffectDiagnostic> EvaluateEffectModules(const ce::dynamic_array<CompiledEffectModule>&,
        ParticleValues&, const ModuleEvaluationContext&);
    DefinitionId instance_;
    bool enabled_{};
    EffectStage stage_{EffectStage::Initialize};
    AttributeMask reads_{}, writes_{};
    ce::dynamic_array<ModuleInstruction> instructions_;
    ce::dynamic_array<ce::pair<ParticleAttribute, std::size_t>> outputs_;
    ce::dynamic_array<ModuleInputDefinition> ports_;
    ce::dynamic_array<InputBinding> inputs_;
    ce::dynamic_array<EffectParameter> parameters_;
};
struct ModuleCompileResult
{
    std::optional<CompiledEffectModule> module;
    ce::dynamic_array<EffectDiagnostic> diagnostics;
};
ModuleCompileResult CompileEffectModule(const ModuleDefinition&, const ModuleInstance&, const ModuleGraph&,
    const ce::dynamic_array<EffectParameter>& parameters = {});
ModuleCompileResult CompileBuiltinEffectModule(const ModuleInstance&,
    const ce::dynamic_array<EffectParameter>& parameters = {});
// Ordered stack evaluation is transactional: errors leave the particle unchanged.
// Attribute reads observe module-entry state; outputs commit together per module.
ce::dynamic_array<EffectDiagnostic> EvaluateEffectModules(const ce::dynamic_array<CompiledEffectModule>&,
    ParticleValues&, const ModuleEvaluationContext&);
// Distribution v1: counter-mixed FNV identity; independent component streams.
// Exposed so the CPU oracle and future GPU lowering share exact sample vectors.
EffectValue SampleEffectRandom(const RandomInput&, std::uint64_t key);
ce::dynamic_array<EffectDiagnostic> StepEffectParticle(const ce::dynamic_array<CompiledEffectModule>& update,
    ParticleValues&, bool& alive, const ModuleEvaluationContext&);
}
