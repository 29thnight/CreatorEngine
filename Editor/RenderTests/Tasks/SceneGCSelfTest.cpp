#include "Tasks/SceneGCSelfTest.h"
#include "Scene.h"
#include "SceneManager.h"
#include "Component.h"
#include "Entity.h"
#include "LifecycleRegistry.h"
#include "ScriptObjectRegistry.h"
#if defined(CE_CONTRACT_STANDALONE)
#include "../../../Tools/build/ModuleAdapters/Creator.Concurrency.Jobs.h"
#else
import Creator.Concurrency.Jobs;
#endif
#include "Terrain.h"
#include "LightComponent.h"
#include "SpriteRenderer.h"
#include "ImageComponent.h"
#include "TextComponent.h"
#include "SpriteSheetComponent.h"
#include "MeshRenderer.h"
#include "FoliageComponent.h"
#include "DecalComponent.h"
#include "Animator.h"
#include "../../EngineEntry/Editing/ReflectionPropertyEdit.h"

#include "../../../Engine/Utility_Framework/Foundation/StandardLibrary.h"

namespace
{
    void Require(bool condition, const char* message)
    {
        if (!condition)
        {
            throw std::runtime_error(message);
        }
    }

    struct ObservationRecorder final : Runtime::ISceneEntityObserver, Runtime::IComponentWriteObserver
    {
        std::vector<Runtime::SceneEntityEvent> entities;
        std::vector<Runtime::ComponentWriteEvent> writes;
        void OnEntityChanged(const Runtime::SceneEntityEvent& event) override {entities.push_back(event);}
        void OnComponentWritten(const Runtime::ComponentWriteEvent& event) override {writes.push_back(event);}
    };
    struct ObservationLease
    {
        Runtime::SceneObservation& bus;
        std::shared_ptr<ObservationRecorder> recorder=std::make_shared<ObservationRecorder>();
        uint64_t entity=bus.Subscribe(std::static_pointer_cast<Runtime::ISceneEntityObserver>(recorder));
        uint64_t write=bus.Subscribe(std::static_pointer_cast<Runtime::IComponentWriteObserver>(recorder));
        ~ObservationLease(){bus.Unsubscribe(write);bus.Unsubscribe(entity);}
    };
    void VerifyAuthoringWrites(ObservationRecorder& observation)
    {
        auto* scene=SceneManagers->GetActiveScene();
        if(!scene)scene=SceneManagers->CreateScene("ObservationAuthoring");
        Require(scene!=nullptr,"Missing authoring scene");
        auto* entity=scene->CreateEntity("ObservationAuthoringProbe");
        auto* light=entity->AddComponent<LightComponent>();
        scene->CollectLightComponent(light);
        const auto published=scene->GetRenderProxyCommitMetrics().publishCalls;
        auto* undo=Meta::UndoManager::GetInstance();
        const auto depth=undo->EditUndoDepth();
        observation.writes.clear();
        const auto old=light->m_intencity;
        Require((Meta::TypedDraw::CommitMemberChange<LightComponent,float,&LightComponent::m_intencity>(light,old,old+2,"m_intencity")),"Typed scalar edit rejected");
        Require(light->m_intencity==old+2&&observation.writes.size()==1
            && observation.writes.back().source==Meta::PropertyChangeSource::Inspector
            && std::string_view(observation.writes.back().property.data())=="m_intencity","Scalar notification fanout or origin incorrect");
        Require(scene->GetRenderProxyCommitMetrics().pending>0 && scene->GetRenderProxyCommitMetrics().publishCalls>published,"Inspector edit did not publish render dirty");
        undo->Undo();Require(light->m_intencity==old&&observation.writes.back().source==Meta::PropertyChangeSource::Undo,"Undo notification missing");
        undo->Redo();Require(light->m_intencity==old+2&&observation.writes.back().source==Meta::PropertyChangeSource::Redo,"Redo notification missing");
        Require(undo->EditUndoDepth()==depth+1,"Scalar produced duplicate Undo records");
        observation.writes.clear();
        { Meta::TypedDraw::ComplexMemberEdit edit(light,"m_lightType");light->m_lightType=LightType::PointLight;edit.Finish(); }
        Require(observation.writes.size()==1&&observation.writes.back().source==Meta::PropertyChangeSource::Inspector,"Enum transaction notification missing");
        undo->Undo();Require(light->m_lightType==LightType::DirectionalLight,"Enum Undo missing");
        undo->Redo();Require(light->m_lightType==LightType::PointLight,"Enum Redo missing");
        observation.writes.clear();
        { Meta::TypedDraw::ComplexMemberEdit edit(light,"m_lightType");edit.Finish(); }
        Require(observation.writes.empty(),"No-op gesture produced a write");
        entity->m_editorLocked=true;
        Require(!(Meta::TypedDraw::CommitMemberChange<LightComponent,float,&LightComponent::m_intencity>(light,old+2,91,"m_intencity")),"Locked authoring accepted");
        Require(light->m_intencity==old+2&&observation.writes.empty(),"Locked authoring leaked a value/write");
        entity->m_editorLocked=false;
        auto* animator=entity->AddComponent<Animator>();
        observation.writes.clear();
        { Meta::TypedDraw::ComplexMemberEdit edit(animator,"m_TwoBoneIKConstraints");
          animator->m_TwoBoneIKConstraints.push_back(TwoBoneIKConstraint{});
          animator->m_TwoBoneIKConstraints.back().StartBone="root";edit.Finish(); }
        Require(animator->m_TwoBoneIKConstraints.size()==1&&observation.writes.size()==1,"Container/nested transaction missing");
        Authoring::WriteDocument legacyNull;
        legacyNull.Root().Child("m_TwoBoneIKConstraints").SetNull();
        Meta::Deserialize(animator,*Meta::Find(animator->GetTypeID()),legacyNull.Root().Read());
        Require(animator->m_TwoBoneIKConstraints.size()==1,"Legacy null container load was changed");
        undo->Undo();Require(animator->m_TwoBoneIKConstraints.empty(),"Container Undo missing");
        undo->Redo();Require(animator->m_TwoBoneIKConstraints.size()==1&&animator->m_TwoBoneIKConstraints.front().StartBone=="root","Container nested Redo missing");
        observation.writes.clear();
        light->SetRange(27);Require(observation.writes.size()==1&&observation.writes.back().allProperties,"Native setter notification missing");
        observation.writes.clear();
        entity->Transform_().SetPosition({3,4,5},TransformWriteReason::Script);
        Require(observation.writes.size()==1&&observation.writes.back().source==Meta::PropertyChangeSource::Script,"Transform Script writer notification missing");
        observation.writes.clear();
        entity->Transform_().NotifyPropertyChanged("position",Meta::PropertyChangeSource::Undo);
        Require(observation.writes.size()==1&&observation.writes.back().source==Meta::PropertyChangeSource::Undo,"Transform virtual hook duplicated write/origin");
        scene->UnCollectLightComponent(light);
        entity->Destroy();scene->EndFramePass();
        const auto writes=observation.writes.size();undo->Undo();undo->Redo();
        Require(observation.writes.size()==writes,"Stale Undo wrote to a retired component");
    }

    template<class T>
    void VerifyRenderRegistration(gc::domain& domain, Scene& scene, Entity& owner,
        void (Scene::*collect)(T*), void (Scene::*uncollect)(T*))
    {
        auto component = Component::CreateManaged<T>(domain);
        const auto before = scene.GetRenderProxyCommitMetrics();
        // Null and ownerless admission must leave the scene unchanged.
        (scene.*collect)(nullptr);
        (scene.*uncollect)(nullptr);
        (scene.*collect)(component.get());
        Require(scene.RenderProxyComponentCount() == before.registered,
            "Ownerless render component was admitted");

        component->SetOwner(&owner);
        (scene.*collect)(component.get());
        (scene.*collect)(component.get());
        const auto registered = scene.GetRenderProxyCommitMetrics();
        Require(registered.registered == before.registered + 1
            && registered.pending == before.pending + 1,
            "Duplicate render admission changed registration or ticket count");
        Require(scene.PublishRenderProxyDirty(component.get(), ProxyDirty::Transform),
            "Registered render component rejected dirty publication");

        (scene.*uncollect)(component.get());
        (scene.*uncollect)(component.get());
        Require(!scene.PublishRenderProxyDirty(component.get(), ProxyDirty::Transform),
            "Removed render component accepted dirty publication");
        // Re-admit the same address while the preceding generation is queued.
        (scene.*collect)(component.get());
        (scene.*uncollect)(component.get());
        Require(scene.RenderProxyComponentCount() == before.registered,
            "Render removal leaked a registration");
        scene.CommitRenderProxies();
        const auto drained = scene.GetRenderProxyCommitMetrics();
        Require(drained.lastStale == 2 && drained.lastCommitted == 0 && drained.pending == 0,
            "Retired render generations were dispatched or retained");
        component->SetOwner(nullptr);
        component->FinalizeManagedDestroy();
    }

    void VerifyLightCompaction(gc::domain& domain, Scene& scene, Entity& owner)
    {
        auto first = Component::CreateManaged<LightComponent>(domain);
        auto second = Component::CreateManaged<LightComponent>(domain);
        auto point = Component::CreateManaged<LightComponent>(domain);
        first->SetOwner(&owner);
        second->SetOwner(&owner);
        point->SetOwner(&owner);
        first->m_lightIndex = 2;
        second->m_lightIndex = 5;
        point->m_lightIndex = 0;
        first->m_lightType = LightType::DirectionalLight;
        second->m_lightType = LightType::DirectionalLight;
        point->m_lightType = LightType::PointLight;
        for (const size_t invalid : {
            static_cast<size_t>((std::numeric_limits<int>::max)()) + 1u,
            static_cast<size_t>(-2), (std::numeric_limits<size_t>::max)() })
        {
            bool rejected = false;
            try
            {
                scene.EnsureLightSlot(invalid);
            }
            catch (const std::out_of_range&)
            {
                rejected = true;
            }
            Require(rejected, "Out-of-range light slot was not rejected");
        }
        Require(scene.AddLight() == 0, "Invalid light slot input mutated the registry");
        scene.RemoveLight(0);
        scene.EnsureLightSlot(0);
        scene.EnsureLightSlot(2);
        scene.EnsureLightSlot(5);
        scene.CollectLightComponent(first.get());
        scene.CollectLightComponent(second.get());
        scene.CollectLightComponent(point.get());
        scene.DestroyLight();
        Require(first->m_lightIndex == 1 && second->m_lightIndex == 2 && point->m_lightIndex == 0,
            "Sparse serialized light slots were remapped incorrectly");
        Require(first->m_lightStatus == LightStatus::StaticShadows
            && second->m_lightStatus == LightStatus::Enabled,
            "Light compaction changed first-directional shadow selection");
        Require(scene.AddLight() == 3, "Compacted light slot count was incorrect");
        scene.RemoveLight(3);
        scene.UnCollectLightComponent(first.get());
        scene.RemoveLight(1);
        for (int iteration = 0; iteration < 32; ++iteration)
        {
            scene.DestroyLight();
            Require(second->m_lightIndex == 1 && point->m_lightIndex == 0,
                "Repeated light compaction changed live indices");
        }
        Require(scene.AddLight() == 2, "Removed light slot survived compaction");
        scene.RemoveLight(2);
        scene.UnCollectLightComponent(second.get());
        scene.UnCollectLightComponent(point.get());
        scene.RemoveLight(0);
        scene.RemoveLight(1);
        scene.DestroyLight();
        first->SetOwner(nullptr);
        second->SetOwner(nullptr);
        point->SetOwner(nullptr);
        first->FinalizeManagedDestroy();
        second->FinalizeManagedDestroy();
        point->FinalizeManagedDestroy();
    }

    struct ProbeResults
    {
        int ended = 0;
        int removed = 0;
        int uninitialized = 0;
        int reclaimed = 0;
        bool handlesValidInCleanup = true;
    };

    class CycleProbe final : public Component
    {
    public:
        explicit CycleProbe(ProbeResults& results) : m_results(results)
        {
            m_typeID = type_guid(CycleProbe);
        }

        ~CycleProbe() override
        {
            ++m_results.reclaimed;
        }

        void gc_trace(gc::tracer& tracer) const override
        {
            Component::gc_trace(tracer);
            tracer.visit(peer);
            tracer.visit(retainedScene);
            tracer.visit(retainedEntity);
        }

        void OnEndSimulation() override
        {
            ++m_results.ended;
        }

        void OnRemovingFromScene() override
        {
            ++m_results.removed;
        }

        void OnUninitializing() override
        {
            ++m_results.uninitialized;
            m_results.handlesValidInCleanup = m_results.handlesValidInCleanup
                && GetOwner() != nullptr
                && ScriptObjectRegistry::Get().Resolve(scriptHandle) == GetOwner();
        }

        // Intentionally retain graph cycles after logical cleanup. Their release
        // is the collector's job, not a destructor that recursively owns peers.
        gc::trace_ref<CycleProbe> peer;
        gc::trace_ref<Scene> retainedScene;
        gc::trace_ref<Entity> retainedEntity;
        ScriptObjectHandle scriptHandle;
        const unsigned marker = 0xC0FFEEu;

    private:
        ProbeResults& m_results;
    };

    class JoinedBorrow final
    {
    public:
        explicit JoinedBorrow(CycleProbe& probe)
            : m_pin(probe.root_from_this()),
              m_readSucceeded(std::make_shared<std::atomic_bool>(false))
        {
            auto ready = m_gate.get_future().share();
            // The worker closure carries no gc root, weak promotion or pin.
            m_job = ce::get_job_scheduler().submit(
                [borrowed = m_pin.get(), ready, result = m_readSucceeded]
                {
                    ready.wait();
                    result->store(borrowed->marker == 0xC0FFEEu);
                });
        }

        ~JoinedBorrow()
        {
            Finish();
        }

        void Finish()
        {
            if (!m_joined)
            {
                m_gate.set_value();
                m_job.wait();
                m_pin.release();
                m_joined = true;
            }
        }

        bool ReadSucceeded() const
        {
            return m_readSucceeded->load();
        }

    private:
        // Owner-thread lifetime: the actual borrowed component stays pinned
        // until the job token says its callback and captured values are gone.
        gc::pinned<CycleProbe> m_pin;
        std::promise<void> m_gate;
        std::shared_ptr<std::atomic_bool> m_readSucceeded;
        job_handle m_job;
        bool m_joined = false;
    };
}

namespace RenderTest
{
    bool RunSceneGCSelfTest(std::string& log)
    {
        auto& domain = SceneManagers->ManagedDomain();
        ProbeResults results;
        gc::root_ref<Scene> source;
        gc::root_ref<Scene> destination;
        std::vector<DetachedEntityTransfer> transfers;
        try
        {
            Require(!SceneManagers->IsPlayCommitted(), "GC regression requires an idle host");
            ObservationLease observation{SceneManagers->Observations()};
            VerifyAuthoringWrites(*observation.recorder);
            {
                auto abandoned=Scene::CreateNewScene(domain,"AbandonedTransfer");
                auto* entity=abandoned->CreateEntity("DetachedFinalization");
                const auto identity=entity->GetInstanceID();
                std::vector<DetachedEntityTransfer> pending;
                abandoned->DetachEntityHierarchy(entity,pending);
                entity->FinalizeManagedDestroy();
                entity->FinalizeManagedDestroy();
                pending.clear();abandoned->RetireManagedGraph();abandoned.reset();
                std::vector<Runtime::EntityChange> events;
                for(const auto& event:observation.recorder->entities)if(event.instance==identity)events.push_back(event.change);
                Require(events==std::vector{Runtime::EntityChange::Created,Runtime::EntityChange::Detached,Runtime::EntityChange::Destroyed},"Abandoned DDOL transfer terminal notification incorrect");
            }
            observation.recorder->entities.clear();
            domain.collect_full();
            const auto baseline = domain.stats();
            Lifecycle::Registry::Register<CycleProbe>();
            source = Scene::CreateNewScene(domain, "SceneGCSource");
            destination = Scene::CreateNewScene(domain, "SceneGCDestination");

            Entity* first = source->CreateEntity("CycleFirst");
            Entity* second = source->CreateEntity("CycleSecond");
            VerifyRenderRegistration(domain, *source, *first,
                &Scene::CollectTerrainComponent, &Scene::UnCollectTerrainComponent);
            VerifyRenderRegistration(domain, *source, *first,
                &Scene::CollectSpriteRenderer, &Scene::UnCollectSpriteRenderer);
            VerifyRenderRegistration(domain, *source, *first,
                &Scene::CollectImageComponent, &Scene::UnCollectImageComponent);
            VerifyRenderRegistration(domain, *source, *first,
                &Scene::CollectTextComponent, &Scene::UnCollectTextComponent);
            VerifyRenderRegistration(domain, *source, *first,
                &Scene::CollectSpriteSheetComponent, &Scene::UnCollectSpriteSheetComponent);
            VerifyRenderRegistration(domain, *source, *first,
                &Scene::CollectLightComponent, &Scene::UnCollectLightComponent);
            VerifyRenderRegistration(domain, *source, *first,
                &Scene::CollectMeshRenderer, &Scene::UnCollectMeshRenderer);
            VerifyRenderRegistration(domain, *source, *first,
                &Scene::CollectFoliageComponent, &Scene::UnCollectFoliageComponent);
            VerifyRenderRegistration(domain, *source, *first,
                &Scene::CollectDecalComponent, &Scene::UnCollectDecalComponent);
            VerifyLightCompaction(domain, *source, *first);

            auto* firstProbe = first->AddComponent<CycleProbe>(results);
            auto* secondProbe = second->AddComponent<CycleProbe>(results);
            observation.recorder->writes.clear();
            firstProbe->NotifyPropertyChanged("peer",Meta::PropertyChangeSource::Reflection);
            Require(observation.recorder->writes.size()==1 && observation.recorder->writes.back().component==firstProbe->GetInstanceID(),"Non-render component write missing");
            firstProbe->peer = secondProbe->root_from_this();
            secondProbe->peer = firstProbe->root_from_this();
            firstProbe->retainedScene = source;
            secondProbe->retainedScene = source;
            firstProbe->retainedEntity = first->root_from_this();
            secondProbe->retainedEntity = second->root_from_this();
            firstProbe->scriptHandle = ScriptObjectRegistry::Get().Register(first);
            secondProbe->scriptHandle = ScriptObjectRegistry::Get().Register(second);
            const auto firstScript = firstProbe->scriptHandle;
            const auto secondScript = secondProbe->scriptHandle;
            const auto firstIdentity=first->GetInstanceID();
            const auto secondIdentity=second->GetInstanceID();
            const auto staleFirst = source->HandleOf(first->m_index);
            const auto staleSecond = source->HandleOf(second->m_index);
            const auto weakFirst = first->weak_from_this();
            const auto weakSecond = second->weak_from_this();
            const auto weakFirstProbe = firstProbe->weak_from_this();
            const auto weakSecondProbe = secondProbe->weak_from_this();
            const auto weakSource = source->weak_from_this();
            const auto weakDestination = destination->weak_from_this();

            source->m_selectedEntity = first;
            source->m_selectedEntities = { first };
            source->m_simulationSelection = { first };
            {
                JoinedBorrow borrow(*firstProbe);
                first->Destroy();
                Require(ScriptObjectRegistry::Get().Resolve(firstScript) == first,
                    "Destroy request invalidated script handle before cleanup hooks");
                source->EndFramePass();
                Require(results.uninitialized == 1 && results.handlesValidInCleanup,
                    "Existing component cleanup hooks did not run with a valid script handle");
                Require(source->Resolve(staleFirst) == nullptr
                    && ScriptObjectRegistry::Get().Resolve(firstScript) == nullptr,
                    "Logical destruction left native or script handles valid");
                Require(source->m_selectedEntity == nullptr && source->m_selectedEntities.empty()
                    && source->m_simulationSelection.empty(), "Selection retained a destroyed borrow");
                auto retained = weakFirst.lock();
                Require(retained && gc::lifecycle_of(retained) == gc::lifecycle_state::destroyed,
                    "Pinned cycle did not preserve memory independently of logical lifetime");
                retained.reset();
                domain.collect_full();
                Require(results.reclaimed == 0, "Collector reclaimed a pinned graph borrow");
                borrow.Finish();
                Require(borrow.ReadSucceeded(), "Worker borrow did not remain alive through join");
            }

            Entity* replacement = source->CreateEntity("ReusedSlot");
            Require(replacement->m_index == staleFirst.index && source->Resolve(staleFirst) == nullptr,
                "Slot reuse revived a stale generation handle");
            source->m_selectedEntity = second;
            source->m_selectedEntities = { second };
            source->m_simulationSelection = { second };
            source->DetachEntityHierarchy(second, transfers);
            Require(!transfers.empty() && source->Resolve(staleSecond) == nullptr,
                "DDOL transfer did not retain its entity or invalidate the source handle");
            Require(ScriptObjectRegistry::Get().Resolve(secondScript) == second,
                "DDOL transfer invalidated a live script handle");
            Require(source->m_selectedEntity == nullptr && source->m_selectedEntities.empty()
                && source->m_simulationSelection.empty(), "DDOL transfer retained source selection");
            source->RetireManagedGraph();
            source.reset();
            domain.collect_full();
            Require(weakSecond.lock() && results.reclaimed == 0,
                "Detached transfer root did not retain its traced component cycle");
            destination->AttachExistingEntityHierarchy(transfers);
            for (const auto& component : second->m_components)
            {
                if (component)
                {
                    destination->RegisterComponent(component.get());
                }
            }
            Require(transfers.empty() && second->GetScene() == destination.get(),
                "DDOL roots were not handed off to the destination graph");
            Require(destination->Resolve(staleSecond) == nullptr
                && ScriptObjectRegistry::Get().Resolve(secondScript) == second,
                "Scene scoping or persistent script identity changed during transfer");
            second->Destroy();
            destination->EndFramePass();
            Require(results.uninitialized == 2 && results.ended == 2 && results.removed == 3
                && results.handlesValidInCleanup, "Cleanup callbacks were missing or duplicated");
            destination->RetireManagedGraph();
            destination.reset();
            domain.request_collection();
            bool incrementalFinished = false;
            std::size_t incrementalSteps = 0;
            for (; incrementalSteps < 4096; ++incrementalSteps)
            {
                const auto step = domain.collect_step(
                    gc::step_budget{ std::chrono::microseconds(250), 16 });
                Require(!step.refused, "Incremental collection refused the owner-thread boundary");
                if (step.cycle_finished)
                {
                    incrementalFinished = true;
                    ++incrementalSteps;
                    break;
                }
            }
            Require(incrementalFinished && domain.current_phase() == gc::phase::idle,
                "Incremental collection did not finish within the step limit");
            Require(results.reclaimed == 2 && !weakFirst.lock() && !weakSecond.lock()
                && !weakFirstProbe.lock() && !weakSecondProbe.lock()
                && !weakSource.lock() && !weakDestination.lock(),
                "Unreachable Scene/Entity/Component cycles were not reclaimed");
            std::vector<Runtime::EntityChange> firstEvents, secondEvents;
            for(const auto& event:observation.recorder->entities)
            {
                if(event.instance==firstIdentity)firstEvents.push_back(event.change);
                if(event.instance==secondIdentity)secondEvents.push_back(event.change);
                if(event.instance==secondIdentity&&event.change==Runtime::EntityChange::Attached)
                    Require(event.before.scene==staleSecond.sceneId&&event.after.scene!=event.before.scene,"DDOL location was not rebound");
            }
            Require(firstEvents==std::vector{Runtime::EntityChange::Created,Runtime::EntityChange::Destroyed},"Entity create/destroy observation order incorrect");
            Require(secondEvents==std::vector{Runtime::EntityChange::Created,Runtime::EntityChange::Detached,Runtime::EntityChange::Attached,Runtime::EntityChange::Destroyed},"DDOL observation duplicated destruction or identity");
            Require(SceneManagers->Observations().Metrics().lost==0&&SceneManagers->Observations().Metrics().failures==0,"Observer delivery lost/failed");
            std::printf("SCENE_OBSERVATION_PRODUCT_OK scalar=1 enum=1 container=1 locked=1 undoRedo=1 noOp=1 staleUndo=1 native=1 transform=1 nonRender=1 lifetime=1 ddol=1 abandoned=1\n");
            const auto final = domain.stats();
            Require(final.live_objects == baseline.live_objects && final.quarantined == baseline.quarantined,
                "Regression leaked graph objects or unfinished cleanup obligations");
            Require(final.cycles_aborted == baseline.cycles_aborted,
                "Collection aborted during the graph regression");
            std::printf("SCENE_RENDER_REGISTRY_OK types=9 duplicate=9 ownerless=9 retiredGenerations=18 lightCompactions=33 invalidLightSlots=3\n");
            log = "SCENE_GC_OK cycle=2 lifecycle=2 staleHandles=2 selection=2 ddol=1 workerPin=1 incrementalSteps="
                + std::to_string(incrementalSteps);
            return true;
        }
        catch (const std::exception& error)
        {
            // Roots are retained through failure cleanup as well. Reattach any
            // in-flight transfers before retiring their destination graph.
            if (destination && !transfers.empty())
            {
                destination->AttachExistingEntityHierarchy(transfers);
                for (const auto& entity : destination->m_Entities)
                {
                    if (entity)
                    {
                        for (const auto& component : entity->m_components)
                        {
                            if (component)
                            {
                                destination->RegisterComponent(component.get());
                            }
                        }
                    }
                }
            }
            if (source)
            {
                source->RetireManagedGraph();
                source.reset();
            }
            if (destination)
            {
                destination->RetireManagedGraph();
                destination.reset();
            }
            transfers.clear();
            domain.collect_full();
            log = std::string("SCENE_GC_FAILED ") + error.what();
            return false;
        }
    }
}
