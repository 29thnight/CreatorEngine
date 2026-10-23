#pragma once
#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string_view>
#include <thread>
#include "../Utility_Framework/PropertyChange.h"

namespace Runtime
{
    // Local copied identities only; never serialize these as network IDs.
    struct ObservedLocation { std::uint32_t scene{}, index{}, generation{}; };
    enum class EntityChange : std::uint8_t { Created, Detached, Attached, Destroyed };
    struct SceneEntityEvent
    {
        std::uint64_t instance{};
        ObservedLocation before{}, after{};
        EntityChange change{};
    };
    struct ComponentWriteEvent
    {
        std::uint64_t entity{}, component{};
        ObservedLocation owner{};
        std::array<char,128> property{};
        Meta::PropertyChangeSource source{};
        bool allProperties{};
    };
    struct ISceneEntityObserver { virtual ~ISceneEntityObserver() = default; virtual void OnEntityChanged(const SceneEntityEvent&) = 0; };
    struct IComponentWriteObserver { virtual ~IComponentWriteObserver() = default; virtual void OnComponentWritten(const ComponentWriteEvent&) = 0; };
    struct ObservationMetrics { std::uint64_t delivered{}, failures{}, lost{}; };

    // One owner dispatches copied events. Workers may enqueue writes from joined
    // engine jobs, but never invoke adapters. Shared callback owners survive their
    // call; removal takes effect before the next invocation, including reentry.
    class SceneObservation
    {
        struct Slot { std::uint64_t id{}; std::shared_ptr<ISceneEntityObserver> entity; std::shared_ptr<IComponentWriteObserver> write; };
        struct Event { bool write{}; SceneEntityEvent entity{}; ComponentWriteEvent component{}; };
    public:
        std::uint64_t Subscribe(std::shared_ptr<ISceneEntityObserver> observer) { return Add(std::move(observer), {}); }
        std::uint64_t Subscribe(std::shared_ptr<IComponentWriteObserver> observer) { return Add({}, std::move(observer)); }
        void Unsubscribe(std::uint64_t id)
        {
            RequireOwner(); Slot retired;
            { std::lock_guard lock(mutex_); for(auto& slot:slots_) if(slot.id==id) { retired=std::move(slot); slot={}; break; } Refresh(); }
        }
        void Clear()
        {
            RequireOwner(); std::array<Slot,16> retired;
            { std::lock_guard lock(mutex_); retired=std::move(slots_);slots_={};count_=0;head_=0;Refresh(); }
        }
        bool HasWriteObservers() const noexcept { return writes_.load(std::memory_order_relaxed); }
        ObservationMetrics Metrics() const { std::lock_guard lock(mutex_);return metrics_; }
        void Publish(const SceneEntityEvent& event) { Event value;value.entity=event;Enqueue(value); }
        void Publish(const ComponentWriteEvent& event) { Event value;value.write=true;value.component=event;Enqueue(value); }
        void Drain()
        {
            RequireOwner(); if(draining_)return;draining_=true;
            for(std::uint32_t processed=0;processed<256;++processed)
            {
                Event event;std::array<Slot,16> snapshot;
                { std::lock_guard lock(mutex_);if(!count_) {draining_=false;return;}event=queue_[head_];head_=(head_+1)%queue_.size();--count_;snapshot=slots_; }
                for(const auto& selected:snapshot)
                {
                    if(!selected.id || (event.write ? !selected.write : !selected.entity))continue;
                    bool active=false;
                    { std::lock_guard lock(mutex_);for(const auto& slot:slots_)if(slot.id==selected.id){active=true;break;} }
                    if(!active)continue;
                    try {if(event.write)selected.write->OnComponentWritten(event.component);else selected.entity->OnEntityChanged(event.entity);
                        std::lock_guard lock(mutex_);++metrics_.delivered;}
                    catch(...) {std::lock_guard lock(mutex_);++metrics_.failures;}
                }
            }
            { std::lock_guard lock(mutex_);metrics_.lost+=count_;count_=0;head_=0; }
            draining_=false;
        }
    private:
        void RequireOwner() const {if(owner_!=std::this_thread::get_id())throw std::logic_error("Scene observation owner thread required");}
        std::uint64_t Add(std::shared_ptr<ISceneEntityObserver> entity,std::shared_ptr<IComponentWriteObserver> write)
        {
            RequireOwner();if(!entity&&!write)return 0;
            std::lock_guard lock(mutex_);
            for(const auto& slot:slots_)if(slot.id && slot.entity==entity && slot.write==write)return slot.id;
            for(auto& slot:slots_)if(!slot.id)
            { if(next_==UINT64_MAX)return 0;slot={++next_,std::move(entity),std::move(write)};Refresh();return slot.id; }
            return 0;
        }
        void Refresh() { bool writes=false,entities=false;for(const auto& slot:slots_){writes|=bool(slot.write);entities|=bool(slot.entity);}writes_=writes;entities_=entities; }
        void Enqueue(const Event& event)
        {
            if(event.write ? !writes_.load(std::memory_order_relaxed) : !entities_.load(std::memory_order_relaxed))return;
            { std::lock_guard lock(mutex_);if(count_==queue_.size()){++metrics_.lost;return;}queue_[(head_+count_)%queue_.size()]=event;++count_; }
            if(owner_==std::this_thread::get_id())Drain();
        }
        std::thread::id owner_{std::this_thread::get_id()};
        mutable std::mutex mutex_;
        std::array<Slot,16> slots_{};
        std::array<Event,256> queue_{};
        std::uint32_t head_{},count_{};
        std::uint64_t next_{};
        bool draining_{};
        std::atomic_bool writes_{false},entities_{false};
        ObservationMetrics metrics_{};
    };
}
