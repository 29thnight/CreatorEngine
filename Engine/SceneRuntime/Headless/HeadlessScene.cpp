#if defined(CE_CONTRACT_STANDALONE)
#include "../../../Tools/build/ModuleAdapters/Creator.Core.Ownership.h"
#else
import Creator.Core.Ownership;
#endif
#if defined(CE_CONTRACT_STANDALONE)
#include "../../../Tools/build/ModuleAdapters/Creator.Core.Paths.h"
#else
import Creator.Core.Paths;
#endif
#include "HeadlessScene.h"
#include "../CookedSceneLoader.h"
#include "../ClrHost.h"
#include "../ScriptComponent.h"
#include "../ProjectLayerSettings.h"
#include "../../Utility_Framework/Time/TimeSystem.h"

namespace Runtime::Headless
{
    Session::Session() : manager_(*SceneManagers)
    {
        manager_.ManagerInitialize();
        layers_ = own::make_shared<ProjectLayerSettings>();
        if (!manager_.BindProjectLayerSettings(layers_))
            throw std::runtime_error("Project layer settings could not be bound");
        if (!manager_.ConfigureSimulationSession(SimulationSessionPolicy::mode::runtime))
            throw std::logic_error("Simulation session is already active");
    }

    Session::~Session()
    {
        try { Stop(); }
        catch (...) { /* explicit Stop reports failure after retiring owners */ }
        ClrHost::Get().Shutdown();
    }

    void Session::CheckOwner() const
    {
        if (owner_ != std::this_thread::get_id())
            throw std::logic_error("Scene execution requires the owner thread");
    }

    void Session::Load(const std::filesystem::path& cooked)
    {
        CheckOwner();
        if (scene_ || started_) throw std::logic_error("Load requires an empty session");
        auto candidate = ::Scene::LoadScene(manager_.ManagedDomain(), cooked.stem().string());
        scene_ = manager_.AdoptScene(candidate);
        stopped_ = false;
        CookedSceneLoader::Load(*scene_, cooked);
        manager_.ActivateScene(scene_);
        manager_.ApplyPendingSceneStructureChange();
        manager_.Observations().Drain();
    }

    void Session::Start(const std::filesystem::path& managed)
    {
        CheckOwner();
        if (!scene_ || started_ || stopped_) throw std::logic_error("Scene is not ready to start");
        const auto executable = ce::paths::resolve_process_executable_directory();
        ce::paths::roots paths;
        paths.executable_root = executable;
        paths.project_root = executable;
        paths.runtime_content_root = executable;
        paths.runtime_data_root = executable / "RuntimeData";
        paths.assets_root = executable / "Assets";
        paths.managed_root = std::filesystem::absolute(managed);
        if (!ce::paths::initialize(paths)) throw std::runtime_error("Host paths could not be bound");

        auto& clr = ClrHost::Get();
        if (!clr.Initialize() || !clr.AreScriptsLoaded()) throw std::runtime_error("F# runtime could not be loaded");
        // Existing ScriptComponent applies authored fields and owns all managed
        // instances. Scene's pending queue remains the only lifecycle driver.
        manager_.SetGameStart(true);
        manager_.ApplyPendingSceneStructureChange();
        if (!manager_.IsPlayCommitted()) throw std::runtime_error(manager_.LastPlayFailure());
        manager_.Initialization();
        clr.FlushRegistrations();
        if (ManagedErrors()) throw std::runtime_error("Managed startup failed");
        started_ = true;
        const auto* physics = scene_->PhysicsSimulation().Runtime();
        cpu_ = physics && physics->status().backend == ce::physics::execution_backend::cpu;
        if (!cpu_) throw std::runtime_error("Renderless host requires CPU physics");
    }

    void Session::Frame(double elapsed)
    {
        CheckOwner();
        if (!started_ || stopped_ || !std::isfinite(elapsed) || elapsed < 0)
            throw std::logic_error("Invalid renderless frame");
        Time->Tick([] {}); // Same frame ordinal service as Editor/Player.
        const auto before = manager_.SimulationClock().committed;
        auto& clr = ClrHost::Get();
        clr.TickPrePhysics(static_cast<float>(elapsed));
        manager_.Physics(static_cast<float>(elapsed));
        committed_ = manager_.SimulationClock().committed;
        dropped_ = manager_.SimulationClock().dropped;
        physicsSteps_ = scene_->PhysicsSimulation().CompletedSteps();
        if (manager_.PlayFailureCount() || ManagedErrors())
            throw std::runtime_error("Scene fixed tick failed: " + manager_.LastPlayFailure());
        clr.TickPostPhysics(static_cast<float>(elapsed));
        scene_->EndFramePass();
        manager_.EndOfFrame();
        manager_.CollectManagedAtFrameBoundary();
        ++frames_;
        if (before == committed_) ++zeroFrames_;
        if (committed_ > before + 1) ++multiFrames_;
    }

    void Session::CaptureEntities()
    {
        if (!scene_) return;
        entityReport_.clear();
        scene_->AllUpdateWorldMatrix(TransformSyncPoint::SceneLoad);
        for (const auto& entity : scene_->m_Entities)
            if (entity && !entity->IsDestroyMark())
                entityReport_.push_back({entity->GetName(), entity->Transform_().GetWorldPosition()});
    }

    void Session::Stop()
    {
        CheckOwner();
        if (stopped_) return;
        CaptureEntities();
        manager_.SetGameStart(false);
        manager_.ApplyPendingSceneStructureChange();
        // This is the normal shutdown path, including DDOL, final hooks,
        // ScriptObjectRegistry invalidation, graph retirement, and GCCE sweep.
        manager_.Decommissioning();
        scene_ = nullptr;
        stopped_ = true;
    }

    std::uint64_t Session::ManagedErrors() const noexcept
    {
        return ClrHost::Get().ManagedErrorCount();
    }

    std::string Session::Inspect(std::string_view type, std::string_view method)
    {
        CheckOwner();
        ClrHost::UserCodeScope permit;
        const auto result = ClrHost::Get().InvokeCallableStatic(std::string(type), std::string(method), {});
        if (!result.Succeeded()) throw std::runtime_error(result.reason);
        return result.returnValue;
    }

    void Session::WriteReport(Diagnostics::Node root) const
    {
        root.SetMap();
        root["implementation"] << "Scene/Entity/ScriptComponent/SceneManager";
        root["frames"] << frames_;
        root["engineFrameCount"] << static_cast<std::uint64_t>(Time->GetFrameCount());
        root["committedTicks"] << committed_;
        root["managedBatches"] << ClrHost::Get().SimulationBatchCount();
        root["physicsSteps"] << physicsSteps_;
        root["zeroTickFrames"] << zeroFrames_;
        root["multiTickFrames"] << multiFrames_;
        root["droppedSeconds"] << dropped_;
        root["cpuPhysics"] << cpu_;
        root["stopped"] << stopped_;
        root["ddolTransferred"] << transferred_;
        root["managedErrors"] << ManagedErrors();
        const auto gc = manager_.ManagedStatistics();
        root["liveObjects"] << static_cast<std::uint64_t>(gc.live_objects);
        root["roots"] << static_cast<std::uint64_t>(gc.roots);
        root["liveScriptHandles"] << static_cast<std::uint64_t>(ScriptObjectRegistry::Get().LiveCount());
        root["entities"].SetSequence();
        for (const auto& entity : entityReport_)
        {
            auto item = root["entities"].Append(); item.SetMap();
            item["name"] << entity.name;
            item["x"] << entity.position.x;
            item["y"] << entity.position.y;
            item["z"] << entity.position.z;
            item["alive"] << false;
        }
    }
}
