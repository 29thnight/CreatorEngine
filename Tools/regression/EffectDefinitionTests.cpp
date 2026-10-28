#include "fixtures/EffectExplosionDefinition.h"
#include <stdio.h>
#include <stdexcept>

using namespace ce::effects;
using ce::effects::fixtures::MakeExplosionDefinition;
namespace
{
unsigned checks{};
void Require(bool value, const char* message)
{
    ++checks;
    if (!value) throw std::runtime_error(message);
}
bool Has(const ce::dynamic_array<EffectDiagnostic>& errors, std::string_view code, std::string_view location = {})
{
    return std::any_of(errors.begin(), errors.end(), [&](const auto& e) {
        return e.code == code && (location.empty() || e.path.find(location) != std::string::npos);
    });
}
void Reject(const std::function<void(EffectDefinition&)>& mutate, const char* code, const char* path = "")
{
    auto effect = MakeExplosionDefinition(); mutate(effect);
    Require(Has(ValidateEffectDefinition(effect, BuiltinEffectModules()), code, path), code);
}
}
int main()
{
    try {
        auto effect = MakeExplosionDefinition();
        const auto catalog = BuiltinEffectModules();
        Require(ValidateEffectDefinition(effect, catalog).empty(), "explosion definition");
        auto second = effect.emitters.front(); second.id = "smoke"; second.name = "Smoke"; second.startSeconds = .1;
        effect.emitters.push_back(second);
        Require(ValidateEffectDefinition(effect, catalog).empty(), "multiple emitters with local instance IDs");
        effect.name = "Renamed"; effect.emitters.front().name = "Renamed sparks";
        std::swap(effect.emitters.front().stacks[1].modules[1], effect.emitters.front().stacks[1].modules[2]);
        std::swap(effect.emitters.front().stacks[2].modules[0], effect.emitters.front().stacks[2].modules[1]);
        Require(ValidateEffectDefinition(effect, catalog).empty(), "valid reorder and rename preserve identity");
        Require(effect.emitters.front().stacks[2].modules[0].id == "drag", "stack order stored, not sorted by ID");
        Reject([](auto& e) { e.schemaVersion = 2; }, "schema.version");
        Reject([](auto& e) { e.id.clear(); }, "id.invalid");
        Reject([](auto& e) { e.clock.fixedStepSeconds = 0; }, "clock.invalid");
        Reject([](auto& e) { e.clock.timeScale = -1; }, "clock.invalid");
        Reject([](auto& e) { e.clock.maxSubsteps = 0; }, "clock.invalid");
        Reject([](auto& e) { e.clock.fixedStepSeconds = std::numeric_limits<double>::infinity(); }, "clock.invalid");
        Reject([](auto& e) { e.emitters.clear(); }, "effect.empty");
        Reject([](auto& e) { e.emitters.push_back(e.emitters.front()); }, "id.duplicate");
        Reject([](auto& e) { e.emitters[0].capacity = 0; }, "emitter.contract");
        Reject([](auto& e) { e.emitters[0].startSeconds = -1; }, "emitter.contract");
        Reject([](auto& e) { e.emitters[0].material.clear(); }, "material.invalid");
        Reject([](auto& e) { e.parameters.push_back(e.parameters.front()); }, "parameter.invalid");
        Reject([](auto& e) { e.emitters[0].stacks.pop_back(); }, "stack.missing");
        Reject([](auto& e) { e.emitters[0].stacks.push_back(e.emitters[0].stacks[0]); }, "stack.stage");
        Reject([](auto& e) { e.emitters[0].stacks[2].modules[0].definition = "missing"; }, "module.unknown", "gravity");
        Reject([](auto& e) { e.emitters[0].stacks[2].modules[0].definitionVersion = 2; }, "module.version");
        Reject([](auto& e) { e.emitters[0].stacks[1].modules.push_back(e.emitters[0].stacks[2].modules[0]); }, "module.stage", "initialize/gravity");
        Reject([](auto& e) { e.emitters[0].stacks[2].modules[1].id = "gravity"; }, "id.duplicate");
        Reject([](auto& e) { e.emitters[0].stacks[1].modules[0].enabled = false; }, "attribute.required");
        Reject([](auto& e) { e.emitters[0].stacks[1].modules[2].enabled = false; }, "attribute.uninitialized", "gravity");
        Reject([](auto& e) { e.emitters[0].stacks[0].modules[0].enabled = false; }, "emitter.operations");
        Reject([](auto& e) { e.emitters[0].stacks[3].modules.push_back({"billboard2","ce.billboard"}); }, "emitter.operations");
        Reject([](auto& e) { e.emitters[0].stacks[0].modules[0].inputs = {{"count",ConstantInput{80.f}}}; }, "input.type", "burst/count");
        Reject([](auto& e) { e.emitters[0].stacks[0].modules[0].inputs = {{"typo",ConstantInput{80.f}}}; }, "input.unknown");
        Reject([](auto& e) { e.emitters[0].stacks[1].modules[0].inputs.push_back(e.emitters[0].stacks[1].modules[0].inputs[0]); }, "input.duplicate");
        Reject([](auto& e) { e.emitters[0].stacks[1].modules[0].inputs[0].expression = RandomInput{2.f,1.f}; }, "input.range");
        Reject([](auto& e) { e.emitters[0].stacks[1].modules[0].inputs[0].expression = ConstantInput{0.f}; }, "input.value");
        Reject([](auto& e) { e.emitters[0].stacks[1].modules[0].inputs[0].expression = RandomInput{.1f,1.f,RandomSampling::EachTick}; }, "input.sampling");
        Reject([](auto& e) { e.emitters[0].stacks[1].modules[4].inputs[0].expression = ParameterInput{"missing"}; }, "input.parameter");
        Reject([](auto& e) { e.parameters[0].defaultValue = 1.f; }, "input.type", "color/color");
        Reject([](auto& e) { e.emitters[0].stacks[2].modules[1].inputs = {{"coefficient",ConstantInput{-.1f}}}; }, "input.value");
        Reject([](auto& e) { e.emitters[0].stacks[2].modules[1].inputs = {{"coefficient",ConstantInput{std::numeric_limits<float>::quiet_NaN()}}}; }, "input.nonfinite");
        Reject([](auto& e) { e.emitters[0].stacks[2].modules[3].inputs[0].expression = CurveInput{}; }, "input.keys");
        Reject([](auto& e) { e.emitters[0].stacks[2].modules[3].inputs[0].expression = CurveInput{{{0,1},{0,.5f},{1,0}}}; }, "input.keys");
        Reject([](auto& e) { e.emitters[0].stacks[2].modules[3].inputs[0].expression = CurveInput{{{.1f,1},{1,0}}}; }, "input.keys");
        Reject([](auto& e) { e.emitters[0].stacks[1].modules[0].inputs[0].expression = CurveInput{{{0,1},{1,2}}}; }, "input.domain");
        Reject([](auto& e) { e.emitters[0].stacks[2].modules[2].inputs[0].expression = CurveInput{{{0,1},{1,0}}}; }, "input.type");
        {
            const auto nan = std::numeric_limits<float>::quiet_NaN();
            const ModuleInputDefinition vectorPort{"v", InputType::Float3, Float3{}, InputDomain::Finite};
            Require(Has(ValidateEffectInput(ConstantInput{Float3{1.f,nan,3.f}}, vectorPort, EffectStage::Update, {}, "v"), "input.nonfinite"), "float3 middle nonfinite");
            const ModuleInputDefinition colorPort{"c", InputType::Color, Color4{}, InputDomain::Finite};
            Require(Has(ValidateEffectInput(ConstantInput{Color4{1.f,1.f,1.f,nan}}, colorPort, EffectStage::Update, {}, "c"), "input.nonfinite"), "color alpha nonfinite");
            Require(Has(ValidateEffectInput(RandomInput{Float3{0.f,2.f,0.f},Float3{1.f,1.f,1.f}}, vectorPort, EffectStage::Update, {}, "v"), "input.range"), "float3 middle order");
        }
        auto custom = catalog;
        custom.push_back({"custom.read.position",1,EffectStage::Initialize,{},Attribute(ParticleAttribute::Position),Attribute(ParticleAttribute::Size)});
        auto ordered = MakeExplosionDefinition();
        ordered.emitters[0].stacks[1].modules.insert(ordered.emitters[0].stacks[1].modules.begin(), {"custom","custom.read.position"});
        Require(Has(ValidateEffectDefinition(ordered,custom),"attribute.uninitialized","custom"), "initialize order rejects read-before-write");
        std::swap(ordered.emitters[0].stacks[1].modules[0],ordered.emitters[0].stacks[1].modules[2]);
        Require(ValidateEffectDefinition(ordered,custom).empty(), "ordered multi-writer is valid after initialization");
        custom.push_back(custom.front());
        Require(Has(ValidateEffectDefinition(ordered,custom),"catalog.id"), "duplicate module definition");
        custom = catalog; custom[0].writes = Attribute(ParticleAttribute::Age);
        Require(Has(ValidateEffectDefinition(MakeExplosionDefinition(),custom),"catalog.operation"), "engine-owned age cannot be written");
        custom = catalog; custom[1].inputs[0].defaultValue = -1.f;
        Require(Has(ValidateEffectDefinition(MakeExplosionDefinition(),custom),"catalog.input"), "catalog default domain");
        const auto key = [](RandomSampling mode, std::uint64_t tick, std::string_view module="life") {
            return EffectRandomKey(42,7,11,"sparks",module,"seconds",mode,tick);
        };
        Require(key(RandomSampling::AtSpawn,1) == 1302758027843302164ull, "v1 stable random key golden value");
        Require(key(RandomSampling::AtSpawn,1) == key(RandomSampling::AtSpawn,999), "spawn random is tick-independent");
        Require(key(RandomSampling::EachTick,1) != key(RandomSampling::EachTick,2), "update random varies with simulation tick");
        Require(key(RandomSampling::AtSpawn,1) != key(RandomSampling::AtSpawn,1,"other"), "module ID partitions random");
        Require(EffectRandomKey(42,7,11,"a","bc","d",RandomSampling::AtSpawn,0) !=
            EffectRandomKey(42,7,11,"ab","c","d",RandomSampling::AtSpawn,0), "length-delimited ID hashing");
        std::printf("EFFECT_DEFINITION_OK checks=%u modules=%zu schema=%u randomKey=%llu\n",checks,catalog.size(),EffectSchemaVersion,
            static_cast<unsigned long long>(key(RandomSampling::AtSpawn,1)));
        return 0;
    } catch (const std::exception& error) { std::fprintf(stderr,"EFFECT_DEFINITION_FAIL %s\n",error.what()); return 1; }
}
