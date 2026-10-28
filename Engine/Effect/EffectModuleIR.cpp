#include "../Utility_Framework/Foundation/EngineOrderedMap.h"
#include "../Utility_Framework/Foundation/EngineOrderedSet.h"
#include "../Containers/ContainerAPI.h"
#include "EffectModuleIR.h"

namespace ce::effects
{
namespace
{
std::optional<InputType> AttributeType(ParticleAttribute a)
{
    switch (a) {
    case ParticleAttribute::Age: case ParticleAttribute::Lifetime: case ParticleAttribute::Size: return InputType::Float;
    case ParticleAttribute::Position: case ParticleAttribute::Velocity: return InputType::Float3;
    case ParticleAttribute::Color: return InputType::Color;
    }
    return {};
}
bool Finite(const EffectValue& value)
{
    return std::visit([](const auto& v) {
        using T = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<T, Float3>) return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
        else if constexpr (std::is_same_v<T, Color4>) return std::isfinite(v.r) && std::isfinite(v.g) && std::isfinite(v.b) && std::isfinite(v.a);
        else return std::isfinite(static_cast<double>(v));
    }, value);
}
EffectValue Read(const ParticleValues& p, ParticleAttribute a)
{
    switch (a) {
    case ParticleAttribute::Age: return p.age;
    case ParticleAttribute::Lifetime: return p.lifetime;
    case ParticleAttribute::Position: return p.position;
    case ParticleAttribute::Velocity: return p.velocity;
    case ParticleAttribute::Size: return p.size;
    case ParticleAttribute::Color: return p.color;
    }
    return 0.f; // Only compiler-validated attributes reach the evaluator.
}
void Store(ParticleValues& p, ParticleAttribute a, const EffectValue& value)
{
    switch (a) {
    case ParticleAttribute::Lifetime: p.lifetime = ce::get<float>(value); break;
    case ParticleAttribute::Position: p.position = ce::get<Float3>(value); break;
    case ParticleAttribute::Velocity: p.velocity = ce::get<Float3>(value); break;
    case ParticleAttribute::Size: p.size = ce::get<float>(value); break;
    case ParticleAttribute::Color: p.color = ce::get<Color4>(value); break;
    default: break;
    }
}
EffectValue Add(const EffectValue& a, const EffectValue& b)
{
    if (const auto* f = std::get_if<float>(&a)) return *f + ce::get<float>(b);
    if (const auto* v = std::get_if<Float3>(&a)) {
        const auto w = ce::get<Float3>(b); return Float3{v->x+w.x,v->y+w.y,v->z+w.z};
    }
    const auto v = ce::get<Color4>(a), w = ce::get<Color4>(b);
    return Color4{v.r+w.r,v.g+w.g,v.b+w.b,v.a+w.a};
}
EffectValue Scale(const EffectValue& a, float b)
{
    if (const auto* f = std::get_if<float>(&a)) return *f*b;
    if (const auto* v = std::get_if<Float3>(&a)) return Float3{v->x*b,v->y*b,v->z*b};
    const auto v = ce::get<Color4>(a); return Color4{v.r*b,v.g*b,v.b*b,v.a*b};
}
}

ModuleCompileResult CompileEffectModule(const ModuleDefinition& definition, const ModuleInstance& instance,
    const ModuleGraph& graph, const ce::dynamic_array<EffectParameter>& parameters)
{
    ModuleCompileResult result;
    const auto path = "modules/" + instance.id;
    const auto error = [&](std::string code, const std::string& suffix, std::string message) {
        result.diagnostics.push_back({std::move(code), path + suffix, std::move(message)});
    };
    CompiledEffectModule program;
    program.instance_ = instance.id; program.enabled_ = instance.enabled;
    program.stage_ = definition.stage;
    program.reads_ = definition.reads; program.writes_ = definition.writes;
    program.ports_ = definition.inputs; program.parameters_ = parameters;
    if (!IsEffectDefinitionId(instance.id) || !IsEffectDefinitionId(definition.id) || instance.definition != definition.id)
        error("module.id", "", "Invalid or mismatched stable module ID");
    if (!definition.version || instance.definitionVersion != definition.version)
        error("module.version", "", "Module version mismatch");
    if (definition.operation != ModuleOperation::Attributes ||
        (definition.stage != EffectStage::Initialize && definition.stage != EffectStage::Update) ||
        ((definition.reads | definition.writes) & ~AttributeMask{63}) ||
        (definition.writes & Attribute(ParticleAttribute::Age)))
        error("module.contract", "", "Attribute graph requires Initialize/Update and cannot write engine-owned age");
    ce::engine_ordered_map<DefinitionId, const ModuleInputDefinition*> ports;
    ce::engine_ordered_set<DefinitionId> parameterIds;
    for (const auto& parameter : parameters)
        if (!IsEffectDefinitionId(parameter.id) || !parameterIds.insert(parameter.id).second || !Finite(parameter.defaultValue))
            error("parameter.invalid", "/parameters/" + parameter.id, "Invalid parameter ID or default");
    for (const auto& port : definition.inputs) {
        if (!IsEffectDefinitionId(port.id) || !ports.emplace(port.id, &port).second)
            error("input.id", "/" + port.id, "Invalid or duplicate input port");
        auto errors = ValidateEffectInput(ConstantInput{port.defaultValue}, port, definition.stage, parameters, path + "/" + port.id);
        result.diagnostics.insert(result.diagnostics.end(), errors.begin(), errors.end());
    }
    ce::engine_ordered_map<DefinitionId, InputExpression> bindings;
    for (const auto& binding : instance.inputs) {
        if (!ports.contains(binding.port)) error("input.unknown", "/" + binding.port, "Unknown port");
        if (!bindings.emplace(binding.port, binding.expression).second) error("input.duplicate", "/" + binding.port, "Duplicate binding");
    }
    for (const auto& port : definition.inputs) {
        const auto found = bindings.find(port.id);
        const auto expression = found == bindings.end() ? InputExpression{ConstantInput{port.defaultValue}} : found->second;
        const auto errors = ValidateEffectInput(expression, port, definition.stage, parameters, path + "/" + port.id);
        result.diagnostics.insert(result.diagnostics.end(), errors.begin(), errors.end());
        if (std::holds_alternative<CurveInput>(expression) || std::holds_alternative<GradientInput>(expression))
            program.reads_ |= Attribute(ParticleAttribute::Age) | Attribute(ParticleAttribute::Lifetime);
        program.inputs_.push_back({port.id, expression});
    }
    ce::engine_ordered_map<DefinitionId, std::size_t> nodes;
    for (std::size_t i=0; i<graph.nodes.size(); ++i)
        if (!IsEffectDefinitionId(graph.nodes[i].id) || !nodes.emplace(graph.nodes[i].id, i).second)
            error("graph.id", "/nodes/" + graph.nodes[i].id, "Invalid or duplicate node ID");
    if (!result.diagnostics.empty()) return result;
    // Iterative Kahn traversal handles even disconnected cycles without recursion.
    ce::dynamic_array<std::size_t> degrees(graph.nodes.size());
    ce::dynamic_array<ce::dynamic_array<std::size_t>> dependents(graph.nodes.size());
    for (std::size_t i=0; i<graph.nodes.size(); ++i) {
        for (const auto& argument : graph.nodes[i].arguments) {
            const auto found = nodes.find(argument);
            if (found == nodes.end()) error("graph.edge", "/nodes/" + graph.nodes[i].id, "Missing argument node: " + argument);
            else { ++degrees[i]; dependents[found->second].push_back(i); }
        }
    }
    if (!result.diagnostics.empty()) return result;
    ce::engine_ordered_set<std::size_t> ready;
    for (std::size_t i=0; i<degrees.size(); ++i) if (!degrees[i]) ready.insert(i);
    ce::dynamic_array<std::size_t> registers(graph.nodes.size());
    while (!ready.empty()) {
        const auto index = *ready.begin(); ready.erase(ready.begin());
        const auto& node = graph.nodes[index];
        ModuleInstruction instruction{node.id,node.opcode,InputType::Float,{},node.literal,node.port,node.attribute};
        for (const auto& arg : node.arguments) instruction.arguments.push_back(registers[nodes.at(arg)]);
        const auto fail = [&](std::string message) { error("graph.type", "/nodes/" + node.id, std::move(message)); };
        const auto arity = node.opcode == ModuleOpcode::Add || node.opcode == ModuleOpcode::Multiply ? 2u :
            node.opcode == ModuleOpcode::ExpNeg ? 1u : 0u;
        if (node.arguments.size() != arity) fail("Wrong argument count");
        else switch (node.opcode) {
        case ModuleOpcode::Constant:
            instruction.type = TypeOf(node.literal); if (!Finite(node.literal)) fail("Nonfinite literal"); break;
        case ModuleOpcode::Input:
            if (!ports.contains(node.port)) fail("Unknown input port");
            else instruction.type = ports.at(node.port)->type;
            break;
        case ModuleOpcode::ReadAttribute: {
            const auto type = AttributeType(node.attribute);
            if (!type || !(definition.reads & Attribute(node.attribute))) fail("Undeclared or invalid attribute read");
            else instruction.type = *type;
            break;
        }
        case ModuleOpcode::DeltaSeconds:
            if (definition.stage != EffectStage::Update) fail("DeltaSeconds is available only in Update"); break;
        case ModuleOpcode::ExpNeg:
            if (program.instructions_[instruction.arguments[0]].type != InputType::Float) fail("ExpNeg requires Float"); break;
        case ModuleOpcode::Add: case ModuleOpcode::Multiply: {
            const auto a = program.instructions_[instruction.arguments[0]].type;
            const auto b = program.instructions_[instruction.arguments[1]].type;
            instruction.type = a;
            if (a < InputType::Float || (node.opcode == ModuleOpcode::Add ? a != b : b != InputType::Float))
                fail("Add requires equal Float/Float3/Color; Multiply requires Float right operand");
            break;
        }
        default: fail("Unsupported opcode"); break;
        }
        if (!result.diagnostics.empty()) return result;
        registers[index] = program.instructions_.size();
        program.instructions_.push_back(std::move(instruction));
        for (const auto next : dependents[index]) if (--degrees[next] == 0) ready.insert(next);
    }
    if (program.instructions_.size() != graph.nodes.size()) {
        for (std::size_t i=0; i<degrees.size(); ++i) if (degrees[i])
            error("graph.cycle", "/nodes/" + graph.nodes[i].id, "Cycle or dependency on cyclic nodes");
    }
    AttributeMask outputs{};
    for (const auto& output : graph.outputs) {
        const auto type = AttributeType(output.attribute);
        const auto found = nodes.find(output.node);
        if (!type || output.attribute == ParticleAttribute::Age || !(definition.writes & Attribute(output.attribute)) ||
            (outputs & Attribute(output.attribute))) error("graph.output", "/outputs/" + output.node, "Invalid, duplicate or undeclared attribute output");
        else if (found == nodes.end()) error("graph.output", "/outputs/" + output.node, "Missing output node");
        else if (result.diagnostics.empty()) {
            const auto reg = registers[found->second];
            if (program.instructions_[reg].type != *type) error("graph.output", "/outputs/" + output.node, "Output type differs from attribute");
            else program.outputs_.push_back({output.attribute,reg});
        }
        outputs |= Attribute(output.attribute);
    }
    if (outputs != definition.writes) error("graph.output", "/outputs", "Every declared write requires exactly one output");
    if (result.diagnostics.empty()) result.module = std::move(program);
    return result;
}

ModuleCompileResult CompileBuiltinEffectModule(const ModuleInstance& instance, const ce::dynamic_array<EffectParameter>& parameters)
{
    const auto catalog = BuiltinEffectModules();
    const auto found = std::find_if(catalog.begin(),catalog.end(),[&](const auto& d) { return d.id == instance.definition; });
    if (found == catalog.end()) return {{},{{"module.unknown","modules/"+instance.id,"Unknown builtin module"}}};
    ModuleGraph graph;
    const auto add = [&](DefinitionId id, ModuleOpcode op, ce::dynamic_array<DefinitionId> args = {}, DefinitionId port = "") {
        graph.nodes.push_back({std::move(id),op,std::move(args),0.f,std::move(port),ParticleAttribute::Velocity});
    };
    if (instance.definition == "ce.gravity" || instance.definition == "ce.drag") {
        add("velocity",ModuleOpcode::ReadAttribute);
        add("input",ModuleOpcode::Input,{},found->inputs.front().id);
        add("dt",ModuleOpcode::DeltaSeconds);
        add("scaled",ModuleOpcode::Multiply,{"input","dt"});
        if (instance.definition == "ce.gravity") add("result",ModuleOpcode::Add,{"velocity","scaled"});
        else { add("decay",ModuleOpcode::ExpNeg,{"scaled"}); add("result",ModuleOpcode::Multiply,{"velocity","decay"}); }
        graph.outputs.push_back({ParticleAttribute::Velocity,"result"});
    } else if (instance.definition == "ce.lifetime" || instance.definition == "ce.size" || instance.definition == "ce.color" ||
        instance.definition == "ce.size.life" || instance.definition == "ce.color.life") {
        add("input",ModuleOpcode::Input,{},found->inputs.front().id);
        graph.outputs.push_back({static_cast<ParticleAttribute>(found->writes),"input"});
    } else return {{},{{"ir.builtin.unsupported","modules/"+instance.id,"Emission, shape sampling and rendering lowering belong to subsequent slices"}}};
    return CompileEffectModule(*found,instance,graph,parameters);
}

ce::dynamic_array<EffectDiagnostic> EvaluateEffectModules(const ce::dynamic_array<CompiledEffectModule>& modules,
    ParticleValues& particle, const ModuleEvaluationContext& context)
{
    const auto error = [](std::string code, std::string path, std::string message) {
        return ce::dynamic_array<EffectDiagnostic>{{std::move(code),std::move(path),std::move(message)}};
    };
    if (!std::isfinite(context.deltaSeconds) || context.deltaSeconds < 0)
        return error("eval.clock","context/deltaSeconds","Delta must be finite and nonnegative");
    ce::engine_ordered_set<DefinitionId> overrideIds;
    for (const auto& parameter : context.parameters) {
        if (!IsEffectDefinitionId(parameter.id) || !overrideIds.insert(parameter.id).second || !Finite(parameter.defaultValue))
            return error("eval.parameter","parameters/"+parameter.id,"Invalid or duplicate runtime parameter");
        bool known = false;
        for (const auto& module : modules) for (const auto& declared : module.parameters_)
            if (declared.id == parameter.id) {
                known = true;
                if (TypeOf(declared.defaultValue) != TypeOf(parameter.defaultValue))
                    return error("eval.parameter","parameters/"+parameter.id,"Runtime parameter type mismatch");
            }
        if (!known) return error("eval.parameter","parameters/"+parameter.id,"Unknown runtime parameter");
    }
    auto candidate = particle;
    for (const auto& module : modules) {
        const auto path = "modules/" + module.instance_;
        if (!module.enabled_) continue;
        if (module.reads_ & ~candidate.initialized) return error("eval.uninitialized",path,"Attribute read before initialization");
        ce::engine_ordered_map<DefinitionId, EffectValue> inputs;
        for (std::size_t i=0; i<module.inputs_.size(); ++i) {
            const auto& binding = module.inputs_[i];
            EffectValue value;
            if (const auto* constant = std::get_if<ConstantInput>(&binding.expression)) value = constant->value;
            else if (const auto* parameter = std::get_if<ParameterInput>(&binding.expression)) {
                const auto& id = parameter->parameter;
                const auto found = std::find_if(context.parameters.begin(),context.parameters.end(),[&](const auto& p) { return p.id == id; });
                value = found != context.parameters.end() ? found->defaultValue :
                    std::find_if(module.parameters_.begin(),module.parameters_.end(),[&](const auto& p) { return p.id == id; })->defaultValue;
            } else if (const auto* random = std::get_if<RandomInput>(&binding.expression)) {
                if (!IsEffectDefinitionId(context.emitterId))
                    return error("eval.identity",path+"/"+binding.port,"Random requires a stable emitter ID");
                value = SampleEffectRandom(*random,EffectRandomKey(context.effectSeed,context.emitterSeed,
                    context.particleId,context.emitterId,module.instance_,binding.port,random->sampling,context.simulationTick));
            } else {
                if (!std::isfinite(candidate.age) || candidate.age < 0 || !std::isfinite(candidate.lifetime) || candidate.lifetime <= 0)
                    return error("eval.life",path+"/"+binding.port,"Normalized life requires finite nonnegative age and positive lifetime");
                const auto t = std::clamp(static_cast<double>(candidate.age)/candidate.lifetime,0.,1.);
                std::visit([&](const auto& input) {
                    using T = std::decay_t<decltype(input)>;
                    if constexpr (std::is_same_v<T,CurveInput> || std::is_same_v<T,GradientInput>) {
                        auto upper = std::upper_bound(input.keys.begin(),input.keys.end(),t,
                            [](double time,const auto& key){ return time < key.time; });
                        if (upper == input.keys.begin()) value = input.keys.front().value;
                        else if (upper == input.keys.end()) value = input.keys.back().value;
                        else {
                            const auto& lo = *(upper-1);
                            const auto fraction = (t-lo.time)/(static_cast<double>(upper->time)-lo.time);
                            const auto lerp = [fraction](float a,float b) {
                                return static_cast<float>(static_cast<double>(a)*(1.-fraction)+static_cast<double>(b)*fraction);
                            };
                            if constexpr (std::is_same_v<T,CurveInput>) value = lerp(lo.value,upper->value);
                            else value = Color4{lerp(lo.value.r,upper->value.r),lerp(lo.value.g,upper->value.g),
                                lerp(lo.value.b,upper->value.b),lerp(lo.value.a,upper->value.a)};
                        }
                    }
                },binding.expression);
            }
            const auto errors = ValidateEffectInput(ConstantInput{value},module.ports_[i],EffectStage::Update,{},path+"/"+binding.port);
            if (!errors.empty()) return errors;
            inputs.emplace(binding.port,std::move(value));
        }
        ce::dynamic_array<EffectValue> registers;
        for (const auto& instruction : module.instructions_) {
            EffectValue value;
            switch (instruction.opcode) {
            case ModuleOpcode::Constant: value = instruction.literal; break;
            case ModuleOpcode::Input: value = inputs.at(instruction.port); break;
            case ModuleOpcode::ReadAttribute: value = Read(candidate,instruction.attribute); break;
            case ModuleOpcode::DeltaSeconds: value = context.deltaSeconds; break;
            case ModuleOpcode::Add: value = Add(registers[instruction.arguments[0]],registers[instruction.arguments[1]]); break;
            case ModuleOpcode::Multiply: value = Scale(registers[instruction.arguments[0]],ce::get<float>(registers[instruction.arguments[1]])); break;
            case ModuleOpcode::ExpNeg: value = std::exp(-ce::get<float>(registers[instruction.arguments[0]])); break;
            }
            if (!Finite(value)) return error("eval.nonfinite",path+"/nodes/"+instruction.node,"Nonfinite attribute or arithmetic result");
            registers.push_back(std::move(value));
        }
        for (const auto& [attribute,reg] : module.outputs_) {
            const auto& value = registers[reg];
            if ((attribute == ParticleAttribute::Lifetime && ce::get<float>(value) <= 0) ||
                (attribute == ParticleAttribute::Size && ce::get<float>(value) < 0))
                return error("eval.domain",path,"Lifetime must be positive and Size nonnegative");
            Store(candidate,attribute,value);
        }
        candidate.initialized |= module.writes_;
    }
    particle = candidate;
    return {};
}

EffectValue SampleEffectRandom(const RandomInput& input, std::uint64_t key)
{
    const ModuleInputDefinition port{"sample",TypeOf(input.minimum),input.minimum};
    if (!ValidateEffectInput(input,port,EffectStage::Update,{},"sample").empty())
        throw std::invalid_argument("SampleEffectRandom requires a validated numeric range");
    std::uint64_t counter{};
    const auto next = [&]() {
        auto x = key + 0x9e3779b97f4a7c15ull * ++counter;
        x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ull;
        x = (x ^ (x >> 27)) * 0x94d049bb133111ebull;
        return x ^ (x >> 31);
    };
    const auto real = [&](float lo,float hi) {
        const auto u = static_cast<double>(next() >> 40) / 16777216.;
        return static_cast<float>(static_cast<double>(lo)*(1.-u)+static_cast<double>(hi)*u);
    };
    if (const auto* lo = std::get_if<std::uint32_t>(&input.minimum)) {
        const auto span = std::uint64_t{ce::get<std::uint32_t>(input.maximum)} - *lo + 1;
        // Rejection removes modulo bias, including a full [0, UINT32_MAX] range.
        const auto threshold = (std::uint64_t{0}-span)%span;
        auto bits = next(); while (bits < threshold) bits = next();
        return static_cast<std::uint32_t>(*lo + bits%span);
    }
    if (const auto* lo = std::get_if<float>(&input.minimum)) return real(*lo,ce::get<float>(input.maximum));
    const auto lo = ce::get<Float3>(input.minimum), hi = ce::get<Float3>(input.maximum);
    return Float3{real(lo.x,hi.x),real(lo.y,hi.y),real(lo.z,hi.z)};
}

ce::dynamic_array<EffectDiagnostic> StepEffectParticle(const ce::dynamic_array<CompiledEffectModule>& update,
    ParticleValues& particle, bool& alive, const ModuleEvaluationContext& context)
{
    const auto fail = [](std::string message) { return ce::dynamic_array<EffectDiagnostic>{{"step.invalid","particle",std::move(message)}}; };
    if (!std::isfinite(context.deltaSeconds) || context.deltaSeconds < 0) return fail("Invalid fixed dt");
    for (const auto& module : update) if (module.Stage() != EffectStage::Update) return fail("Step accepts Update modules only");
    if (!alive) return {};
    if ((particle.initialized & 63) != 63 || !Finite(particle.age) || particle.age < 0 ||
        !Finite(particle.lifetime) || particle.lifetime <= 0 || !Finite(particle.position) ||
        !Finite(particle.velocity) || !Finite(particle.size) || particle.size < 0 || !Finite(particle.color))
        return fail("Step requires all finite initialized attributes and valid age/lifetime/size");
    if (context.deltaSeconds == 0) return {}; // Paused particles do not execute modules.
    auto candidate = particle;
    candidate.age += context.deltaSeconds;
    if (!std::isfinite(candidate.age)) return fail("Age overflow");
    if (candidate.age >= candidate.lifetime) { particle = candidate; alive = false; return {}; }
    const auto errors = EvaluateEffectModules(update,candidate,context);
    if (!errors.empty()) return errors;
    candidate.position = ce::get<Float3>(Add(candidate.position,Scale(candidate.velocity,context.deltaSeconds)));
    if (!Finite(candidate.position)) return fail("Position integration overflow");
    particle = candidate;
    return {};
}
}
