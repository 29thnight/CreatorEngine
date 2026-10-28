#include "../../Engine/Effect/EffectState.h"
#include "../../Engine/RenderEngine/Effects/IEffectSystem.h"
#include <stdio.h>
#include <stdexcept>
#if defined(_DEBUG)
#include <crtdbg.h>
namespace {
thread_local bool observeStorage = false;
thread_local unsigned storageAllocations = 0, storageFrees = 0;
thread_local const void* observedStringBuffer = nullptr;
thread_local unsigned stringBufferFrees = 0;
int StorageHook(int kind, void* data, size_t, int, long, const unsigned char*, int) {
    if(observeStorage && (kind==_HOOK_ALLOC || kind==_HOOK_REALLOC)) ++storageAllocations;
    if(observeStorage && kind==_HOOK_FREE) ++storageFrees;
    if(observeStorage && kind==_HOOK_FREE && data==observedStringBuffer) ++stringBufferFrees;
    return 1;
}
struct StorageScope {
    _CRT_ALLOC_HOOK previous;
    explicit StorageScope(const void* buffer=nullptr):previous(_CrtSetAllocHook(StorageHook)) {
        storageAllocations=storageFrees=stringBufferFrees=0;observedStringBuffer=buffer;observeStorage=true;
    }
    ~StorageScope(){observeStorage=false;observedStringBuffer=nullptr;_CrtSetAllocHook(previous);}
};
}
#endif

namespace
{
class CpuFacade final : public ce::effects::IEffectSystem
{
public:
    bool Initialize(std::string&) override { return true; }
    void Shutdown() noexcept override { state.Shutdown(); }
    bool SetSceneEpoch(std::uint64_t epoch) override { return state.SetSceneEpoch(epoch); }
    bool CommitTick(const ce::effects::EffectTick& tick) override { return state.CommitTick(tick); }
    ce::effects::EffectFrameOwner CaptureFrame() const override { return state.CaptureFrame(); }
private:
    ce::effects::EffectState state;
};
std::unique_ptr<ce::effects::IEffectSystem> CreateCpuFacade() { return std::make_unique<CpuFacade>(); }
}

int main()
{
    unsigned checks = 0;
    const auto require = [&](bool value, const char* text) { ++checks; if (!value) throw std::runtime_error(text); };
    try {
        using namespace ce::effects;
        static_assert(std::is_same_v<decltype(EffectInstanceSnapshot::definitionId),std::string>);
        static_assert(sizeof(EffectTransform)==16*sizeof(float));
        {
            EffectFrame frame;
            frame.instances.reserve(2);
            frame.instances.emplace_back(); frame.instances.back().definitionId.assign(96,'a');
            frame.instances.emplace_back(); frame.instances.back().definitionId.assign(96,'b');
            const auto* storage=frame.instances.data();
            const auto capacity=frame.instances.capacity();
            bool readable=true;
#if defined(_DEBUG)
            unsigned allocations=0, erasedFrees=0, clearedFrees=0, eraseRaw=0, clearRaw=0;
            {
                StorageScope scope;
#endif
                for(unsigned i=0;i<4096;++i)
                    readable &= frame.instances[0].definitionId.size()==96 &&
                        frame.instances[1].definitionId.front()=='b' && frame.instances[1].transform.world[15]==1;
#if defined(_DEBUG)
                allocations=storageAllocations;
            }
            require(allocations==0,"effect frame indexed reads allocate zero on calling thread");
            {
                StorageScope scope(frame.instances[0].definitionId.data());
#endif
                frame.instances.erase(frame.instances.begin());
#if defined(_DEBUG)
                erasedFrees=stringBufferFrees; eraseRaw=storageFrees;
            }
            require(erasedFrees==1,"erase releases the removed owned string immediately");
#endif
            require(readable && frame.instances.size()==1 && frame.instances[0].definitionId.front()=='b',
                "array erase preserves survivor values and fixed identity matrix");
#if defined(_DEBUG)
            {
                StorageScope scope(frame.instances[0].definitionId.data());
#endif
                frame.instances.clear();
#if defined(_DEBUG)
                clearedFrees=stringBufferFrees; clearRaw=storageFrees;
            }
            require(clearedFrees==1,"clear releases remaining owned string immediately");
            std::printf("EFFECT_ARRAY_FREE_DIAGNOSTICS eraseTotal=%u clearTotal=%u erasedStringBuffer=%u clearedStringBuffer=%u\n",
                eraseRaw,clearRaw,erasedFrees,clearedFrees);
#endif
            require(frame.instances.empty() && frame.instances.data()==storage && frame.instances.capacity()==capacity,
                "clear retains reserved array storage while destroying values");
        }
        {
            EffectState producer;
            require(producer.SetSceneEpoch(91), "owned string snapshot epoch");
            const std::string id(96,'x');
            const auto instance=producer.RegisterInstance(id,{});
            auto packet=producer.CaptureFrame();
            own::weak_owner<const EffectFrame> lifetime(packet);
            require(instance.index && producer.UnregisterInstance(instance) &&
                producer.CaptureFrame()->instances.empty() && packet->instances[0].definitionId==id,
                "unregister leaves retained snapshot strings and arrays valid");
            producer.Shutdown();
            require(!lifetime.expired() && packet->instances[0].definitionId==id,
                "snapshot owns values after producer shutdown");
            packet.reset();
            require(lifetime.expired(),"last snapshot owner releases its frame");
        }
        EffectState state;
        require(!state.CaptureFrame(), "no unscoped publication");
        require(!state.RegisterInstance("probe",{}).index, "registration requires epoch");
        require(state.SetSceneEpoch(7), "epoch admission");
        require(!state.RegisterInstance("bad id",{}).index, "definition ID follows schema contract");
        auto first = state.RegisterInstance("probe",{});
        require(first.index == 1 && first.generation == 1, "fresh handle");
        require(state.Burst(first,2) && state.Burst(first,3), "burst commands accepted");
        auto retained = state.CaptureFrame();
        require(retained->instances[0].burstTotal == 5, "latest snapshot preserves every burst");
        EffectTransform moved; moved.world[12] = 10;
        require(state.UpdateInstance(first,moved), "transform update");
        require(retained->instances[0].transform.world[12] == 0, "prior packet immutable");
        require(state.CaptureFrame()->instances[0].transform.world[12] == 10, "new snapshot owns new value");
        require(state.CommitTick({7,1,1.0/60}), "first simulation tick");
        auto tickFrame = state.CaptureFrame();
        auto view1 = tickFrame, view2 = tickFrame;
        require(view1->tick == 1 && view2->tick == 1 && state.Counters()->ticks.load() == 1, "views do not advance simulation");
        require(!state.CommitTick({7,1,1.0/60}), "duplicate tick rejected");
        require(!state.CommitTick({7,3,1.0/60}), "tick gap rejected");
        require(!state.CommitTick({7,2,0}), "pause cannot submit tick");
        require(!state.CommitTick({7,2,std::numeric_limits<double>::quiet_NaN()}), "nonfinite tick rejected");
        require(state.CommitTick({7,2,1.0/60}), "next tick");
        const auto revision = state.CaptureFrame()->revision;
        require(state.SetSceneEpoch(7) && state.CaptureFrame()->revision == revision, "same epoch is not a reset");
        std::atomic<bool> readable{}, wrongThreadRejected{};
        std::thread reader([packet = view1, &state, &readable, &wrongThreadRejected] {
            readable = packet->tick == 1 && packet->instances[0].burstTotal == 5;
            try { state.Burst(packet->instances[0].handle,1); }
            catch (const std::logic_error&) { wrongThreadRejected = true; }
        });
        reader.join();
        require(readable && wrongThreadRejected, "RT reads owned packet but cannot mutate GT state");
        require(state.UnregisterInstance(first), "unregister");
        require(!state.UnregisterInstance(first) && !state.Burst(first,1), "dead handle rejected");
        auto second = state.RegisterInstance("probe",{});
        require(second.index == first.index && second.generation == first.generation + 1, "slot reuse increments generation");
        require(!state.UpdateInstance(first,{}) && state.Burst(second,4), "ABA guard");
        require(state.SetSceneEpoch(8), "scene change");
        auto epochToken = state.SceneEpochToken();
        require(epochToken->load() == 8 && epochToken->load() != view1->sceneEpoch, "owned epoch token rejects old queued frame");
        require(!state.Burst(second,1) && !state.CommitTick({7,3,1.0/60}), "prior scene command rejected");
        require(!state.SetSceneEpoch(7) && !state.SetSceneEpoch(0), "epoch cannot regress");
        require(state.CaptureFrame()->instances.empty() && state.CaptureFrame()->tick == 0, "scene reset clears state");
        auto third = state.RegisterInstance("new-scene",{});
        require(third.index == first.index && third.sceneEpoch != first.sceneEpoch, "epoch protects reset generation");
        require(retained->instances[0].handle == first && retained->instances[0].burstTotal == 5, "pending old frame retains values");
        auto invalid = moved; invalid.world[0] = std::numeric_limits<float>::infinity();
        require(!state.UpdateInstance(third,invalid) && !state.RegisterInstance("invalid",invalid).index, "nonfinite transform");
        state.Shutdown();
        require(epochToken->load() == 0, "epoch token cancels recording on shutdown without borrowing state");
        require(!state.CaptureFrame() && !state.Burst(third,1) && !state.SetSceneEpoch(9), "shutdown rejects new work");
        require(view1->instances[0].burstTotal == 5, "retired packet survives shutdown");
        state.Shutdown();
        require(!CaptureEffectFrame() && CommitEffectTick({}) && SetEffectSceneEpoch(1), "absent package bridge is no-op");
        EffectState delivery;
        require(delivery.SetSceneEpoch(2), "delivery epoch");
        const auto emitter = delivery.RegisterInstance("delivery",{});
        require(delivery.Burst(emitter,2), "first burst event");
        const auto firstBurst = delivery.CaptureFrame();
        require(delivery.UpdateInstance(emitter,moved) && delivery.CommitTick({2,1,1.0/60}) && delivery.Burst(emitter,3), "second burst event");
        auto latest = delivery.CaptureFrame();
        require(latest->bursts.size() == 2 && latest->bursts[0].transform.world[12] == 0 &&
            latest->bursts[1].transform.world[12] == 10 && latest->bursts[0].tick == 0 && latest->bursts[1].tick == 1,
            "coalesced frame preserves per-event transform and tick");
        firstBurst->AcknowledgeSubmittedBursts(); // Simulated successful submission receipt, not a GPU test.
        require(delivery.Burst(emitter,4), "new burst after older submission");
        require(delivery.CaptureFrame()->bursts.size() == 2 && delivery.CaptureFrame()->bursts.front().sequence == 2,
            "older view receipt cannot consume later events");
        latest = delivery.CaptureFrame(); latest->AcknowledgeSubmittedBursts();
        bool accepted = true;
        for (std::size_t i=0; i<EffectPendingBurstLimit; ++i) accepted = delivery.Burst(emitter,1) && accepted;
        require(accepted && !delivery.Burst(emitter,1), "bounded journal rejects overload instead of dropping events");
        auto full = delivery.CaptureFrame();
        require(full->bursts.size() == EffectPendingBurstLimit, "all pending events retained");
        full->AcknowledgeSubmittedBursts();
        require(delivery.Burst(emitter,9) && delivery.CaptureFrame()->bursts.size() == 1, "submission retires acknowledged events");
        require(delivery.UnregisterInstance(emitter) && delivery.CaptureFrame()->bursts.empty(), "destroy cancels unsubmitted events");
        require(delivery.SetSceneEpoch(3), "new delivery scene");
        const auto newEmitter = delivery.RegisterInstance("next",{});
        require(delivery.Burst(newEmitter,1), "new scene burst");
        full->AcknowledgeSubmittedBursts();
        require(delivery.UpdateInstance(newEmitter,{}) && delivery.CaptureFrame()->bursts.size() == 1,
            "old scene submission cannot acknowledge new scene events");
        require(RegisterFactory(&CreateCpuFacade), "facade registration");
        {
            Session session;
            std::string error;
            require(session.Initialize(error), "facade session initialization");
            require(SetEffectSceneEpoch(91) && CommitEffectTick({91,1,1.0/60}), "core bridge forwards committed tick");
            auto packet = CaptureEffectFrame();
            require(packet && packet->sceneEpoch == 91 && packet->tick == 1, "core bridge captures owned publication");
            Session duplicate;
            require(!duplicate.Initialize(error) && CaptureEffectFrame()->tick == 1, "overlapping session rejected without clearing owner");
            session.Reset();
            require(!CaptureEffectFrame() && !CaptureEffectContributor(), "session teardown disconnects producer and contributor");
            require(packet->tick == 1, "packet outlives facade session");
        }
        require(UnregisterFactory(&CreateCpuFacade), "facade unregistration");
        std::printf("EFFECT_STATE_OK checks=%u immutable multi-view stale-generation epoch burst shutdown thread-owner\n",checks);
#if defined(_DEBUG)
        std::puts("EFFECT_ARRAY_STORAGE_OK reads=4096 snapshot=1 allocationChecks=1 stringReleaseChecks=2");
#else
        std::puts("EFFECT_ARRAY_STORAGE_OK reads=4096 snapshot=1 allocationChecks=0 stringReleaseChecks=0");
#endif
        return 0;
    } catch (const std::exception& e) { std::fprintf(stderr,"EFFECT_STATE_FAIL %s\n",e.what()); return 1; }
}
