#pragma once
#if defined(CE_CONTRACT_STANDALONE)
#include "../../../Tools/build/ModuleAdapters/Creator.Core.Ownership.h"
#else
import Creator.Core.Ownership;
#endif

#include "../SceneManager.h"
#include "../Scene.h"
#include "../../Utility_Framework/Authoring/DiagnosticDocument.h"

namespace Runtime::Headless
{
    // Host orchestration only. SceneManager owns the ordinary Scene/Entity graph,
    // component lifecycle, physics bindings, command exchange, and GC domain.
    class Session final
    {
    public:
        Session();
        ~Session();
        Session(const Session&) = delete;
        Session& operator=(const Session&) = delete;

        void Load(const std::filesystem::path& cookedScene);
        void Start(const std::filesystem::path& managedDirectory);
        void Frame(double elapsed);
        void Stop();
        void TransferPersistentEntity(std::string_view name);
        std::string Inspect(std::string_view type, std::string_view method);
        void WriteReport(Diagnostics::Node output) const;

        std::uint64_t Committed() const noexcept { return committed_; }
        std::uint64_t ManagedErrors() const noexcept;
        SceneObservation& Observations() noexcept { return manager_.Observations(); }

    private:
        void CaptureEntities();
        void CheckOwner() const;

        SceneManager& manager_;
        own::shared_owner<ProjectLayerSettings> layers_;
        ::Scene* scene_ = nullptr; // borrowed; manager owns its GC root
        std::thread::id owner_ = std::this_thread::get_id();
        struct EntityReport { std::string name; math::vector3 position; };
        std::vector<EntityReport> entityReport_;
        std::uint64_t frames_ = 0, committed_ = 0, zeroFrames_ = 0, multiFrames_ = 0;
        double dropped_ = 0;
        std::uint64_t physicsSteps_ = 0;
        bool cpu_ = false;
        bool transferred_ = false;
        bool started_ = false, stopped_ = true;
    };
}
