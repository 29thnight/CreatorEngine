// Full production selection executes below. Native program work and the view's
// unrelated geometry/camera fields are reduced; ownership, containers, pin table,
// readiness/coverage decisions and revision-publication operations are real.
#include "Engine/Utility_Framework/Foundation/EngineOrderedMap.h"
#include "Tools/build/ModuleAdapters/Creator.Core.Ownership.h"

namespace experiment
{
    using AssetId = std::uint64_t;
}

enum class RHIShaderBinary { Dxil, Spirv };
struct RHIShaderCompiler
{
    static inline RHIShaderBinary output = RHIShaderBinary::Dxil;
    static RHIShaderBinary GetOutput() { return output; }
};

struct EnhancedFrameContext
{
    std::uint64_t frameId{1}, sceneEpoch{1};
    std::uint32_t width{640}, height{480};
};
#include "SceneSelectionCoverageData.inl"

namespace material_graph
{
    struct Generation
    {
        experiment::AssetId assetId{};
        std::uint64_t generation{1};
        struct
        {
            struct
            {
                struct
                {
                    std::uint64_t semanticKey{};
                    bool surface{true}, volume{};
                } program;
            } product;
        } cooked;
    };
    struct Instance
    {
        own::shared_owner<const Generation> generation;
        std::uint64_t representationId{};
        bool editorPreview{};
    };
#include "SceneSelectionPins.inl"
#include "SceneSelectionCoverageEnum.inl"
#include "SceneSelectionIdentity.inl"

    namespace coverage_contract
    {
#include "SceneSelectionCoverage.inl"
    }
    unsigned coverageCalls{};
    bool ClassifySceneCoverage(const EnhancedMaterialCoverage& coverage, SceneCoverage& result, std::string& error)
    {
        ++coverageCalls;
        return coverage_contract::ClassifySceneCoverage(coverage, result, error);
    }

    // Counts calls at the real append-only table boundary; it does not replace
    // owner retention, table identity, lock, or borrow behavior.
    struct CountedPins
    {
        InstanceFramePins pins;
        unsigned retainCalls{}, borrowCalls{};
        mutable unsigned ownerCalls{};
        std::size_t Retain(const own::shared_owner<const Instance>& instance)
        {
            ++retainCalls;
            return pins.Retain(instance);
        }
        own::local_view<const Instance> Borrow(std::size_t index)
        {
            ++borrowCalls;
            return pins.Borrow(index);
        }
        own::shared_owner<const Instance> Owner(std::size_t index) const
        {
            ++ownerCalls;
            return pins.Owner(index);
        }
        void ResetCalls() { retainCalls = borrowCalls = ownerCalls = 0; }
    };

    struct SceneInputView
    {
        std::uint64_t frameId{1}, sceneEpoch{1}, viewId{1};
        std::uint32_t width{640}, height{480};
    };
    struct SceneDrawInput
    {
        std::uint64_t materialSlot{}, selectionRevision{};
        own::local_view<const Instance> material;
        std::size_t materialPinIndex{InstanceFramePins::InvalidIndex};
        EnhancedMaterialCoverage coverage;
        SceneCoverage queue{};
    };
    struct SceneViewInput
    {
        struct ConstructionKey {};
        static inline unsigned copies{};
        SceneInputView view_;
        ce::dynamic_array<SceneDrawInput> draws_;
        own::shared_owner<CountedPins> materialPins_ = own::make_shared<CountedPins>();
        SceneViewInput() = default;
        SceneViewInput(ConstructionKey, const SceneViewInput& source)
            : view_(source.view_), draws_(source.draws_), materialPins_(source.materialPins_)
        {
            ++copies;
        }
        const SceneInputView& View() const { return view_; }
        std::span<const SceneDrawInput> Draws() const { return draws_; }
        own::shared_owner<const Instance> MaterialOwner(const SceneDrawInput& draw) const
        {
            return materialPins_->Owner(draw.materialPinIndex);
        }
    };

    constexpr char PreparationAdmissionError[] =
        "METEOR Scene program preparation exceeds its 64-request admission budget.";
    bool Fail(std::string& error, std::string message)
    {
        error = std::move(message);
        return false;
    }

    struct SceneHost
    {
        struct Slot;
        struct Program
        {
            own::shared_owner<const Generation> generation;
            RHIShaderBinary backend = RHIShaderBinary::Dxil;
            ce::dynamic_array<int> meshPipelines;
        };
        struct Preparation
        {
            struct Work { bool cooked{}; };
            own::shared_owner<const Generation> generation;
            RHIShaderBinary backend = RHIShaderBinary::Dxil;
            std::string error;
            bool checked{};
            own::shared_owner<Work> work;
            own::shared_owner<Program> program;
        };
        struct Stats { unsigned publications{}, stalePublications{}; } stats_;
        using SlotKey = ce::tuple<std::uint64_t, std::uint64_t, std::uint64_t>;
        ce::engine_ordered_map<SlotKey, own::unique_owner<Slot>> slots_;
        ce::dynamic_array<own::shared_owner<Program>> programs_, completeOnPoll;
        ce::dynamic_array<own::shared_owner<Preparation>> preparations_;
        ce::engine_ordered_map<experiment::AssetId, std::string> requestErrors;
        std::uint64_t selectionSerial_{};
        bool selectionDeferred_{};
        mutable unsigned readyCalls{};
        unsigned requestCalls{}, pollCalls{}, submittedPollCalls{}, pruneCalls{};
        SceneHost() = default;
        ~SceneHost();
        bool SelectReadyInput(const EnhancedFrameContext& context,
                              own::shared_owner<const SceneViewInput> requested,
                              own::shared_owner<const SceneViewInput>& result, std::string& error);
        bool IsProgramReady(const own::shared_owner<const Generation>& generation, RHIShaderBinary backend) const
        {
            ++readyCalls;
#include "SceneSelectionReady.inl"
        }
        // Native request/cache work is outside this fixture. readyCalls measures
        // SelectReadyInput's own decisions after its single PollPrograms call.
        bool RequestProgram(const EnhancedFrameContext&, const own::shared_owner<const Generation>& generation,
                            std::string& error)
        {
            ++requestCalls;
            const auto failure = requestErrors.find(generation->assetId);
            error = failure == requestErrors.end() ? std::string{} : failure->second;
            return error.empty();
        }
        void PollPrograms(const EnhancedFrameContext&)
        {
            ++pollCalls;
            for (const auto& program : completeOnPoll) programs_.push_back(program);
            completeOnPoll.clear();
        }
        void PollSubmittedFrames() { ++submittedPollCalls; }
        void PruneFailedPreparations() { ++pruneCalls; }
        void ResetCalls()
        {
            readyCalls = requestCalls = pollCalls = submittedPollCalls = pruneCalls = 0;
            coverageCalls = SceneViewInput::copies = 0;
        }
        void Ready(const own::shared_owner<const Generation>& generation, RHIShaderBinary backend = RHIShaderBinary::Dxil)
        {
            programs_.push_back(own::make_shared<Program>(Program{generation, backend, {}}));
        }
        void Failure(const own::shared_owner<const Generation>& generation, std::string error)
        {
            auto item = own::make_shared<Preparation>();
            item->generation = generation;
            item->error = std::move(error);
            preparations_.push_back(std::move(item));
        }
        void CommitSelected(const own::shared_owner<const SceneViewInput>& input);
        Slot& At(const SceneInputView& view, std::uint64_t slot);
    };
#include "SceneSelectionSlot.inl"
    SceneHost::~SceneHost() = default;
    SceneHost::Slot& SceneHost::At(const SceneInputView& view, std::uint64_t slot)
    {
        return *slots_.at({view.sceneEpoch, view.viewId, slot});
    }
    void SceneHost::CommitSelected(const own::shared_owner<const SceneViewInput>& input)
    {
        struct Frame { own::shared_owner<const SceneViewInput> input; } value{input};
        const auto* frame = &value;
        const auto& view = input->View();
#include "SceneSelectionPublication.inl"
    }
#include "SceneSelection.inl"
}

namespace
{
    using namespace material_graph;
    unsigned checks{};
    void Check(bool condition, const char* message)
    {
        if (!condition) throw std::runtime_error(message);
        ++checks;
    }
    EnhancedMaterialCoverage Coverage(std::uint32_t extra = 0)
    {
        return {EnhancedMaterialCoverage::Enabled | extra, 0.5f, 1.f};
    }
    own::shared_owner<const Instance> Material(std::uint64_t asset, std::uint64_t representation = 0,
                                               bool preview = false)
    {
        auto generation = own::make_shared<Generation>();
        generation->assetId = asset;
        generation->cooked.product.program.semanticKey = asset * 17;
        return own::make_shared<const Instance>(Instance{generation, representation ? representation : asset * 100 + 1, preview});
    }
    void Add(SceneViewInput& input, std::uint64_t slot, const own::shared_owner<const Instance>& material,
             unsigned count = 1, EnhancedMaterialCoverage coverage = Coverage())
    {
        const auto index = input.materialPins_->Retain(material);
        const auto borrow = input.materialPins_->Borrow(index);
        SceneCoverage queue{};
        std::string error;
        Check(coverage_contract::ClassifySceneCoverage(coverage, queue, error), "Fixture coverage is invalid");
        for (unsigned draw = 0; draw < count; ++draw)
            input.draws_.push_back({slot, 0, borrow, index, coverage, queue});
    }
    auto Input(std::uint64_t slot, const own::shared_owner<const Instance>& material, unsigned count = 1,
               EnhancedMaterialCoverage coverage = Coverage())
    {
        auto input = own::make_shared<SceneViewInput>();
        Add(*input, slot, material, count, coverage);
        return input;
    }
    void Reset(SceneHost& host, const own::shared_owner<SceneViewInput>& input)
    {
        host.ResetCalls();
        input->materialPins_->ResetCalls();
    }
    bool SameCoverage(const EnhancedMaterialCoverage& a, const EnhancedMaterialCoverage& b)
    {
        return a.flags == b.flags && a.cutoff == b.cutoff && a.baseAlpha == b.baseAlpha;
    }

    struct WorkCounts
    {
        unsigned readyQueries{}, previewQueries{}, previewRetains{}, previewBorrows{}, previewCoverage{};
    };

    void DuplicateValidation()
    {
        EnhancedFrameContext context;
        SceneHost host;
        auto original = Material(1);
        auto alternate = Material(2);
        host.Ready(original->generation);
        auto initial = Input(9, original);
        own::shared_owner<const SceneViewInput> selected;
        std::string error;
        Check(host.SelectReadyInput(context, initial, selected, error), "Initial ready selection failed");
        const auto revision = host.At(initial->View(), 9).revision;
        for (unsigned mismatch = 0; mismatch < 5; ++mismatch)
        {
            auto invalid = Input(9, original, 2);
            if (mismatch == 0) invalid->draws_[1].material = alternate.borrow();
            if (mismatch == 1) invalid->draws_[1].coverage.flags |= EnhancedMaterialCoverage::Masked;
            if (mismatch == 2) invalid->draws_[1].coverage.cutoff = 0.25f;
            if (mismatch == 3) invalid->draws_[1].coverage.baseAlpha = 0.25f;
            auto impostor = own::make_shared<const Instance>(*original);
            if (mismatch == 4) invalid->draws_[1].material = impostor.borrow();
            auto result = selected;
            Reset(host, invalid);
            Check(!host.SelectReadyInput(context, invalid, result, error), "Conflicting shared slot was accepted");
            Check(&*result == &*selected && !host.selectionDeferred_, "Invalid slot changed last-good result");
            Check(host.At(initial->View(), 9).revision == revision && host.requestCalls == 0 &&
                  host.pollCalls == 0 && SceneViewInput::copies == 0,
                  "Shared-slot validation mutated selection before rejecting");
        }
        auto zeroSlot = Input(0, original);
        auto result = selected;
        Reset(host, zeroSlot);
        Check(!host.SelectReadyInput(context, zeroSlot, result, error) && host.requestCalls == 0,
              "Zero material slot changed request state");
    }

    void ReadySelections(WorkCounts& work)
    {
        EnhancedFrameContext context;
        SceneHost host;
        auto first = Material(3), second = Material(4);
        host.Ready(first->generation);
        host.Ready(second->generation);
        auto input = Input(9, first, 96);
        Add(*input, 3, second, 64, Coverage(EnhancedMaterialCoverage::Masked));
        own::shared_owner<const SceneViewInput> selected;
        std::string error;
        Reset(host, input);
        Check(host.SelectReadyInput(context, input, selected, error) && error.empty(), "Ready shared-slot selection failed");
        work.readyQueries = host.readyCalls;
        Check(host.requestCalls == 2 && host.pollCalls == 1 && SceneViewInput::copies == 1,
              "Selection must request unique slots, poll once and copy revisions once");
        Check(input->materialPins_->retainCalls == 0 && input->materialPins_->borrowCalls == 0,
              "Ready selection redundantly touched material pins");
        Check(&*selected != &*input && selected->Draws().size() == 160, "Ready selection dropped its revision copy");
        const auto firstRevision = selected->Draws()[0].selectionRevision;
        const auto secondRevision = selected->Draws()[96].selectionRevision;
        Check(firstRevision != 0 && firstRevision < secondRevision,
              "Representation revision order no longer follows first draw occurrence");
        for (std::size_t index = 0; index < input->Draws().size(); ++index)
        {
            const auto& source = input->Draws()[index];
            const auto& draw = selected->Draws()[index];
            Check(source.selectionRevision == 0 && draw.selectionRevision == (index < 96 ? firstRevision : secondRevision),
                  "Selection mutated sealed input or assigned inconsistent shared-slot revisions");
            Check(&*source.material == &*draw.material && SameCoverage(source.coverage, draw.coverage) && source.queue == draw.queue,
                  "Ready material selection changed identity or coverage");
        }
        own::shared_owner<const SceneViewInput> repeated;
        Check(host.SelectReadyInput(context, input, repeated, error), "Repeated selection failed");
        Check(repeated->Draws()[0].selectionRevision == firstRevision &&
              repeated->Draws()[96].selectionRevision == secondRevision, "Unchanged selection churned revisions");
        host.CommitSelected(input);
        Check(host.stats_.publications == 0, "Unselected revision-zero input published an active material");
        host.CommitSelected(selected);
        Check(host.stats_.publications == 160 && &*host.At(input->View(), 9).active == &*first,
              "Selected revision failed active-material publication");

        SceneHost completing;
        completing.completeOnPoll.push_back(own::make_shared<SceneHost::Program>(
            SceneHost::Program{first->generation, RHIShaderBinary::Dxil, {}}));
        auto pending = Input(9, first);
        Check(completing.SelectReadyInput(context, pending, repeated, error),
              "Readiness was decided before program polling completed");
    }

    void DeferredFailuresAndOrder()
    {
        EnhancedFrameContext context;
        auto pending = Material(5), failed = Material(6);
        own::shared_owner<const SceneViewInput> lastGood = Input(1, Material(7));
        std::string error;
        for (bool pendingFirst : {true, false})
        {
            SceneHost host;
            host.Failure(failed->generation, "fixture native failure");
            auto input = Input(pendingFirst ? 9 : 3, pendingFirst ? pending : failed, 5);
            Add(*input, pendingFirst ? 3 : 9, pendingFirst ? failed : pending, 5);
            auto result = lastGood;
            Reset(host, input);
            Check(!host.SelectReadyInput(context, input, result, error) && &*result == &*lastGood,
                  "Pending/failed selection replaced the last-good result");
            Check(host.selectionDeferred_ == pendingFirst,
                  "Mixed pending/failure outcome no longer follows first draw occurrence");
            Check(pendingFirst ? error.find("still preparing") != std::string::npos : error == "fixture native failure",
                  "Mixed pending/failure error priority changed");
            Check(SceneViewInput::copies == 0, "Rejected selection copied the full input");
        }
        for (bool admission : {true, false})
        {
            SceneHost host;
            host.requestErrors[pending->generation->assetId] = admission ? PreparationAdmissionError : "fixture request failure";
            auto input = Input(9, pending, 8);
            auto result = lastGood;
            Reset(host, input);
            Check(!host.SelectReadyInput(context, input, result, error) && &*result == &*lastGood &&
                  host.selectionDeferred_ == admission && host.pollCalls == 0,
                  "Request admission/failure semantics changed");
        }
        auto unsupportedGeneration = own::make_shared<Generation>(*pending->generation);
        unsupportedGeneration->cooked.product.program.surface = false;
        auto unsupported = own::make_shared<const Instance>(Instance{unsupportedGeneration, 1, false});
        SceneHost host;
        auto input = Input(9, unsupported);
        auto result = lastGood;
        Check(!host.SelectReadyInput(context, input, result, error) && host.requestCalls == 0 && !host.selectionDeferred_,
              "Unsupported material entered native preparation");
    }

    void PreviewAndPublication(WorkCounts& work)
    {
        EnhancedFrameContext context;
        SceneHost host;
        auto preview = Material(8, 1, true), replacement = Material(9);
        const auto previewCoverage = Coverage(EnhancedMaterialCoverage::Masked | EnhancedMaterialCoverage::DoubleSided);
        auto initial = Input(9, preview, 1, previewCoverage);
        host.Ready(preview->generation);
        own::shared_owner<const SceneViewInput> selected;
        std::string error;
        Check(host.SelectReadyInput(context, initial, selected, error), "Ready editor preview could not be selected");
        host.CommitSelected(selected);
        auto input = Input(9, replacement, 80);
        Reset(host, input);
        Check(host.SelectReadyInput(context, input, selected, error) && !host.selectionDeferred_,
              "Ready active editor preview did not cover a pending request");
        work.previewQueries = host.readyCalls;
        work.previewRetains = input->materialPins_->retainCalls;
        work.previewBorrows = input->materialPins_->borrowCalls;
        work.previewCoverage = coverageCalls;
        Check(host.requestCalls == 1 && SceneViewInput::copies == 1 && input->materialPins_->pins.Size() == 2,
              "Preview selection changed pin ownership or request batching");
        const auto fallbackRevision = selected->Draws()[0].selectionRevision;
        for (const auto& draw : selected->Draws())
        {
            Check(&*draw.material == &*preview && SameCoverage(draw.coverage, previewCoverage) &&
                  draw.queue == SceneCoverage::Masked && draw.selectionRevision == fallbackRevision,
                  "Preview scatter changed identity, coverage, queue or shared revision");
            Check(&*selected->MaterialOwner(draw) == &*preview, "Preview borrow is not anchored by its selected pin");
        }
        for (const auto& draw : input->Draws())
            Check(&*draw.material == &*replacement && draw.selectionRevision == 0 && draw.queue == SceneCoverage::Opaque,
                  "Preview selection mutated the requested immutable draws");
        auto fallback = selected;
        host.Ready(replacement->generation);
        Reset(host, input);
        Check(host.SelectReadyInput(context, input, selected, error) && &*selected->Draws()[0].material == &*replacement &&
              selected->Draws()[0].selectionRevision > fallbackRevision, "Ready replacement did not supersede preview");
        Check(input->materialPins_->retainCalls == 0 && input->materialPins_->borrowCalls == 0,
              "Ready replacement performed preview pin work");
        host.CommitSelected(selected);
        const auto staleBefore = host.stats_.stalePublications;
        host.CommitSelected(fallback);
        Check(host.stats_.stalePublications - staleBefore == 80 && &*host.At(input->View(), 9).active == &*replacement,
              "Late preview submission overwrote a newer selected material");

        // An ordinary active material never gets the editor-preview exemption.
        auto another = Input(9, Material(10), 4);
        auto result = selected;
        Check(!host.SelectReadyInput(context, another, result, error) && host.selectionDeferred_ && &*result == &*selected,
              "Non-preview active material silently replaced a pending source");
        host.Failure(another->Draws()[0].material->generation, "replacement failure");
        Check(!host.SelectReadyInput(context, another, result, error) && !host.selectionDeferred_ && error == "replacement failure",
              "Failed source with non-preview active material did not reject");

        SceneHost noReadyPreview;
        auto seeded = Input(9, preview);
        noReadyPreview.Ready(preview->generation);
        Check(noReadyPreview.SelectReadyInput(context, seeded, result, error), "Preview seed selection failed");
        noReadyPreview.CommitSelected(result);
        noReadyPreview.programs_.clear();
        auto unavailable = Input(9, replacement);
        auto previous = result;
        Check(!noReadyPreview.SelectReadyInput(context, unavailable, result, error) && &*result == &*previous &&
              noReadyPreview.selectionDeferred_, "Unready preview program was used for fallback");
        noReadyPreview.Ready(preview->generation);
        noReadyPreview.At(seeded->View(), 9).activeCoverage.flags = 0;
        Check(!noReadyPreview.SelectReadyInput(context, unavailable, result, error) && &*result == &*previous &&
              error == "Scene material needs a valid enabled coverage policy.", "Invalid preview coverage was accepted");
    }

    void ViewAndGenerationBoundaries()
    {
        EnhancedFrameContext context;
        SceneHost host;
        auto material = Material(11);
        host.Ready(material->generation);
        auto input = Input(9, material);
        own::shared_owner<const SceneViewInput> selected;
        std::string error;
        Check(host.SelectReadyInput(context, input, selected, error), "Boundary seed failed");
        auto otherView = Input(4, material);
        otherView->view_.viewId = 2;
        Check(host.SelectReadyInput(context, otherView, selected, error), "Other view seed failed");
        auto lastGood = selected;
        for (unsigned field = 0; field < 4; ++field)
        {
            auto invalid = Input(9, material);
            if (field == 0) ++invalid->view_.frameId;
            if (field == 1) ++invalid->view_.sceneEpoch;
            if (field == 2) ++invalid->view_.width;
            if (field == 3) ++invalid->view_.height;
            Reset(host, invalid);
            Check(!host.SelectReadyInput(context, invalid, selected, error) && &*selected == &*lastGood &&
                  host.slots_.size() == 2 && host.requestCalls == 0 && !host.selectionDeferred_,
                  "Foreign view context mutated slots or last-good result");
        }
        auto staleEmpty = own::make_shared<SceneViewInput>();
        ++staleEmpty->view_.sceneEpoch;
        Check(!host.SelectReadyInput(context, staleEmpty, selected, error) && host.slots_.size() == 2,
              "Stale empty view cleared current slots");
        auto newer = Input(12, material);
        ++newer->view_.sceneEpoch;
        ++context.sceneEpoch;
        Check(host.SelectReadyInput(context, newer, selected, error) && host.slots_.size() == 2 &&
              host.slots_.contains({2, 1, 12}) && host.slots_.contains({1, 2, 4}) && !host.slots_.contains({1, 1, 9}),
              "New epoch failed targeted slot cleanup");
        auto empty = own::make_shared<SceneViewInput>();
        empty->view_.sceneEpoch = context.sceneEpoch;
        Reset(host, empty);
        Check(host.SelectReadyInput(context, empty, selected, error) && &*selected == &*empty && host.slots_.size() == 1 &&
              host.slots_.contains({1, 2, 4}) && host.pollCalls == 1 && host.submittedPollCalls == 1,
              "Empty view cleanup crossed view ownership or skipped polling");
        Check(host.SelectReadyInput(context, {}, selected, error) && !selected && error.empty() && !host.selectionDeferred_,
              "Null input did not clear selection");

        for (unsigned mismatch = 0; mismatch < 4; ++mismatch)
        {
            SceneHost checking;
            auto generation = own::make_shared<Generation>(*material->generation);
            auto backend = RHIShaderBinary::Dxil;
            if (mismatch == 0) ++generation->assetId;
            if (mismatch == 1) ++generation->generation;
            if (mismatch == 2) ++generation->cooked.product.program.semanticKey;
            if (mismatch == 3) backend = RHIShaderBinary::Spirv;
            checking.Ready(generation, backend);
            auto result = lastGood;
            EnhancedFrameContext current;
            Check(!checking.SelectReadyInput(current, input, result, error) && checking.selectionDeferred_ && &*result == &*lastGood,
                  "Readiness bypassed exact generation/semantic/backend identity");
            checking.Ready(material->generation);
            Check(checking.SelectReadyInput(current, input, result, error), "Exact ready program did not resolve the request");
        }
    }
}

int main(int argc, char** argv)
{
    try
    {
        const bool behaviorOnly = argc == 2 && std::string_view(argv[1]) == "--behavior-only";
        WorkCounts work;
        DuplicateValidation();
        ReadySelections(work);
        DeferredFailuresAndOrder();
        PreviewAndPublication(work);
        ViewAndGenerationBoundaries();
        std::printf("scene selection behavior PASS checks=%u; selection_ready_queries=%u for 160 draws/2 slots; "
                    "selection_preview_queries=%u retains=%u borrows=%u coverage=%u for 80 draws/1 slot\n",
                    checks, work.readyQueries, work.previewQueries, work.previewRetains, work.previewBorrows, work.previewCoverage);
        if (!behaviorOnly)
        {
            Check(work.readyQueries == 2, "Ready selection still scans programs per draw instead of per slot");
            Check(work.previewQueries == 2 && work.previewRetains == 1 && work.previewBorrows == 1 && work.previewCoverage == 1,
                  "Preview selection still repeats program/pin/coverage work for a shared slot");
            std::printf("scene selection unique-slot work PASS\n");
        }
        return 0;
    }
    catch (const std::exception& error)
    {
        std::fprintf(stderr, "scene selection FAIL: %s\n", error.what());
        return 1;
    }
}
