#include "../Utility_Framework/Foundation/EngineOrderedMap.h"
#include "../Utility_Framework/Foundation/EngineOrderedSet.h"
#include "EffectDefinition.h"

namespace ce::effects
{
bool IsEffectDefinitionId(std::string_view id)
{
    return !id.empty() && id.size() <= 128 && std::all_of(id.begin(), id.end(), [](unsigned char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
            c == '.' || c == '_' || c == ':' || c == '-';
    });
}
namespace
{
constexpr AttributeMask AllAttributes = 63;
ce::dynamic_array<double> Components(const EffectValue& v)
{
    return std::visit([](const auto& x) -> ce::dynamic_array<double> {
        using T = std::decay_t<decltype(x)>;
        if constexpr (std::is_same_v<T, Float3>) return {x.x, x.y, x.z};
        else if constexpr (std::is_same_v<T, Color4>) return {x.r, x.g, x.b, x.a};
        else return {static_cast<double>(x)};
    }, v);
}
bool Finite(const EffectValue& v)
{
    const auto c = Components(v);
    return std::all_of(c.begin(), c.end(), [](double x) { return std::isfinite(x); });
}
bool InDomain(const EffectValue& v, InputDomain domain)
{
    if (domain > InputDomain::Positive) return false;
    const auto values = Components(v);
    return std::all_of(values.begin(), values.end(), [domain](double x) {
        return std::isfinite(x) && (domain == InputDomain::Finite ||
            (domain == InputDomain::Nonnegative ? x >= 0 : x > 0));
    });
}
bool Ordered(const EffectValue& a, const EffectValue& b)
{
    if (TypeOf(a) != TypeOf(b)) return false;
    const auto x = Components(a), y = Components(b);
    for (std::size_t i = 0; i < x.size(); ++i) if (x[i] > y[i]) return false;
    return true;
}
bool ValidStage(EffectStage stage) { return stage >= EffectStage::Spawn && stage <= EffectStage::Render; }
const char* StageName(EffectStage stage)
{
    switch (stage) {
    case EffectStage::Spawn: return "spawn";
    case EffectStage::Initialize: return "initialize";
    case EffectStage::Update: return "update";
    case EffectStage::Render: return "render";
    }
    return "invalid";
}
}

InputType TypeOf(const EffectValue& value) { return static_cast<InputType>(value.index()); }

ce::dynamic_array<ModuleDefinition> BuiltinEffectModules()
{
    using A = ParticleAttribute;
    const auto ageLife = Attribute(A::Age) | Attribute(A::Lifetime);
    return {
        {"ce.burst", 1, EffectStage::Spawn, {{"count", InputType::UInt, std::uint32_t{80}}}, 0, 0, ModuleOperation::Emit},
        {"ce.lifetime", 1, EffectStage::Initialize, {{"seconds", InputType::Float, 1.f, InputDomain::Positive}}, 0, Attribute(A::Lifetime)},
        {"ce.position.sphere", 1, EffectStage::Initialize, {{"radius", InputType::Float, .15f, InputDomain::Nonnegative}}, 0, Attribute(A::Position)},
        {"ce.velocity.cone", 1, EffectStage::Initialize, {{"speed", InputType::Float, 5.f, InputDomain::Nonnegative}}, 0, Attribute(A::Velocity)},
        {"ce.size", 1, EffectStage::Initialize, {{"size", InputType::Float, .04f, InputDomain::Nonnegative}}, 0, Attribute(A::Size)},
        {"ce.color", 1, EffectStage::Initialize, {{"color", InputType::Color, Color4{}}}, 0, Attribute(A::Color)},
        {"ce.gravity", 1, EffectStage::Update, {{"acceleration", InputType::Float3, Float3{0,-9.8f,0}}}, Attribute(A::Velocity), Attribute(A::Velocity)},
        {"ce.drag", 1, EffectStage::Update, {{"coefficient", InputType::Float, .8f, InputDomain::Nonnegative}}, Attribute(A::Velocity), Attribute(A::Velocity)},
        {"ce.color.life", 1, EffectStage::Update, {{"color", InputType::Color, Color4{}}}, ageLife, Attribute(A::Color)},
        {"ce.size.life", 1, EffectStage::Update, {{"size", InputType::Float, 1.f, InputDomain::Nonnegative}}, ageLife, Attribute(A::Size)},
        {"ce.billboard", 1, EffectStage::Render, {}, Attribute(A::Position) | Attribute(A::Size) | Attribute(A::Color), 0, ModuleOperation::Draw}
    };
}

ce::dynamic_array<EffectDiagnostic> ValidateEffectInput(const InputExpression& expression,
    const ModuleInputDefinition& port, EffectStage stage,
    const ce::dynamic_array<EffectParameter>& parameterList, const std::string& path)
{
    ce::dynamic_array<EffectDiagnostic> errors;
    const auto error = [&](std::string code, std::string where, std::string message) {
        errors.push_back({std::move(code), std::move(where), std::move(message)});
    };
    const auto expected = port.type;
    const auto domain = port.domain;
    ce::engine_ordered_map<DefinitionId, const EffectParameter*> parameters;
    for (const auto& parameter : parameterList) parameters.emplace(parameter.id, &parameter);
        const auto value = [&](const EffectValue& v) {
            if (TypeOf(v) != expected) error("input.type", path, "Input type differs from declared port");
            if (!Finite(v)) error("input.nonfinite", path, "Input must be finite");
            if (!InDomain(v, domain)) error("input.value", path, "Input violates the port numeric domain");
        };
        std::visit([&](const auto& input) {
            using T = std::decay_t<decltype(input)>;
            if constexpr (std::is_same_v<T, ConstantInput>) value(input.value);
            else if constexpr (std::is_same_v<T, ParameterInput>) {
                const auto found = parameters.find(input.parameter);
                if (found == parameters.end()) error("input.parameter", path, "Unknown parameter ID");
                else value(found->second->defaultValue);
            } else if constexpr (std::is_same_v<T, RandomInput>) {
                value(input.minimum); value(input.maximum);
                if (expected == InputType::Boolean || expected == InputType::Color || !Ordered(input.minimum, input.maximum))
                    error("input.range", path, "Random supports ordered UInt/Float/Float3 ranges only");
                if (input.sampling > RandomSampling::EachTick || stage == EffectStage::Render ||
                    (stage == EffectStage::Initialize && input.sampling != RandomSampling::AtSpawn) ||
                    (stage == EffectStage::Spawn && input.sampling != RandomSampling::EachTick))
                    error("input.sampling", path, "Random sample time is incompatible with stage");
            } else {
                if constexpr (std::is_same_v<T, CurveInput>) {
                    if (expected != InputType::Float) error("input.type", path, "Curve requires Float port");
                } else {
                    if (expected != InputType::Color) error("input.type", path, "Gradient requires Color port");
                }
                if (stage != EffectStage::Update) error("input.domain", path, "Normalized particle life is available in Update only");
                if (input.keys.size() < 2 || input.keys.front().time != 0.f || input.keys.back().time != 1.f)
                    error("input.keys", path, "Keys must cover normalized life [0,1] with at least two keys");
                float previous = -1.f;
                for (const auto& key : input.keys) {
                    if (!std::isfinite(key.time) || key.time < 0 || key.time > 1 || key.time <= previous)
                        error("input.keys", path, "Key times must be finite and strictly increasing in [0,1]");
                    previous = key.time;
                    value(EffectValue{key.value});
                }
            }
        }, expression);
    return errors;
}

ce::dynamic_array<EffectDiagnostic> ValidateEffectDefinition(const EffectDefinition& e,
    const ce::dynamic_array<ModuleDefinition>& catalog)
{
    ce::dynamic_array<EffectDiagnostic> errors;
    const auto error = [&](std::string code, std::string path, std::string message) {
        errors.push_back({std::move(code), std::move(path), std::move(message)});
    };
    if (e.schemaVersion != EffectSchemaVersion) error("schema.version", "effect", "Unsupported schema version; explicit migration required");
    if (!IsEffectDefinitionId(e.id)) error("id.invalid", "effect", "Invalid stable ID");
    if (!std::isfinite(e.clock.fixedStepSeconds) || e.clock.fixedStepSeconds <= 0 ||
        !std::isfinite(e.clock.timeScale) || e.clock.timeScale < 0 || e.clock.maxSubsteps == 0)
        error("clock.invalid", "effect/clock", "Fixed step must be positive; scale nonnegative; substeps nonzero");
    ce::engine_ordered_map<DefinitionId, const ModuleDefinition*> modules;
    for (const auto& def : catalog) {
        const auto path = "catalog/" + def.id;
        if (!IsEffectDefinitionId(def.id) || !modules.emplace(def.id, &def).second) error("catalog.id", path, "Invalid or duplicate module ID");
        if (!def.version || !ValidStage(def.stage) || (def.reads | def.writes) & ~AllAttributes)
            error("catalog.contract", path, "Invalid module version, stage or attribute mask");
        if (def.operation > ModuleOperation::Draw ||
            (def.operation == ModuleOperation::Attributes && def.stage != EffectStage::Initialize && def.stage != EffectStage::Update) ||
            (def.operation == ModuleOperation::Emit && def.stage != EffectStage::Spawn) ||
            (def.operation == ModuleOperation::Draw && def.stage != EffectStage::Render) ||
            (def.stage == EffectStage::Render && def.writes != 0) ||
            (def.stage == EffectStage::Spawn && (def.reads | def.writes) != 0) ||
            (def.writes & Attribute(ParticleAttribute::Age)))
            error("catalog.operation", path, "Operation violates stage or engine-owned age contract");
        ce::engine_ordered_set<DefinitionId> ports;
        for (const auto& input : def.inputs)
            if (!IsEffectDefinitionId(input.id) || !ports.insert(input.id).second || TypeOf(input.defaultValue) != input.type || !InDomain(input.defaultValue, input.domain))
                error("catalog.input", path + "/" + input.id, "Invalid port ID, default or type");
    }
    ce::engine_ordered_map<DefinitionId, const EffectParameter*> parameters;
    for (const auto& parameter : e.parameters) {
        if (!IsEffectDefinitionId(parameter.id) || !parameters.emplace(parameter.id, &parameter).second || !Finite(parameter.defaultValue))
            error("parameter.invalid", "parameters/" + parameter.id, "Invalid/duplicate parameter ID or nonfinite default");
    }
    ce::engine_ordered_set<DefinitionId> emitters;
    if (e.emitters.empty()) error("effect.empty", "effect", "At least one emitter is required");
    for (const auto& emitter : e.emitters) {
        const auto path = "emitters/" + emitter.id;
        if (!IsEffectDefinitionId(emitter.id) || !emitters.insert(emitter.id).second) error("id.duplicate", path, "Invalid or duplicate emitter ID");
        if (!emitter.capacity || !std::isfinite(emitter.startSeconds) || emitter.startSeconds < 0 ||
            emitter.overflow != SpawnOverflow::DropNewest || emitter.space > SimulationSpace::World)
            error("emitter.contract", path, "Invalid capacity, start time, overflow policy or coordinate space");
        if (!IsEffectDefinitionId(emitter.material)) error("material.invalid", path, "A stable material asset ID is required; resolution happens later");
        ce::engine_ordered_map<EffectStage, const ModuleStack*> stacks;
        for (const auto& stack : emitter.stacks)
            if (!ValidStage(stack.stage) || !stacks.emplace(stack.stage, &stack).second) error("stack.stage", path, "Invalid or duplicate stage stack");
        AttributeMask initialized = Attribute(ParticleAttribute::Age);
        ce::engine_ordered_set<DefinitionId> instances;
        unsigned emitCount = 0, drawCount = 0;
        for (const auto stage : {EffectStage::Spawn, EffectStage::Initialize, EffectStage::Update, EffectStage::Render}) {
            const auto stack = stacks.find(stage);
            if (stack == stacks.end()) { error("stack.missing", path, "Missing " + std::string(StageName(stage)) + " stack"); continue; }
            for (const auto& instance : stack->second->modules) {
                const auto modulePath = path + "/" + StageName(stage) + "/" + instance.id;
                if (!IsEffectDefinitionId(instance.id) || !instances.insert(instance.id).second) error("id.duplicate", modulePath, "Invalid or duplicate instance ID");
                const auto found = modules.find(instance.definition);
                if (found == modules.end()) { error("module.unknown", modulePath, "Unknown module definition ID"); continue; }
                const auto& def = *found->second;
                if (def.version != instance.definitionVersion) error("module.version", modulePath, "Module version mismatch; explicit migration required");
                if (def.stage != stage) error("module.stage", modulePath, "Module is not allowed in this stage");
                ce::engine_ordered_map<DefinitionId, const InputBinding*> bindings;
                for (const auto& binding : instance.inputs) {
                    if (!bindings.emplace(binding.port, &binding).second) error("input.duplicate", modulePath + "/" + binding.port, "Duplicate port binding");
                    if (std::none_of(def.inputs.begin(), def.inputs.end(), [&](const auto& p) { return p.id == binding.port; }))
                        error("input.unknown", modulePath + "/" + binding.port, "Unknown input port");
                }
                for (const auto& port : def.inputs) {
                    const auto binding = bindings.find(port.id);
                    const auto inputErrors = ValidateEffectInput(binding == bindings.end() ? InputExpression{ConstantInput{port.defaultValue}} : binding->second->expression,
                        port, stage, e.parameters, modulePath + "/" + port.id);
                    errors.insert(errors.end(), inputErrors.begin(), inputErrors.end());
                }
                if (!instance.enabled) continue; // Disabled content remains structurally valid, but produces no attributes.
                AttributeMask reads = def.reads;
                for (const auto& binding : instance.inputs)
                    if (std::holds_alternative<CurveInput>(binding.expression) || std::holds_alternative<GradientInput>(binding.expression))
                        reads |= Attribute(ParticleAttribute::Age) | Attribute(ParticleAttribute::Lifetime);
                if (reads & ~initialized) error("attribute.uninitialized", modulePath, "Module reads an attribute before initialization");
                initialized |= def.writes;
                emitCount += def.operation == ModuleOperation::Emit;
                drawCount += def.operation == ModuleOperation::Draw;
            }
            if (stage == EffectStage::Initialize) {
                // Engine-owned fixed sequence: age += dt, update modules in stack
                // order, position += velocity*dt, then draw. Both integrator inputs
                // and normalized-life denominator must exist before any tick.
                const auto required = AllAttributes;
                if ((initialized & required) != required) error("attribute.required", path, "Initialize must produce Lifetime, Position, Velocity, Size and Color");
            }
        }
        if (!emitCount || drawCount != 1) error("emitter.operations", path, "At least one emitter module and exactly one renderer are required");
    }
    return errors;
}

std::uint64_t EffectRandomKey(std::uint64_t effectSeed, std::uint64_t emitterSeed,
    std::uint64_t particleId, std::string_view emitterId, std::string_view moduleId,
    std::string_view portId, RandomSampling sampling, std::uint64_t tick)
{
    std::uint64_t hash = 14695981039346656037ull;
    const auto byte = [&](std::uint8_t v) { hash = (hash ^ v) * 1099511628211ull; };
    const auto word = [&](std::uint64_t v) { for (unsigned n=0; n<8; ++n) byte(static_cast<std::uint8_t>(v >> (n*8))); };
    const auto text = [&](std::string_view s) { word(s.size()); for (const unsigned char c : s) byte(c); };
    word(EffectSchemaVersion); word(effectSeed); word(emitterSeed); word(particleId);
    text(emitterId); text(moduleId); text(portId); word(static_cast<std::uint8_t>(sampling));
    word(sampling == RandomSampling::EachTick ? tick : 0);
    return hash;
}

}
