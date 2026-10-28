#include "../Containers/ContainerAPI.h"
#pragma once
#include "SimulationExchangeABI.h"
#include <array>
#include <cstdint>
#include <cstring>
#include <memory>
#include <mutex>
#include <span>
#include <thread>
#include <limits>
#include <algorithm>
#include <utility>

namespace Runtime
{
    enum class TickResult : std::int32_t
    {
        Ok, Inactive, StaleEpoch, InvalidArgument, Late, TooFar, Full,
        NoSnapshot, SnapshotGap, BufferTooSmall, WrongThread, Busy, Failed
    };
    struct TickCommand
    {
        std::uint64_t epoch{}, tick{}, sequence{};
        std::uint32_t kind{}, bytes{};
        ce::fixed_array<std::byte, 64> payload{};
    };
    // Kind/schema 1 is an explicitly local bridge, not a replicated entity ID.
    struct LocalPositionValue { std::uint32_t index{}, generation{}; float x{}, y{}, z{}; };
    struct LocalPositionSnapshot { LocalPositionValue value{}; std::uint32_t alive{}; };
    // Local ABI only, never a network packet. Schema/IDs/authority belong to the
    // N2/N4 adapter. Apply validates each decoded value before touching the world.
    struct SimulationAdapter
    {
        virtual ~SimulationAdapter() = default;
        virtual bool Apply(const TickCommand&) = 0;
        virtual bool Capture(std::span<std::byte> output, std::uint32_t& bytes, std::uint32_t& schema) = 0;
    };

    // One GT consumer, concurrent copied-value producers/readers. No borrowed
    // engine pointers or caller buffers survive a call. No callbacks under lock.
    class SimulationExchange
    {
    public:
        static constexpr std::size_t command_capacity = 256, snapshot_capacity = 8, snapshot_bytes = 4096;
        static constexpr std::uint64_t future_ticks = 128;
        SimulationExchange() : owner_(std::this_thread::get_id()) {}
        TickStatus Status() const { std::lock_guard lock(mutex_); return status_; }

        // GT boundary only. Reset invalidates queued commands, cursors and adapters.
        TickResult Reset(bool active)
        {
            if (!Owner()) return TickResult::WrongThread;
            std::shared_ptr<SimulationAdapter> old;
            {
                std::lock_guard lock(mutex_);
                if (status_.epoch == (std::numeric_limits<std::uint64_t>::max)()) return TickResult::Failed;
                const auto epoch = status_.epoch + 1;
                status_ = {epoch, 0, 0, 0, active ? 1u : 0u};
                next_ = 0; staged_ = {}; ready_ = false;
                for (auto& snapshot : snapshots_) snapshot = {};
                old = std::move(adapter_);
            }
            return TickResult::Ok; // release native adapter outside the lock
        }
        TickResult Bind(std::uint64_t epoch, std::shared_ptr<SimulationAdapter> adapter)
        {
            if (!Owner()) return TickResult::WrongThread;
            std::shared_ptr<SimulationAdapter> old;
            {
                std::lock_guard lock(mutex_);
                if (epoch != status_.epoch) return TickResult::StaleEpoch;
                if (!status_.active) return TickResult::Inactive;
                if (status_.executing || status_.queued) return TickResult::Busy;
                old = std::exchange(adapter_, std::move(adapter));
            }
            return TickResult::Ok;
        }
        TickResult Submit(std::uint64_t epoch, std::uint64_t tick, std::uint32_t kind,
            std::span<const std::byte> payload, std::uint64_t& sequence)
        {
            sequence = 0;
            if (!kind || payload.size() > 64) return TickResult::InvalidArgument;
            std::lock_guard lock(mutex_);
            if (epoch != status_.epoch) return TickResult::StaleEpoch;
            if (!status_.active) return TickResult::Inactive;
            if (tick <= status_.committed || (status_.executing && tick <= status_.executing)) return TickResult::Late;
            if (tick - status_.committed > future_ticks) return TickResult::TooFar;
            if (status_.queued == command_capacity) return TickResult::Full;
            if (next_ == (std::numeric_limits<std::uint64_t>::max)()) return TickResult::Failed;
            auto& command = commands_[status_.queued++];
            command = {epoch, tick, ++next_, kind, static_cast<std::uint32_t>(payload.size())};
            if (!payload.empty()) std::memcpy(command.payload.data(), payload.data(), payload.size());
            sequence = command.sequence;
            return TickResult::Ok;
        }
        template<class Apply> TickResult Begin(std::uint64_t tick, Apply&& fallback)
        {
            if (!Owner()) return TickResult::WrongThread;
            ce::fixed_array<TickCommand, command_capacity> batch{};
            std::size_t count = 0;
            std::shared_ptr<SimulationAdapter> adapter;
            {
                std::lock_guard lock(mutex_);
                if (!status_.active) return TickResult::Inactive;
                if (status_.executing) return TickResult::Busy;
                if (tick != status_.committed + 1 || !tick) return TickResult::InvalidArgument;
                status_.executing = tick;
                staged_ = {}; staged_.header = {status_.epoch, tick}; ready_ = false;
                std::uint32_t retained = 0;
                for (std::uint32_t i = 0; i < status_.queued; ++i)
                    if (commands_[i].tick == tick) batch[count++] = commands_[i];
                    else commands_[retained++] = commands_[i];
                status_.queued = retained;
                adapter = adapter_;
            }
            try
            {
                for (std::size_t i = 0; i < count; ++i)
                {
                    bool accepted = adapter ? adapter->Apply(batch[i]) : fallback(batch[i]);
                    // Reset during a callback must not publish or continue an old batch.
                    if (Status().epoch != batch[i].epoch) return TickResult::StaleEpoch;
                    accepted ? ++staged_.header.applied : ++staged_.header.rejected;
                }
            }
            catch (...) { Fail(); return TickResult::Failed; }
            return TickResult::Ok;
        }
        template<class Capture> TickResult Prepare(Capture&& fallback)
        {
            if (!Owner()) return TickResult::WrongThread;
            std::shared_ptr<SimulationAdapter> adapter;
            std::uint64_t epoch;
            {
                std::lock_guard lock(mutex_);
                if (!status_.active || !status_.executing) return TickResult::Inactive;
                epoch = status_.epoch; adapter = adapter_;
            }
            bool captured = false;
            try
            {
                captured = adapter ? adapter->Capture(staged_.payload, staged_.header.bytes, staged_.header.schema)
                    : fallback(std::span<std::byte>(staged_.payload), staged_.header.bytes, staged_.header.schema);
            }
            catch (...) { captured = false; }
            if (Status().epoch != epoch) return TickResult::StaleEpoch;
            if (!captured || staged_.header.bytes > snapshot_bytes) { Fail(); return TickResult::Failed; }
            ready_ = true;
            return TickResult::Ok;
        }
        TickResult Commit()
        {
            if (!Owner()) return TickResult::WrongThread;
            std::lock_guard lock(mutex_);
            if (!ready_ || !status_.active || !status_.executing || staged_.header.epoch != status_.epoch) return TickResult::Failed;
            snapshots_[(status_.executing - 1) % snapshot_capacity] = staged_;
            status_.committed = status_.executing; status_.executing = 0; ready_ = false;
            return TickResult::Ok;
        }
        void Fail()
        {
            if (!Owner()) return;
            std::lock_guard lock(mutex_);
            status_.active = 0; status_.executing = 0; status_.queued = 0; ready_ = false;
            // Previously successful snapshots remain readable; no partial state is published.
        }
        TickResult Read(std::uint64_t epoch, std::uint64_t after, TickSnapshotHeader& header, std::span<std::byte> output) const
        {
            header = {};
            std::lock_guard lock(mutex_);
            if (epoch != status_.epoch) return TickResult::StaleEpoch;
            if (after >= status_.committed) return TickResult::NoSnapshot;
            const auto first = status_.committed > snapshot_capacity ? status_.committed - snapshot_capacity + 1 : 1;
            const auto tick = (std::max)(after + 1, first);
            const auto& snapshot = snapshots_[(tick - 1) % snapshot_capacity];
            if (snapshot.header.epoch != epoch || snapshot.header.tick != tick) return TickResult::NoSnapshot;
            header = snapshot.header;
            if (output.size() < header.bytes) return TickResult::BufferTooSmall;
            if (header.bytes) std::memcpy(output.data(), snapshot.payload.data(), header.bytes);
            return tick != after + 1 ? TickResult::SnapshotGap : TickResult::Ok;
        }
    private:
        bool Owner() const { return owner_ == std::this_thread::get_id(); }
        struct Snapshot { TickSnapshotHeader header{}; ce::fixed_array<std::byte, snapshot_bytes> payload{}; };
        std::thread::id owner_;
        mutable std::mutex mutex_;
        TickStatus status_{};
        std::uint64_t next_{};
        ce::fixed_array<TickCommand, command_capacity> commands_{};
        ce::fixed_array<Snapshot, snapshot_capacity> snapshots_{};
        Snapshot staged_{};
        bool ready_{};
        std::shared_ptr<SimulationAdapter> adapter_;
    };
    static_assert(sizeof(TickStatus) == 32 && sizeof(TickSnapshotHeader) == 32);
}
