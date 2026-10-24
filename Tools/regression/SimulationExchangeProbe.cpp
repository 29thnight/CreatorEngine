#include "../../Engine/SceneRuntime/SimulationExchange.h"
#include "../../Engine/SceneRuntime/SimulationTickExecution.h"
#include <cassert>
#include <cstdio>
#include <vector>
#include <stdexcept>
#include <atomic>
#include <string>
using namespace Runtime;
static std::atomic<int> checks;
static void Check(bool value) { ++checks; if (!value) throw std::runtime_error("simulation exchange assertion " + std::to_string(checks.load())); }
struct Adapter : SimulationAdapter
{
    int state{}, captures{};
    std::vector<std::uint64_t> order;
    bool fail{}, oversized{}, throws{};
    bool Apply(const TickCommand& c) override
    {
        if (throws) throw std::runtime_error("apply failure");
        order.push_back(c.sequence);
        if (c.kind != 17 || c.bytes != 4) return false;
        int value; std::memcpy(&value, c.payload.data(), 4); state += value; return true;
    }
    bool Capture(std::span<std::byte> output, std::uint32_t& bytes, std::uint32_t& schema) override
    {
        ++captures; schema = 17; bytes = oversized ? 4097 : 4;
        std::memcpy(output.data(), &state, 4); return !fail;
    }
};
static auto NoApply = [](const TickCommand&) { return false; };
static auto NoCapture = [](std::span<std::byte>, std::uint32_t&, std::uint32_t&) { return false; };
int main()
{
    for (int fps : {30, 60, 144})
    {
        SimulationExchange q; SimulationTick clock;
        Check(q.Reset(true) == TickResult::Ok);
        auto epoch = q.Status().epoch;
        auto a = std::make_shared<Adapter>(); Check(q.Bind(epoch, a) == TickResult::Ok);
        int value = 3; auto payload = std::as_bytes(std::span(&value, 1)); std::uint64_t sequence;
        // Same target ticks independent of input frame cadence.
        Check(q.Submit(epoch, 1, 17, payload, sequence) == TickResult::Ok);
        Check(q.Submit(epoch, 3, 17, payload, sequence) == TickResult::Ok);
        Check(q.Submit(epoch, 3, 999, {}, sequence) == TickResult::Ok);
        Check(q.Submit(epoch, 7, 17, payload, sequence) == TickResult::Ok);
        value = 100; // admission owns a copy
        for (int frame = 0; frame < fps; ++frame)
        {
            Check(clock.Admit(1.0 / fps));
            while (clock.Ready())
            {
                auto tick = clock.committed + 1;
                Check(q.Begin(tick, NoApply) == TickResult::Ok);
                Check(q.Submit(epoch, tick, 17, {}, sequence) == TickResult::Late);
                Check(q.Prepare(NoCapture) == TickResult::Ok);
                TickSnapshotHeader header; std::array<std::byte,4096> out;
                Check(q.Read(epoch, clock.committed, header, out) == TickResult::NoSnapshot);
                clock.Commit(); Check(q.Commit() == TickResult::Ok);
                Check(q.Read(epoch, tick - 1, header, out) == TickResult::Ok);
                int state; std::memcpy(&state, out.data(), 4);
                Check(header.tick == tick && header.schema == 17 && header.bytes == 4);
                Check(state == (tick >= 7 ? 9 : tick >= 3 ? 6 : 3));
                if (tick == 3) Check(header.applied == 1 && header.rejected == 1);
            }
        }
        Check(clock.committed == 60 && a->state == 9 && a->captures == 60);
        Check((a->order == std::vector<std::uint64_t>{1,2,3,4}));
        TickSnapshotHeader h; std::array<std::byte,4096> out;
        Check(q.Read(epoch, 0, h, out) == TickResult::SnapshotGap && h.tick == 53);
        Check(q.Read(epoch, 59, h, {}) == TickResult::BufferTooSmall && h.bytes == 4);
        Check(q.Read(epoch, 59, h, out) == TickResult::Ok && h.tick == 60);
        Check(q.Submit(epoch, 60, 17, {}, sequence) == TickResult::Late);
        Check(q.Submit(epoch, 189, 17, {}, sequence) == TickResult::TooFar);
        Check(q.Submit(epoch, 61, 0, {}, sequence) == TickResult::InvalidArgument);
        std::array<std::byte,65> big{};
        Check(q.Submit(epoch, 61, 17, big, sequence) == TickResult::InvalidArgument);
        for (int i=0;i<256;++i) Check(q.Submit(epoch, 62, 17, {}, sequence) == TickResult::Ok);
        Check(q.Submit(epoch, 62, 17, {}, sequence) == TickResult::Full && sequence == 0);
        Check(q.Bind(epoch, {}) == TickResult::Busy);
        // Pausing means no Begin/Commit: queue and committed state stay intact.
        Check(q.Status().queued == 256 && q.Status().committed == 60);
        Check(q.Reset(true) == TickResult::Ok);
        Check(q.Submit(epoch, 1, 17, {}, sequence) == TickResult::StaleEpoch);
        Check(q.Read(epoch, 0, h, out) == TickResult::StaleEpoch);
        epoch = q.Status().epoch;
        Check(q.Status().queued == 0 && q.Status().committed == 0);
        Check(q.Bind(epoch, a) == TickResult::Ok);
        a->fail = true;
        Check(q.Begin(1, NoApply) == TickResult::Ok);
        Check(q.Prepare(NoCapture) == TickResult::Failed);
        Check(q.Commit() == TickResult::Failed && q.Status().committed == 0);
        Check(q.Read(epoch, 0, h, out) == TickResult::NoSnapshot);
        Check(q.Submit(epoch, 1, 17, {}, sequence) == TickResult::Inactive);
        Check(q.Reset(true) == TickResult::Ok); epoch = q.Status().epoch;
        a->fail = false; a->oversized = true; Check(q.Bind(epoch, a) == TickResult::Ok);
        Check(q.Begin(1, NoApply) == TickResult::Ok && q.Prepare(NoCapture) == TickResult::Failed);
        Check(q.Reset(true) == TickResult::Ok); epoch = q.Status().epoch;
        a->oversized = false; a->throws = true; Check(q.Bind(epoch, a) == TickResult::Ok);
        Check(q.Submit(epoch, 1, 17, payload, sequence) == TickResult::Ok);
        Check(q.Begin(1, NoApply) == TickResult::Failed && q.Commit() == TickResult::Failed);
    }
    SimulationExchange q; Check(q.Reset(true) == TickResult::Ok);
    auto epoch = q.Status().epoch;
    std::array<std::thread,4> workers;
    for (auto& worker : workers) worker = std::thread([&]
    {
        Check(q.Reset(false) == TickResult::WrongThread);
        for (int i=0;i<64;++i) { std::uint64_t s; Check(q.Submit(epoch,1,17,{},s) == TickResult::Ok); }
    });
    for (auto& worker : workers) worker.join();
    Check(q.Status().queued == 256);
    Check(q.Begin(1, NoApply) == TickResult::Ok);
    // Physics/update failure: fail closed after application, no retry or snapshot.
    q.Fail(); Check(q.Commit() == TickResult::Failed && q.Status().committed == 0);
    Check(q.Reset(false) == TickResult::Ok);
    std::uint64_t s; Check(q.Submit(q.Status().epoch,1,17,{},s) == TickResult::Inactive);
    Check(q.Reset(true) == TickResult::Ok); epoch = q.Status().epoch;
    auto adapter = std::make_shared<Adapter>(); std::weak_ptr<Adapter> weak = adapter;
    Check(q.Bind(epoch, adapter) == TickResult::Ok);
    Check(q.Begin(1, NoApply) == TickResult::Ok && q.Prepare(NoCapture) == TickResult::Ok && q.Commit() == TickResult::Ok);
    adapter->fail = true;
    Check(q.Begin(2, NoApply) == TickResult::Ok && q.Prepare(NoCapture) == TickResult::Failed);
    TickSnapshotHeader header; std::array<std::byte,4096> out;
    Check(q.Read(epoch,0,header,out) == TickResult::Ok && header.tick == 1);
    Check(q.Read(epoch,1,header,out) == TickResult::NoSnapshot);
    adapter.reset(); Check(!weak.expired());
    Check(q.Reset(true) == TickResult::Ok && weak.expired());
    epoch = q.Status().epoch;
    Check(q.Submit(epoch,1,17,{},s) == TickResult::Ok);
    Check(q.Submit(epoch,1,17,{},s) == TickResult::Ok);
    int calls = 0;
    Check(q.Begin(1,[&](const TickCommand&) { ++calls; q.Reset(true); return true; }) == TickResult::StaleEpoch);
    Check(calls == 1 && q.Status().queued == 0 && q.Commit() == TickResult::Failed);
    Check(q.Begin(1,NoApply) == TickResult::Ok);
    Check(q.Prepare([&](std::span<std::byte>,std::uint32_t&,std::uint32_t&) { q.Reset(true); return true; }) == TickResult::StaleEpoch);
    Check(q.Commit() == TickResult::Failed);
    // The actual transaction used by SceneManager and HeadlessHost must have
    // one order and fail closed for every callback failure/exception.
    for (int failure = 0; failure < 7; ++failure)
    {
        SimulationTick clock;
        SimulationExchange exchange;
        Check(exchange.Reset(true) == TickResult::Ok);
        Check(clock.Admit(SimulationTick::seconds));
        std::vector<int> order;
        const auto run = [&]
        {
            return ExecuteSimulationTick(clock, exchange, NoApply,
                [&](float dt) { order.push_back(1); Check(dt == static_cast<float>(SimulationTick::seconds));
                    if (failure == 5) throw std::runtime_error("managed exception"); return failure != 1; },
                [&] { return failure != 2; },
                [&](float) { order.push_back(2); return failure != 3; },
                [&] { order.push_back(3); if (failure == 6) throw std::runtime_error("observer exception"); },
                [&](std::span<std::byte>, std::uint32_t& bytes, std::uint32_t& schema)
                    { order.push_back(4); bytes = 0; schema = 1; return failure != 4; });
        };
        bool threw = false;
        TickExecutionError result = TickExecutionError::None;
        try { result = run(); } catch (const std::runtime_error&) { threw = true; }
        if (!failure)
        {
            Check(!threw && result == TickExecutionError::None);
            Check(order == std::vector<int>({1,2,3,4}));
            Check(clock.committed == 1 && exchange.Status().committed == 1);
        }
        else
        {
            Check(threw || result != TickExecutionError::None);
            Check(!clock.committed && !exchange.Status().committed && !exchange.Status().active);
            Check(exchange.Read(exchange.Status().epoch, 0, header, out) == TickResult::NoSnapshot);
        }
    }
    std::printf("SIMULATION_EXCHANGE_OK checks=%d fps=30,60,144 queue=256 snapshots=8 concurrency=4 failure=closed\n", checks.load());
}
