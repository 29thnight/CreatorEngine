#include "../../Engine/Effect/EffectModuleIR.h"
#include <iostream>
#include <limits>

using namespace ce::effects;
namespace
{
int checks{};
void Check(bool ok, const char* name) { ++checks; if (!ok) { std::cerr << name << '\n'; std::exit(1); } }
bool Near(float a, float b) { return std::abs(a-b) <= 1.e-5f * std::max(1.f,std::abs(b)); }
ModuleDefinition Gravity()
{
    const auto catalog = BuiltinEffectModules();
    return *std::find_if(catalog.begin(),catalog.end(),[](const auto& m) { return m.id == "ce.gravity"; });
}
ModuleGraph GravityGraph()
{
    // Deliberately reverse dependency order: source layout does not imply execution order.
    return {{{"sum",ModuleOpcode::Add,{"v","scaled"}},
        {"scaled",ModuleOpcode::Multiply,{"a","dt"}},
        {"dt",ModuleOpcode::DeltaSeconds},
        {"a",ModuleOpcode::Input,{},0.f,"acceleration"},
        {"v",ModuleOpcode::ReadAttribute,{},0.f,"",ParticleAttribute::Velocity}},
        {{ParticleAttribute::Velocity,"sum"}}};
}
void Reject(ModuleGraph graph, const char* code, ModuleDefinition definition = Gravity(),
    ModuleInstance instance = {"gravity","ce.gravity"})
{
    const auto result = CompileEffectModule(definition,instance,graph);
    Check(!result.module && !result.diagnostics.empty(),"Invalid graph compiled");
    Check(std::any_of(result.diagnostics.begin(),result.diagnostics.end(),[&](const auto& d) {
        return d.code == code && d.path.starts_with("modules/gravity"); }),"Expected located diagnostic");
}
}
int main()
{
    const auto builtin = CompileBuiltinEffectModule({"gravity","ce.gravity"});
    auto userDefinition = Gravity(); userDefinition.id = "user.gravity";
    const auto custom = CompileEffectModule(userDefinition,{"userGravity","user.gravity"},GravityGraph());
    Check(builtin.module.has_value() && custom.module.has_value(),"Gravity compile");
    const auto& a = builtin.module->Instructions(); const auto& b = custom.module->Instructions();
    Check(a.size() == 5 && b.size() == 5,"Gravity SSA instruction count");
    for (const auto op : {ModuleOpcode::ReadAttribute,ModuleOpcode::Input,ModuleOpcode::DeltaSeconds,ModuleOpcode::Multiply,ModuleOpcode::Add}) {
        const auto x = std::find_if(a.begin(),a.end(),[&](const auto& i){ return i.opcode == op; });
        const auto y = std::find_if(b.begin(),b.end(),[&](const auto& i){ return i.opcode == op; });
        Check(x != a.end() && y != b.end() && x->type == y->type,"Builtin and graph share typed operations");
    }
    for (const auto dt : {0.f,.016f,.125f,1.f}) for (const auto speed : {-10.f,0.f,3.f}) {
        ParticleValues x; x.velocity={2,speed,4}; x.initialized=63;
        auto y=x;
        Check(EvaluateEffectModules({*builtin.module},x,{dt}).empty(),"Builtin evaluation");
        Check(EvaluateEffectModules({*custom.module},y,{dt}).empty(),"Graph evaluation");
        Check(Near(x.velocity.y,speed-9.8f*dt) && Near(x.velocity.y,y.velocity.y) && x.velocity.x==2 && x.velocity.z==4,"Gravity analytic comparison");
        Check(x.age==0 && x.position.x==0,"Module does not duplicate engine clock or integration");
    }
    const auto drag=CompileBuiltinEffectModule({"drag","ce.drag"});
    Check(drag.module.has_value(),"Drag compile");
    ParticleValues p; p.velocity={0,10,0}; p.initialized=63;
    auto reversed=p;
    Check(EvaluateEffectModules({*builtin.module,*drag.module},p,{.5f}).empty(),"Ordered stack");
    Check(EvaluateEffectModules({*drag.module,*builtin.module},reversed,{.5f}).empty(),"Reversed stack");
    Check(Near(p.velocity.y,(10-4.9f)*std::exp(-.4f)) && Near(reversed.velocity.y,10*std::exp(-.4f)-4.9f),"Stack order analytic oracle");
    Check(!Near(p.velocity.y,reversed.velocity.y),"Stack order matters");
    const auto parameterized=CompileEffectModule(Gravity(),{"gravity","ce.gravity",1,true,{{"acceleration",ParameterInput{"wind"}}}},
        GravityGraph(),{{"wind","Wind",Float3{1,2,3}}});
    Check(parameterized.module.has_value(),"Parameter compile");
    p={}; p.initialized=63;
    Check(EvaluateEffectModules({*parameterized.module},p,{1}).empty() && p.velocity.y==2,"Parameter default");
    Check(EvaluateEffectModules({*parameterized.module},p,{1,{{"wind","",Float3{0,5,0}}}}).empty() && p.velocity.y==7,"Typed runtime override");
    const auto before=p;
    Check(!EvaluateEffectModules({*parameterized.module},p,{1,{{"wind","",1.f}}}).empty() && p.velocity.y==before.velocity.y,"Parameter type rollback");
    Check(!EvaluateEffectModules({*builtin.module},p,{1,{{"unknown","",1.f}}}).empty(),"Unknown override");
    Check(!EvaluateEffectModules({*parameterized.module},p,{1,{{"wind","",Float3{}},{"wind","",Float3{}}}}).empty(),"Duplicate override");
    for (const auto id : {"ce.lifetime","ce.size","ce.color","ce.size.life","ce.color.life"})
        Check(CompileBuiltinEffectModule({"assign",id}).module.has_value(),"Builtin assignment lowering");
    const auto life=CompileBuiltinEffectModule({"life","ce.lifetime",1,true,{{"seconds",ConstantInput{2.f}}}});
    p={};
    Check(EvaluateEffectModules({*life.module},p,{0}).empty() && p.lifetime==2 && (p.initialized&Attribute(ParticleAttribute::Lifetime)),"Initialization output");
    const auto disabled=CompileBuiltinEffectModule({"off","ce.gravity",1,false});
    Check(EvaluateEffectModules({*disabled.module},p,{1}).empty() && p.velocity.y==0,"Disabled module skips reads and stores");
    Check(!EvaluateEffectModules({*builtin.module},p,{1}).empty() && p.velocity.y==0,"Uninitialized read");
    p.initialized=63;
    Check(!EvaluateEffectModules({*builtin.module},p,{-1}).empty(),"Negative dt");
    Check(!EvaluateEffectModules({*builtin.module},p,{std::numeric_limits<float>::infinity()}).empty(),"Nonfinite dt");
    auto graph=GravityGraph(); graph.nodes[0].arguments[0]="missing"; Reject(graph,"graph.edge");
    graph=GravityGraph(); graph.nodes[4].arguments={"sum"}; Reject(graph,"graph.cycle");
    graph=GravityGraph(); graph.nodes.push_back({"loop",ModuleOpcode::Add,{"loop","loop"}}); Reject(graph,"graph.cycle");
    graph=GravityGraph(); graph.nodes.push_back(graph.nodes[0]); Reject(graph,"graph.id");
    graph=GravityGraph(); graph.nodes[0].id="bad id"; Reject(graph,"graph.id");
    graph=GravityGraph(); graph.nodes[0].arguments.pop_back(); Reject(graph,"graph.type");
    graph=GravityGraph(); graph.nodes[2].opcode=ModuleOpcode::Constant; graph.nodes[2].literal=true; Reject(graph,"graph.type");
    graph=GravityGraph(); graph.nodes[2].opcode=static_cast<ModuleOpcode>(255); Reject(graph,"graph.type");
    graph=GravityGraph(); graph.nodes[2].opcode=ModuleOpcode::Constant; graph.nodes[2].literal=std::numeric_limits<float>::infinity(); Reject(graph,"graph.type");
    graph=GravityGraph(); graph.nodes[3].port="unknown"; Reject(graph,"graph.type");
    graph=GravityGraph(); graph.nodes[4].attribute=ParticleAttribute::Position; Reject(graph,"graph.type");
    graph=GravityGraph(); graph.outputs.clear(); Reject(graph,"graph.output");
    graph=GravityGraph(); graph.outputs.push_back(graph.outputs[0]); Reject(graph,"graph.output");
    graph=GravityGraph(); graph.outputs[0].node="missing"; Reject(graph,"graph.output");
    graph=GravityGraph(); graph.outputs[0].node="dt"; Reject(graph,"graph.output");
    graph=GravityGraph(); graph.outputs[0].attribute=ParticleAttribute::Age; Reject(graph,"graph.output");
    auto definition=Gravity(); definition.stage=EffectStage::Initialize; Reject(GravityGraph(),"graph.type",definition);
    definition=Gravity(); definition.stage=EffectStage::Render; Reject(GravityGraph(),"module.contract",definition);
    definition=Gravity(); definition.writes|=Attribute(ParticleAttribute::Age); Reject(GravityGraph(),"module.contract",definition);
    definition=Gravity(); definition.reads|=128; Reject(GravityGraph(),"module.contract",definition);
    Reject(GravityGraph(),"module.version",Gravity(),{"gravity","ce.gravity",2});
    Reject(GravityGraph(),"input.unknown",Gravity(),{"gravity","ce.gravity",1,true,{{"bad",ConstantInput{1.f}}}});
    Reject(GravityGraph(),"input.duplicate",Gravity(),{"gravity","ce.gravity",1,true,{{"acceleration",ConstantInput{Float3{}}},{"acceleration",ConstantInput{Float3{}}}}});
    Reject(GravityGraph(),"input.type",Gravity(),{"gravity","ce.gravity",1,true,{{"acceleration",ConstantInput{1.f}}}});
    Check(CompileEffectModule(Gravity(),{"gravity","ce.gravity",1,true,{{"acceleration",RandomInput{Float3{},Float3{1,1,1}}}}},GravityGraph()).module.has_value(),"Random graph lowering");
    for (const auto id : {"ce.burst","ce.billboard","ce.position.sphere","ce.velocity.cone","unknown"})
        Check(!CompileBuiltinEffectModule({"unsupported",id}).module,"Unsupported lowering is explicit");
    // First module succeeds, second overflows: the entire stack must roll back.
    const auto huge=CompileBuiltinEffectModule({"huge","ce.gravity",1,true,{{"acceleration",ConstantInput{Float3{0,std::numeric_limits<float>::max(),0}}}}});
    p={}; p.initialized=63;
    Check(!EvaluateEffectModules({*builtin.module,*huge.module},p,{2}).empty() && p.velocity.y==0,"Whole-stack overflow rollback");
    p.velocity.y=std::numeric_limits<float>::quiet_NaN();
    Check(!EvaluateEffectModules({*builtin.module},p,{1}).empty(),"Nonfinite attribute read");
    // Multiple outputs read the original module-entry values, not earlier stores.
    ModuleDefinition swap{"swap",1,EffectStage::Update,{},12,12};
    ModuleGraph swapGraph{{{"p",ModuleOpcode::ReadAttribute,{},0.f,"",ParticleAttribute::Position},
        {"v",ModuleOpcode::ReadAttribute,{},0.f,"",ParticleAttribute::Velocity}},
        {{ParticleAttribute::Position,"v"},{ParticleAttribute::Velocity,"p"}}};
    const auto compiledSwap=CompileEffectModule(swap,{"swap","swap"},swapGraph);
    Check(compiledSwap.module.has_value(),"Swap graph compile");
    p={}; p.initialized=63; p.position.x=3; p.velocity.x=7;
    Check(EvaluateEffectModules({*compiledSwap.module},p,{0}).empty() && p.position.x==7 && p.velocity.x==3,"Simultaneous output commit");
    const auto small=CompileBuiltinEffectModule({"small","ce.size",1,true,{{"size",ConstantInput{2.f}}}});
    const auto large=CompileBuiltinEffectModule({"large","ce.size",1,true,{{"size",ConstantInput{5.f}}}});
    Check(EvaluateEffectModules({*small.module,*large.module},p,{0}).empty() && p.size==5,"Last writer wins across modules");
    ModuleDefinition computedSize{"computed.size",1,EffectStage::Update,{},0,Attribute(ParticleAttribute::Size)};
    ModuleGraph negativeSize{{{"negative",ModuleOpcode::Constant,{},-1.f}},{{ParticleAttribute::Size,"negative"}}};
    const auto negative=CompileEffectModule(computedSize,{"negative","computed.size"},negativeSize);
    Check(negative.module.has_value(),"Computed output has runtime domain check");
    Check(!EvaluateEffectModules({*small.module,*negative.module},p,{0}).empty() && p.size==5,"Output-domain whole-stack rollback");
    const auto parameterSize=CompileBuiltinEffectModule({"size","ce.size",1,true,{{"size",ParameterInput{"scale"}}}},{{"scale","",1.f}});
    Check(parameterSize.module.has_value(),"Domain-bound parameter compile");
    Check(!EvaluateEffectModules({*parameterSize.module},p,{0,{{"scale","",-1.f}}}).empty() && p.size==5,"Override preserves input domain");
    ModuleDefinition tint{"tint",1,EffectStage::Update,{},Attribute(ParticleAttribute::Color),Attribute(ParticleAttribute::Color)};
    ModuleGraph tintGraph{{{"color",ModuleOpcode::ReadAttribute,{},0.f,"",ParticleAttribute::Color},
        {"half",ModuleOpcode::Constant,{},.5f}, {"scaled",ModuleOpcode::Multiply,{"color","half"}}},{{ParticleAttribute::Color,"scaled"}}};
    const auto tintModule=CompileEffectModule(tint,{"tint","tint"},tintGraph);
    Check(tintModule.module.has_value(),"Color typed scaling");
    Check(EvaluateEffectModules({*tintModule.module},p,{0}).empty() && p.color.r==.5f && p.color.a==.5f,"RGBA scaling reference");
    // Sampling identity excludes views and stack order. Spawn samples ignore tick.
    ModuleEvaluationContext sampling; sampling.effectSeed=42; sampling.emitterSeed=7;
    sampling.particleId=11; sampling.simulationTick=1; sampling.emitterId="sparks";
    const auto randomLife=CompileBuiltinEffectModule({"life","ce.lifetime",1,true,
        {{"seconds",RandomInput{1.f,3.f,RandomSampling::AtSpawn}}}});
    Check(randomLife.module.has_value(),"Random lifetime compile");
    ParticleValues sampled;
    Check(EvaluateEffectModules({*randomLife.module},sampled,sampling).empty(),"Random lifetime evaluation");
    const auto firstSample=sampled.lifetime; sampling.simulationTick=999;
    Check(EvaluateEffectModules({*randomLife.module},sampled,sampling).empty() && sampled.lifetime==firstSample,"AtSpawn tick invariance");
    Check(firstSample>=1 && firstSample<=3,"Random range");
    sampling.particleId++;
    Check(EvaluateEffectModules({*randomLife.module},sampled,sampling).empty() && sampled.lifetime!=firstSample,"Per-particle random identity");
    const auto tickRandom=CompileBuiltinEffectModule({"gravity","ce.gravity",1,true,
        {{"acceleration",RandomInput{Float3{-1,-2,-3},Float3{1,2,3},RandomSampling::EachTick}}}});
    p={}; p.initialized=63; sampling.deltaSeconds=1;
    Check(EvaluateEffectModules({*tickRandom.module},p,sampling).empty(),"EachTick vector evaluation");
    const auto previousVelocity=p.velocity; p.velocity={}; sampling.simulationTick++;
    Check(EvaluateEffectModules({*tickRandom.module},p,sampling).empty() && p.velocity.y!=previousVelocity.y,"EachTick changes sample");
    Check(p.velocity.x>=-1 && p.velocity.x<=1 && p.velocity.y>=-2 && p.velocity.y<=2 && p.velocity.z>=-3 && p.velocity.z<=3,"Independent vector ranges");
    sampling.emitterId="";
    Check(!EvaluateEffectModules({*tickRandom.module},p,sampling).empty(),"Missing random identity rejected");
    const auto key=EffectRandomKey(42,7,11,"sparks","life","seconds",RandomSampling::AtSpawn,1);
    const auto golden=std::get<float>(SampleEffectRandom(RandomInput{0.f,1.f},key));
    Check(std::bit_cast<std::uint32_t>(golden)==1029994704u,"Distribution v1 golden vector");
    Check(std::get<std::uint32_t>(SampleEffectRandom(RandomInput{std::uint32_t{0},std::uint32_t{UINT32_MAX}},key))==2095613015u,"Full UInt range golden vector");
    Check(std::get<std::uint32_t>(SampleEffectRandom(RandomInput{std::uint32_t{9},std::uint32_t{9}},key))==9,"Degenerate UInt range");
    for (std::uint64_t k=0;k<100;++k) {
        const auto value=std::get<std::uint32_t>(SampleEffectRandom(RandomInput{std::uint32_t{3},std::uint32_t{7}},k));
        Check(value>=3 && value<=7,"Inclusive UInt interval");
    }
    const auto curve=CompileBuiltinEffectModule({"curve","ce.size.life",1,true,
        {{"size",CurveInput{{{0,0},{.25f,2},{1,8}}}}}});
    const auto gradient=CompileBuiltinEffectModule({"gradient","ce.color.life",1,true,
        {{"color",GradientInput{{{0,Color4{0,2,4,0}},{1,Color4{2,4,8,1}}}}}}});
    Check(curve.module.has_value() && gradient.module.has_value(),"Curve and gradient compile");
    for (const auto time : {0.f,.25f,.5f,1.f,2.f}) {
        p={}; p.initialized=63; p.age=time;
        Check(EvaluateEffectModules({*curve.module,*gradient.module},p,{}).empty(),"Life inputs evaluate");
        Check(Near(p.size,8*std::min(time,1.f)) && Near(p.color.r,2*std::min(time,1.f)) && Near(p.color.a,std::min(time,1.f)),"Linear life interpolation and clamping");
    }
    p={}; p.initialized=Attribute(ParticleAttribute::Age);
    Check(!EvaluateEffectModules({*curve.module},p,{}).empty(),"Curve implicit lifetime read required");
    p.initialized=63; p.lifetime=0;
    Check(!EvaluateEffectModules({*curve.module},p,{}).empty(),"Invalid normalized lifetime");
    // Implicit reads also apply to custom definitions with no explicit age/life mask.
    ModuleDefinition implicit{"implicit",1,EffectStage::Update,{{"size",InputType::Float,1.f}},0,Attribute(ParticleAttribute::Size)};
    ModuleGraph implicitGraph{{{"value",ModuleOpcode::Input,{},0.f,"size"}},{{ParticleAttribute::Size,"value"}}};
    const auto implicitCurve=CompileEffectModule(implicit,{"implicit","implicit",1,true,{{"size",CurveInput{{{0,1},{1,2}}}}}},implicitGraph);
    p={};
    Check(implicitCurve.module.has_value() && !EvaluateEffectModules({*implicitCurve.module},p,{}).empty(),"Custom curve implicit reads");
    p={}; p.initialized=63; p.velocity={2,0,0}; bool alive=true;
    Check(StepEffectParticle({*builtin.module},p,alive,{.1f}).empty() && alive && Near(p.age,.1f) && Near(p.position.x,.2f) && Near(p.position.y,-.098f),"Single engine step sequence");
    const auto paused=p;
    Check(StepEffectParticle({*builtin.module},p,alive,{0}).empty() && p.age==paused.age && p.position.y==paused.position.y,"Pause leaves state unchanged");
    p.age=.9f; p.lifetime=1; p.position={};
    Check(StepEffectParticle({*builtin.module},p,alive,{.1f}).empty() && !alive && p.position.y==0,"Retire before update/integration at exact lifetime");
    const auto retired=p;
    Check(StepEffectParticle({*builtin.module},p,alive,{1}).empty() && p.age==retired.age,"Dead particle remains retired");
    p={}; p.initialized=63; alive=true;
    Check(StepEffectParticle({*huge.module},p,alive,{.5f}).empty(),"Finite large velocity step");
    p={}; p.initialized=63; p.lifetime=100; alive=true;
    Check(!StepEffectParticle({*huge.module},p,alive,{2}).empty() && p.age==0 && p.velocity.y==0 && alive,"Step rollback includes age and alive");
    Check(!StepEffectParticle({*life.module},p,alive,{.1f}).empty(),"Update stage enforcement");
    std::cout << "EFFECT_MODULE_IR_OK checks=" << checks << '\n';
}
