#include "Tasks/SceneGCSelfTest.h"
#include "Scene.h"
#include "SceneManager.h"
#include "Component.h"
#include "Entity.h"
#include "LifecycleRegistry.h"
#include "ScriptObjectRegistry.h"
#include "JobScheduler.h"

#include <algorithm>
#include <atomic>
#include <future>
#include <memory>
#include <stdexcept>
#include <utility>
#include <vector>

namespace
{
    void Require(bool condition, const char* message)
    {
        if (!condition)
        {
            throw std::runtime_error(message);
        }
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
            domain.collect_full();
            const auto baseline = domain.stats();
            Lifecycle::Registry::Register<CycleProbe>();
            source = Scene::CreateNewScene(domain, "SceneGCSource");
            destination = Scene::CreateNewScene(domain, "SceneGCDestination");

            Entity* first = source->CreateEntity("CycleFirst");
            Entity* second = source->CreateEntity("CycleSecond");
            auto* firstProbe = first->AddComponent<CycleProbe>(results);
            auto* secondProbe = second->AddComponent<CycleProbe>(results);
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
            domain.collect_full();
            Require(results.reclaimed == 2 && !weakFirst.lock() && !weakSecond.lock()
                && !weakFirstProbe.lock() && !weakSecondProbe.lock()
                && !weakSource.lock() && !weakDestination.lock(),
                "Unreachable Scene/Entity/Component cycles were not reclaimed");
            const auto final = domain.stats();
            Require(final.live_objects == baseline.live_objects && final.quarantined == baseline.quarantined,
                "Regression leaked graph objects or unfinished cleanup obligations");
            log = "SCENE_GC_OK cycle=2 lifecycle=2 staleHandles=2 selection=2 ddol=1 workerPin=1";
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
