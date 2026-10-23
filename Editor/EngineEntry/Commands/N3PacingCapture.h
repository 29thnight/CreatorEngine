#pragma once
#include "CharacterMovementComponent.h"
#include "PhysicsBodyComponent.h"
#include "DiagnosticDocument.h"
#include "TimeSystem.h"
#include <fstream>

// Explicit local development consumer of the existing copied-value tick adapter.
// No callback or borrowed entity pointer survives a scene epoch change.
namespace N3Pacing
{
    inline void Vector(Diagnostics::Node node, math::vector3 v)
    { node.SetSequence(); node.Append()<<v.x;node.Append()<<v.y;node.Append()<<v.z; }
    inline math::vector3 Translation(const math::matrix4x4& matrix)
    {std::array<float,16> v;std::memcpy(v.data(),&matrix,sizeof(matrix));return {v[12],v[13],v[14]};}
    struct Recorder final : Runtime::SimulationAdapter
    {
        std::uint64_t sceneEpoch{}, exchangeEpoch{}, first{}, last{}, previousFrameTick{};
        EntityHandle character{}, body{}, child{};
        Diagnostics::Document document;
        std::chrono::steady_clock::time_point started=std::chrono::steady_clock::now();
        std::uint64_t frames{};
        bool moving{}, armed{};
        std::string movingPath, captureError;
        Scene* ResolveScene() const
        {return SceneManagers->SceneContextEpoch()==sceneEpoch ? SceneManagers->GetActiveScene() : nullptr;}
        bool Apply(const Runtime::TickCommand& c) override
        {
            auto* scene=ResolveScene();auto* entity=scene?scene->Resolve(character):nullptr;
            auto* movement=entity?entity->GetComponent<CharacterMovementComponent>():nullptr;
            auto* other=scene?scene->Resolve(body):nullptr;
            auto* rigid=other?other->GetComponent<PhysicsBodyComponent>():nullptr;
            if(!movement||!rigid||c.kind!=0x4e335043||c.bytes!=1)return false;
            const auto tick=c.tick-first+1;
            if(!movement->SetDesiredVelocity({tick<=60?3.f:-3.f,0,0}))return false;
            if(tick==30 && !movement->Jump())return false;
            if(tick==100)
            {
                if(!movement->Teleport({-2.f,1.05f,0}))return false;
                if(auto* camera=scene->Cameras().GetPrimaryCamera())camera->NotifyCameraCut();
            }
            return bool(rigid->SetVelocity({2,0,0}));
        }
        bool Capture(std::span<std::byte>,std::uint32_t& bytes,std::uint32_t& schema) override
        {
            auto* scene=ResolveScene();auto* e=scene?scene->Resolve(character):nullptr;
            auto* c=e?e->GetComponent<CharacterMovementComponent>():nullptr;
            auto* b=scene?scene->Resolve(body):nullptr;
            auto* rigid=b?b->GetComponent<PhysicsBodyComponent>():nullptr;
            if(!c||!rigid)return false;
            const auto state=c->ReadState();const auto bs=rigid->ReadState();if(!state||!bs)return false;
            const auto executing=SceneManagers->SimulationCommands().Status().executing;
            auto item=document.Root()["ticks"].Append();item.SetMap();
            item["tick"]<<executing;item["relative"]<<(executing-first+1);item["physicsTick"]<<state->tick.value;
            Vector(item["position"],state->collision.position);Vector(item["body"],bs->transform.position);
            item["below"]<<state->collision.below;item["fallVelocity"]<<state->fall_velocity;
            bytes=0;schema=0x4e33;
            if(executing==last)SceneManagers->SetGamePaused(true);
            return true;
        }
        void Frame()
        {
            auto* scene=ResolveScene();if(!scene)return;
            auto* entity=scene->Resolve(character);auto* other=scene->Resolve(body);auto* descendant=scene->Resolve(child);
            if(!entity||!other||!descendant)return;
            const auto status=SceneManagers->SimulationCommands().Status();
            const auto render=EnhancedSceneRenderer::GetLiveRenderThreadStats();
            auto item=document.Root()["frames"].Append();item.SetMap();
            item["engineFrame"]<<Time->GetFrameCount();
            item["frame"]<<++frames;item["seconds"]<<std::chrono::duration<double>(std::chrono::steady_clock::now()-started).count();
            item["committed"]<<status.committed;item["alpha"]<<scene->SimulationInterpolation();
            item["published"]<<render.published;item["consumed"]<<render.consumed;
            Vector(item["simulation"],entity->Transform_().GetWorldPosition());
            Vector(item["render"],Translation(scene->PhysicsRenderMatrix(character)));
            Vector(item["bodyRender"],Translation(scene->PhysicsRenderMatrix(body)));
            Vector(item["childRender"],Translation(scene->PhysicsRenderMatrix(child)));
            if(auto* camera=scene->Cameras().GetPrimaryCamera())
            {
                const auto snapshot=camera->CaptureFrameSnapshot();
                Vector(item["cameraEye"],snapshot.eyePosition);
                item["cameraCut"]<<snapshot.cameraCutRevision;
                item["cameraLodDistance"]<<camera->CalculateLODDistance(Translation(scene->PhysicsRenderMatrix(character)));
                if(const auto frustum=camera->TryGetFrustum())Vector(item["simulationFrustumOrigin"],frustum->origin);
                if(const auto frustum=camera->TryGetRenderFrustum())Vector(item["renderFrustumOrigin"],frustum->origin);
            }
            if (moving && !armed && status.committed > first+20 && status.committed < first+80
                && status.committed == previousFrameTick && scene->SimulationInterpolation()<.9)
            {
                // Diagnostic readback is allowed to wait for render control.
                // A try-lock retry on a later frame loses this zero-tick pose;
                // RT's 50 ms yield can itself eliminate the next zero-tick frame.
                // Admission remains before publication of this actual engine frame.
                armed=EnhancedSceneRenderer::RequestLivePbrCapture(movingPath,
                    EnhancedLiveDisplayTarget::Game,captureError,false,{},{},{},false,false,false);
            }
            previousFrameTick=status.committed;
        }
    };
    inline CommandCore::CommandResult Transition(const ConsoleCommandContext& ctx)
    {
        using namespace CommandCore;
        if(ctx.parts.size()!=2)return InvalidArguments("simulation.pacing.transition <prepare|disable|enable|warp|remove>");
        auto* scene=SceneManagers->GetActiveScene();
        if(!scene||!SceneManagers->IsPlayCommitted()||!SceneManagers->IsGamePaused())return Fail("n3.phase","Committed paused Play required");
        Entity* owner=nullptr;for(const auto& e:scene->m_Entities)if(e&&e->ToString()=="N3Character")owner=e.get();
        auto* character=owner?owner->GetComponent<CharacterMovementComponent>():nullptr;
        if(!character)return Fail("n3.character","Missing fixture character");
        const auto state=character->ReadState();if(!state)return Fail("n3.character",std::string(state.error().message));
        const auto position=state->collision.position;
        const auto& op=ctx.parts[1];
        if(op=="prepare")scene->SetSimulationInterpolation(0);
        else if(op=="disable")character->SetEnabled(false);
        else if(op=="enable")character->SetEnabled(true);
        else if(op=="warp"){if(!character->Teleport(position))return Fail("n3.warp","Warp failed");}
        else if(op=="remove")character->Destroy();
        else return InvalidArguments("Unknown pacing transition");
        auto data=CommandData::Object();auto values=CommandData::Array();
        values.Append(CommandData::Double(position.x));values.Append(CommandData::Double(position.y));values.Append(CommandData::Double(position.z));
        data.Set("simulation",std::move(values));data.Set("transition",CommandData::String(op));
        return Ok({},std::move(data));
    }

    inline CommandCore::CommandResult Capture(const ConsoleCommandContext& ctx)
    {
        using namespace CommandCore;
        if(ctx.parts.size()!=2 && !(ctx.parts.size()==3 && ctx.parts[2]=="moving"))return InvalidArguments("simulation.pacing.capture <new-absolute-directory> [moving]");
        auto* scene=SceneManagers->GetActiveScene();
        if(!scene||!SceneManagers->IsPlayCommitted()||!SceneManagers->IsGamePaused())return Fail("n3.phase","Committed paused Play required");
        const auto path=std::filesystem::path(std::u8string(ctx.parts[1].begin(),ctx.parts[1].end()));
        std::error_code ec;if(!path.is_absolute()||!std::filesystem::create_directories(path,ec)||ec)return Fail("n3.path","New absolute directory required");
        auto record=std::make_shared<Recorder>();record->moving=ctx.parts.size()==3;record->movingPath=(path/"moving-pbr").generic_string();record->sceneEpoch=SceneManagers->SceneContextEpoch();
        for(const auto& e:scene->m_Entities)if(e){
            if(e->ToString()=="N3Character")record->character=scene->HandleOf(e->m_index);
            if(e->ToString()=="N3Body")record->body=scene->HandleOf(e->m_index);
            if(e->ToString()=="N3Child")record->child=scene->HandleOf(e->m_index);
        }
        auto* e=scene->Resolve(record->character);auto* c=e?e->GetComponent<CharacterMovementComponent>():nullptr;
        if(!c||!scene->Resolve(record->body)||!scene->Resolve(record->child))return Fail("n3.fixture","Missing N3 fixture entities");
        if(!c->Teleport({0,1.05f,0})||!c->SetDesiredVelocity({}))return Fail("n3.reset","Cannot reset character");
        const auto status=SceneManagers->SimulationCommands().Status();
        record->exchangeEpoch=status.epoch;record->first=status.committed+1;record->last=status.committed+120;
        auto root=record->document.Root();root.SetMap();root["schemaVersion"]<<1;root["firstTick"]<<record->first;root["lastTick"]<<record->last;
        root["ticks"].SetSequence();root["frames"].SetSequence();
        if(SceneManagers->SimulationCommands().Bind(status.epoch,record)!=Runtime::TickResult::Ok)return Fail("n3.bind","Adapter busy");
        for(auto tick=record->first;tick<=record->last;++tick){
            std::uint64_t sequence{};const std::array<std::byte,1> payload{std::byte{1}};
            if(SceneManagers->SimulationCommands().Submit(status.epoch,tick,0x4e335043,payload,sequence)!=Runtime::TickResult::Ok){
                SceneManagers->ResetSimulationCommands();return Fail("n3.submit","Cannot submit copied tick inputs");}
        }
        record->Frame();SceneManagers->SetGamePaused(false);
        ctx.system.WaitForResult([record,path]() -> std::optional<CommandResult>{
            if(!record->ResolveScene()||SceneManagers->SimulationCommands().Status().epoch!=record->exchangeEpoch)
                return Fail("n3.epoch","Scene/exchange changed during capture");
            record->Frame();const auto s=SceneManagers->SimulationCommands().Status();
            if(s.committed<record->last){
                if(std::chrono::steady_clock::now()-record->started<std::chrono::seconds(30))return std::nullopt;
                SceneManagers->SetGamePaused(true);SceneManagers->ResetSimulationCommands();return Fail("n3.timeout","Real frame pacing timed out");
            }
            if(record->moving)
            {
                EnhancedLivePbrCaptureStatus capture;
                if(!record->armed){
                    std::ofstream trace(path/"trace.json",std::ios::binary);trace<<record->document.ToJson();
                    return Fail("n3.moving","No zero-tick window admitted a real moving capture: "+record->captureError);
                }
                if(!EnhancedSceneRenderer::TryGetLivePbrCaptureStatus(capture))return std::nullopt;
                if(capture.state==EnhancedPbrCaptureState::Pending || capture.state==EnhancedPbrCaptureState::Recording)return std::nullopt;
                if(capture.state!=EnhancedPbrCaptureState::Complete)return Fail("n3.moving",capture.error);
            }
            SceneManagers->SimulationCommands().Bind(s.epoch,nullptr);
            std::ofstream output(path/"trace.json",std::ios::binary);output<<record->document.ToJson();output.close();
            if(!output)return Fail("n3.output","Cannot persist trace");
            auto data=CommandData::Object();data.Set("frames",CommandData::Int(record->frames));
            data.Set("scope",CommandData::String("Recorded actual frames/ticks; independent oracle required"));
            return Ok({},std::move(data));
        },true);
        return Ok();
    }
}
