#include "AudioRuntime.h"
#include "../../RenderEngine/Experiment/Cooked/CookedAudioClipSource.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace wave
{
    namespace
    {
        constexpr float kHalfPi = 1.57079632679489661923f;
        constexpr float kSilentGain = 0.0001f;

        float FiniteGain(float value)
        {
            return std::isfinite(value) ? std::clamp(value, 0.0f, 16.0f) : 0.0f;
        }

        bool FiniteVector(const math::vector3& value)
        {
            return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
        }

        PlayRequest Sanitize(PlayRequest request)
        {
            request.volume = FiniteGain(request.volume);
            request.pitch = std::isfinite(request.pitch) ? std::clamp(request.pitch, 0.01f, 4.0f) : 1.0f;
            request.spatialBlend = std::isfinite(request.spatialBlend) ?
                std::clamp(request.spatialBlend, 0.0f, 1.0f) : 0.0f;
            request.minimumDistance = std::isfinite(request.minimumDistance) ?
                std::max(0.001f, request.minimumDistance) : 1.0f;
            request.maximumDistance = std::isfinite(request.maximumDistance) ?
                std::max(request.minimumDistance, request.maximumDistance) : request.minimumDistance;
            request.reverbSendDecibels = std::isfinite(request.reverbSendDecibels) ?
                std::clamp(request.reverbSendDecibels, -80.0f, 10.0f) : -80.0f;
            if (!request.bus.IsValid() || request.bus == Buses::Room)
            {
                request.bus = Buses::SFX;
            }
            if (!FiniteVector(request.position))
            {
                request.position = { 0.0f, 0.0f, 0.0f };
            }
            if (!FiniteVector(request.velocity))
            {
                request.velocity = { 0.0f, 0.0f, 0.0f };
            }
            auto& points = request.customRolloff;
            points.erase(std::remove_if(points.begin(), points.end(), [](const RolloffPoint& point)
            {
                return !std::isfinite(point.distance) || !std::isfinite(point.gain);
            }), points.end());
            std::stable_sort(points.begin(), points.end(), [](const RolloffPoint& left, const RolloffPoint& right)
            {
                return left.distance < right.distance;
            });
            std::size_t write = 0u;
            for (RolloffPoint point : points)
            {
                point.distance = std::max(0.0f, point.distance);
                point.gain = FiniteGain(point.gain);
                if (write > 0u && points[write - 1u].distance == point.distance)
                {
                    points[write - 1u] = point;
                }
                else
                {
                    points[write++] = point;
                }
            }
            points.resize(write);
            return request;
        }

        bool CanVirtualize(const VoiceRecord& record)
        {
            return record.request.allowVirtualization && (record.loop || record.request.persistent);
        }
    }

    AudioRuntime::AudioRuntime(AudioBackend& backend, std::size_t voiceCapacity,
        std::uint32_t generationNamespace)
        : m_backend(backend)
        , m_voices(voiceCapacity, generationNamespace)
        , m_physicalLimit(voiceCapacity)
    {
    }

    AudioRuntime::~AudioRuntime()
    {
        Shutdown();
    }

    bool AudioRuntime::Start(const DeviceSettings& settings)
    {
        if (m_started)
        {
            return true;
        }
        m_started = m_backend.Start(settings);
        if (m_started)
        {
            m_backend.SetListener(m_listener);
            for (const auto& [bus, gain] : m_busVolumes)
            {
                m_backend.SetBusVolume(BusId{ bus }, gain);
            }
        }
        return m_started;
    }

    void AudioRuntime::Shutdown()
    {
        if (!m_started)
        {
            return;
        }
        // Close the producer first. Stop still accepts existing handles while closing.
        m_started = false;
        m_voices.ForEachAlive([this](VoiceHandle handle, VoiceRecord&)
        {
            Stop(handle);
        });
        for (const ClipKey& key : m_clips)
        {
            m_backend.UnloadClip(key);
        }
        m_clips.clear();
        m_backend.Stop();
    }

    float AudioRuntime::SampleAttenuation(const PlayRequest& request,
        const ListenerState& listener) noexcept
    {
        const double x = static_cast<double>(request.position.x) - listener.position.x;
        const double y = static_cast<double>(request.position.y) - listener.position.y;
        const double z = static_cast<double>(request.position.z) - listener.position.z;
        const float distance = static_cast<float>(std::sqrt(x * x + y * y + z * z));
        if (!std::isfinite(distance))
        {
            return 0.0f;
        }
        if (request.rolloff == RolloffKind::Custom && !request.customRolloff.empty())
        {
            const auto& points = request.customRolloff;
            if (distance <= points.front().distance)
            {
                return points.front().gain;
            }
            for (std::size_t index = 1u; index < points.size(); ++index)
            {
                const auto& left = points[index - 1u];
                const auto& right = points[index];
                if (distance < right.distance)
                {
                    const float length = right.distance - left.distance;
                    return length > 0.0f ? std::lerp(left.gain, right.gain,
                        (distance - left.distance) / length) : right.gain;
                }
            }
            return points.back().gain;
        }
        const float minimum = std::max(0.001f, request.minimumDistance);
        const float maximum = std::max(minimum, request.maximumDistance);
        if (distance <= minimum)
        {
            return 1.0f;
        }
        if (distance >= maximum)
        {
            return 0.0f;
        }
        if (request.rolloff == RolloffKind::Linear || request.rolloff == RolloffKind::Custom)
        {
            return 1.0f - (distance - minimum) / (maximum - minimum);
        }
        return minimum / distance;
    }

    float AudioRuntime::SampleDoppler(const PlayRequest& request, const ListenerState& listener) noexcept
    {
        if (request.spatialBlend <= 0.0f)
        {
            return 1.0f;
        }
        const double x = static_cast<double>(request.position.x) - listener.position.x;
        const double y = static_cast<double>(request.position.y) - listener.position.y;
        const double z = static_cast<double>(request.position.z) - listener.position.z;
        const double distance = std::sqrt(x * x + y * y + z * z);
        if (!std::isfinite(distance) || distance < 0.00001)
        {
            return 1.0f;
        }
        constexpr double speedOfSound = 343.3;
        const double listenerSpeed = std::clamp((listener.velocity.x * x + listener.velocity.y * y
            + listener.velocity.z * z) / distance, -speedOfSound * 0.9, speedOfSound * 0.9);
        const double sourceSpeed = std::clamp((request.velocity.x * x + request.velocity.y * y
            + request.velocity.z * z) / distance, -speedOfSound * 0.9, speedOfSound * 0.9);
        const float ratio = static_cast<float>(std::clamp((speedOfSound + listenerSpeed)
            / (speedOfSound + sourceSpeed), 0.5, 2.0));
        return std::lerp(1.0f, ratio, request.spatialBlend);
    }

    void AudioRuntime::RefreshGain(VoiceRecord& record)
    {
        record.request.attenuationGain = SampleAttenuation(record.request, m_listener);
        record.request.dopplerPitch = SampleDoppler(record.request, m_listener);
        const float blend = record.request.spatialBlend * kHalfPi;
        record.attenuationGain = std::cos(blend) + std::sin(blend) * record.request.attenuationGain;
        record.busGain = GetBusVolume(Buses::Master);
        if (record.bus != Buses::Master)
        {
            record.busGain *= GetBusVolume(record.bus);
        }
    }

    VoiceHandle AudioRuntime::FindVictim(BusId bus, ConcurrencyGroupId group,
        StealPolicy policy, bool physicalOnly, VoiceHandle excluded) const
    {
        if (policy == StealPolicy::Reject)
        {
            return {};
        }
        VoiceHandle result{};
        const VoiceRecord* worst = nullptr;
        m_voices.ForEachAlive([&](VoiceHandle handle, const VoiceRecord& record)
        {
            if (handle == excluded || (bus.IsValid() && record.bus != bus)
                || (group.IsValid() && record.request.concurrencyGroup.value != group.value)
                || (physicalOnly && !record.backendVoice.IsValid()))
            {
                return;
            }
            bool replace = worst == nullptr;
            if (worst != nullptr)
            {
                if (policy == StealPolicy::LowestPriority && record.priority != worst->priority)
                {
                    replace = record.priority > worst->priority;
                }
                else if (policy != StealPolicy::Oldest && record.EffectiveGain() != worst->EffectiveGain())
                {
                    replace = record.EffectiveGain() < worst->EffectiveGain();
                }
                else if (record.createdFrame != worst->createdFrame)
                {
                    replace = record.createdFrame < worst->createdFrame;
                }
                else
                {
                    replace = handle.index < result.index;
                }
            }
            if (replace)
            {
                result = handle;
                worst = &record;
            }
        });
        return result;
    }

    bool AudioRuntime::CanDisplace(const VoiceRecord& incoming,
        const VoiceRecord& victim, StealPolicy policy) const
    {
        if (policy == StealPolicy::Reject)
        {
            return false;
        }
        if (policy == StealPolicy::LowestPriority && incoming.priority != victim.priority)
        {
            return incoming.priority < victim.priority;
        }
        if (policy == StealPolicy::Quietest || policy == StealPolicy::LowestPriority)
        {
            return incoming.EffectiveGain() > victim.EffectiveGain()
                || (incoming.EffectiveGain() == victim.EffectiveGain()
                    && incoming.createdFrame > victim.createdFrame);
        }
        return true;
    }

    std::size_t AudioRuntime::PhysicalCount(BusId bus) const
    {
        std::size_t count = 0u;
        m_voices.ForEachAlive([&](VoiceHandle, const VoiceRecord& record)
        {
            if (record.backendVoice.IsValid() && (!bus.IsValid() || record.bus == bus))
            {
                ++count;
            }
        });
        return count;
    }

    void AudioRuntime::Virtualize(VoiceRecord& record)
    {
        if (record.backendVoice.IsValid())
        {
            record.playheadFrame = static_cast<double>(m_backend.VoicePlayhead(record.backendVoice));
            m_backend.StopVoice(record.backendVoice);
            record.backendVoice = {};
        }
        record.state = record.paused ? VoiceState::Paused : VoiceState::Virtual;
    }

    bool AudioRuntime::MakePhysical(VoiceHandle handle, VoiceRecord& record)
    {
        if (record.backendVoice.IsValid())
        {
            return true;
        }
        if (record.paused || m_physicalLimit == 0u || record.EffectiveGain() <= kSilentGain)
        {
            return false;
        }
        const auto busLimit = m_busLimits.find(record.bus.value);
        const Limit limit = busLimit != m_busLimits.end() ? busLimit->second :
            Limit{ m_physicalLimit, StealPolicy::LowestPriority };
        if (limit.cap == 0u)
        {
            return false;
        }
        const bool busFull = PhysicalCount(record.bus) >= limit.cap;
        const bool full = PhysicalCount() >= m_physicalLimit;
        if (busFull || full)
        {
            const StealPolicy policy = busFull ? limit.policy : m_masterPolicy;
            const VoiceHandle victimHandle = FindVictim(busFull ? record.bus : BusId{}, {}, policy, true, handle);
            VoiceRecord* victim = m_voices.Find(victimHandle);
            if (victim == nullptr || !CanDisplace(record, *victim, policy))
            {
                return false;
            }
            if (CanVirtualize(*victim))
            {
                Virtualize(*victim);
            }
            else
            {
                Stop(victimHandle);
                ++m_metrics.stolen;
            }
        }
        PlayRequest projected = record.request;
        projected.volume = record.baseGain * record.userGain;
        record.backendVoice = m_backend.StartVoice(projected, record.backendClip);
        if (!record.backendVoice.IsValid())
        {
            ++m_metrics.backendFailures;
            m_lastError = "Audio backend failed to start the retained clip generation: " + record.clip.Text();
            return false;
        }
        if (record.playheadFrame > 0.0)
        {
            if (!m_backend.SeekVoice(record.backendVoice, static_cast<std::uint64_t>(record.playheadFrame)))
            {
                m_backend.StopVoice(record.backendVoice);
                record.backendVoice = {};
                ++m_metrics.backendFailures;
                m_lastError = "Audio backend failed to restore the retained clip playhead: " + record.clip.Text();
                return false;
            }
        }
        record.state = VoiceState::Physical;
        record.settingsDirty = false;
        record.transformDirty = false;
        return true;
    }

    VoiceHandle AudioRuntime::Play(const PlayRequest& input)
    {
        m_lastError.clear();
        if (!m_started || input.clip.IsEmpty() || !m_backend.HasClip(input.clip))
        {
            return {};
        }
        const PlayRequest request = Sanitize(input);
        const ClipInfo info = m_backend.GetClipInfo(request.clip);
        if (request.spatialBlend > 0.0f && info.channels > 1u)
        {
            m_lastError = "A spatial point source requires an explicitly imported mono clip";
            ++m_metrics.dropped;
            return {};
        }
        VoiceRecord incoming{};
        incoming.request = request;
        incoming.bus = request.bus;
        incoming.baseGain = request.volume;
        incoming.priority = request.priority;
        incoming.createdFrame = m_frame;
        RefreshGain(incoming);
        if (request.preemptSameClip)
        {
            m_voices.ForEachAlive([&](VoiceHandle handle, const VoiceRecord& record)
            {
                if (record.clip == request.clip && record.request.concurrencyGroup.value == request.concurrencyGroup.value)
                {
                    Stop(handle);
                    ++m_metrics.stolen;
                }
            });
        }
        const auto groupLimit = m_concurrencyLimits.find(request.concurrencyGroup.value);
        if (request.concurrencyGroup.IsValid() && groupLimit != m_concurrencyLimits.end())
        {
            std::size_t count = 0u;
            m_voices.ForEachAlive([&](VoiceHandle, const VoiceRecord& record)
            {
                if (record.request.concurrencyGroup.value == request.concurrencyGroup.value)
                {
                    ++count;
                }
            });
            if (count >= groupLimit->second.cap)
            {
                const VoiceHandle victim = FindVictim({}, request.concurrencyGroup, groupLimit->second.policy, false);
                const VoiceRecord* record = m_voices.Find(victim);
                if (groupLimit->second.cap == 0u || record == nullptr
                    || !CanDisplace(incoming, *record, groupLimit->second.policy))
                {
                    ++m_metrics.dropped;
                    return {};
                }
                Stop(victim);
                ++m_metrics.stolen;
            }
        }
        VoiceHandle handle = m_voices.Acquire(request, m_frame);
        if (!handle.IsValid())
        {
            // A configured policy also applies at the hard logical capacity.
            // Without an explicit limit policy, preserve fail-closed admission.
            const auto busLimit = m_busLimits.find(request.bus.value);
            const StealPolicy policy = busLimit != m_busLimits.end() ? busLimit->second.policy :
                (m_masterPolicyConfigured ? m_masterPolicy : StealPolicy::Reject);
            const VoiceHandle victim = FindVictim(busLimit != m_busLimits.end() ? request.bus : BusId{},
                {}, policy, false);
            const VoiceRecord* victimRecord = m_voices.Find(victim);
            if (victimRecord == nullptr || !CanDisplace(incoming, *victimRecord, policy))
            {
                ++m_metrics.dropped;
                return {};
            }
            Stop(victim);
            ++m_metrics.stolen;
            handle = m_voices.Acquire(request, m_frame);
        }
        VoiceRecord* record = m_voices.Find(handle);
        if (record == nullptr)
        {
            ++m_metrics.dropped;
            return {};
        }
        record->clipInfo = info;
        record->backendClip = m_backend.RetainClip(request.clip);
        RefreshGain(*record);
        const std::uint64_t failuresBefore = m_metrics.backendFailures;
        if (!MakePhysical(handle, *record))
        {
            if (CanVirtualize(*record) && m_metrics.backendFailures == failuresBefore)
            {
                record->state = VoiceState::Virtual;
            }
            else
            {
                m_backend.ReleaseClip(record->backendClip);
                m_voices.Release(handle);
                ++m_metrics.dropped;
                return {};
            }
        }
        return handle;
    }

    void AudioRuntime::Stop(VoiceHandle handle)
    {
        VoiceRecord* record = m_voices.Find(handle);
        if (record == nullptr)
        {
            return;
        }
        if (record->backendVoice.IsValid())
        {
            m_backend.StopVoice(record->backendVoice);
        }
        m_backend.ReleaseClip(record->backendClip);
        m_voices.Release(handle);
    }

    void AudioRuntime::SetPaused(VoiceHandle handle, bool paused)
    {
        VoiceRecord* record = m_voices.Find(handle);
        if (record == nullptr)
        {
            return;
        }
        record->paused = paused;
        if (record->backendVoice.IsValid())
        {
            m_backend.SetVoicePaused(record->backendVoice, paused);
        }
        record->state = paused ? VoiceState::Paused :
            (record->backendVoice.IsValid() ? VoiceState::Physical : VoiceState::Virtual);
    }

    bool AudioRuntime::IsAlive(VoiceHandle handle) const
    {
        return m_voices.IsAlive(handle);
    }

    void AudioRuntime::StopByOwner(std::uint64_t ownerId)
    {
        if (ownerId == 0u)
        {
            return;
        }
        m_voices.ForEachAlive([&](VoiceHandle handle, const VoiceRecord& record)
        {
            if (record.ownerId == ownerId)
            {
                Stop(handle);
            }
        });
    }

    void AudioRuntime::SetVoiceTransform(VoiceHandle handle,
        const math::vector3& position, const math::vector3& velocity)
    {
        VoiceRecord* record = m_voices.Find(handle);
        if (record == nullptr || !FiniteVector(position) || !FiniteVector(velocity))
        {
            return;
        }
        record->request.position = position;
        record->request.velocity = velocity;
        record->transformDirty = true;
    }

    void AudioRuntime::SetVoiceParameters(VoiceHandle handle, float volume, float pitch, int priority)
    {
        VoiceRecord* record = m_voices.Find(handle);
        if (record == nullptr)
        {
            return;
        }
        record->baseGain = FiniteGain(volume);
        record->priority = priority;
        record->request.volume = record->baseGain;
        record->request.pitch = std::isfinite(pitch) ? std::clamp(pitch, 0.01f, 4.0f) : 1.0f;
        record->request.priority = priority;
        RefreshGain(*record);
        PushGain(*record);
    }

    void AudioRuntime::SetVoiceGain(VoiceHandle handle, float linearGain)
    {
        VoiceRecord* record = m_voices.Find(handle);
        if (record == nullptr)
        {
            return;
        }
        record->userGain = FiniteGain(linearGain);
        PushGain(*record);
    }

    void AudioRuntime::SetVoiceSettings(VoiceHandle handle, const PlayRequest& input)
    {
        m_lastError.clear();
        VoiceRecord* record = m_voices.Find(handle);
        if (record == nullptr)
        {
            return;
        }
        PlayRequest request = Sanitize(input);
        if (request.spatialBlend > 0.0f && record->clipInfo.channels > 1u)
        {
            m_lastError = "Spatial settings were rejected: the retained clip generation is stereo; import a mono point source";
            ++m_metrics.rejectedUpdates;
            return;
        }
        // Clip identity and ownership remain bound to this handle. Bus changes
        // are re-admitted against the destination cap with the same playhead.
        request.clip = record->clip;
        request.ownerId = record->ownerId;
        request.concurrencyGroup = record->request.concurrencyGroup;
        const bool busChanged = request.bus != record->bus;
        if (busChanged && record->backendVoice.IsValid())
        {
            Virtualize(*record);
        }
        record->bus = request.bus;
        record->request = std::move(request);
        record->loop = record->request.loop;
        record->baseGain = record->request.volume;
        record->priority = record->request.priority;
        RefreshGain(*record);
        if (busChanged)
        {
            const std::uint64_t failuresBefore = m_metrics.backendFailures;
            if (!record->paused && !MakePhysical(handle, *record)
                && (!CanVirtualize(*record) || m_metrics.backendFailures != failuresBefore))
            {
                Stop(handle);
                ++m_metrics.dropped;
            }
        }
        else
        {
            PushGain(*record);
        }
    }

    void AudioRuntime::PushGain(const VoiceRecord& record)
    {
        if (record.backendVoice.IsValid())
        {
            PlayRequest projected = record.request;
            projected.volume = record.baseGain * record.userGain;
            m_backend.SetVoiceSettings(record.backendVoice, projected);
        }
    }

    void AudioRuntime::SetLooping(VoiceHandle handle, bool loop)
    {
        if (VoiceRecord* record = m_voices.Find(handle))
        {
            record->loop = loop;
            record->request.loop = loop;
            m_backend.SetVoiceLooping(record->backendVoice, loop);
        }
    }

    bool AudioRuntime::Seek(VoiceHandle handle, std::uint64_t frame)
    {
        VoiceRecord* record = m_voices.Find(handle);
        if (record == nullptr)
        {
            return false;
        }
        if (record->clipInfo.frameCount > 0u)
        {
            frame = record->loop ? frame % record->clipInfo.frameCount :
                std::min(frame, record->clipInfo.frameCount);
        }
        if (record->backendVoice.IsValid() && !m_backend.SeekVoice(record->backendVoice, frame))
        {
            return false;
        }
        record->playheadFrame = static_cast<double>(frame);
        return true;
    }

    std::uint64_t AudioRuntime::GetPlayhead(VoiceHandle handle) const
    {
        const VoiceRecord* record = m_voices.Find(handle);
        if (record == nullptr)
        {
            return 0u;
        }
        return record->backendVoice.IsValid() ? m_backend.VoicePlayhead(record->backendVoice) :
            static_cast<std::uint64_t>(record->playheadFrame);
    }

    VoiceState AudioRuntime::GetVoiceState(VoiceHandle handle) const
    {
        const VoiceRecord* record = m_voices.Find(handle);
        return record != nullptr ? record->state : VoiceState::Free;
    }

    void AudioRuntime::SetListener(const ListenerState& input)
    {
        if (!FiniteVector(input.position) || !FiniteVector(input.velocity)
            || !FiniteVector(input.forward) || !FiniteVector(input.up))
        {
            return;
        }
        m_listener = input;
        auto normalize = [](math::vector3& value, math::vector3 fallback)
        {
            const float length = std::sqrt(value.x * value.x + value.y * value.y + value.z * value.z);
            value = length > 0.00001f ? math::vector3{ value.x / length, value.y / length, value.z / length } : fallback;
        };
        normalize(m_listener.forward, { 0.0f, 0.0f, 1.0f });
        const float dot = m_listener.forward.x * m_listener.up.x +
            m_listener.forward.y * m_listener.up.y + m_listener.forward.z * m_listener.up.z;
        m_listener.up = { m_listener.up.x - dot * m_listener.forward.x,
            m_listener.up.y - dot * m_listener.forward.y, m_listener.up.z - dot * m_listener.forward.z };
        if (std::abs(m_listener.up.x) + std::abs(m_listener.up.y) + std::abs(m_listener.up.z) < 0.00001f)
        {
            m_listener.up = std::abs(m_listener.forward.y) < 0.9f ?
                math::vector3{ -m_listener.forward.y * m_listener.forward.x,
                    1.0f - m_listener.forward.y * m_listener.forward.y,
                    -m_listener.forward.y * m_listener.forward.z } :
                math::vector3{ 1.0f - m_listener.forward.x * m_listener.forward.x,
                    -m_listener.forward.x * m_listener.forward.y, -m_listener.forward.x * m_listener.forward.z };
        }
        normalize(m_listener.up, { 0.0f, 1.0f, 0.0f });
        m_backend.SetListener(m_listener);
        m_voices.ForEachAlive([](VoiceHandle, VoiceRecord& record)
        {
            record.settingsDirty = true;
        });
    }

    void AudioRuntime::SetBusVolume(BusId bus, float linearGain)
    {
        if (!bus.IsValid())
        {
            bus = Buses::Master;
        }
        const float gain = FiniteGain(linearGain);
        m_busVolumes[bus.value] = gain;
        m_backend.SetBusVolume(bus, gain);
        m_voices.ForEachAlive([&](VoiceHandle, VoiceRecord& record)
        {
            RefreshGain(record);
        });
    }

    float AudioRuntime::GetBusVolume(BusId bus) const
    {
        const auto found = m_busVolumes.find(bus.IsValid() ? bus.value : Buses::Master.value);
        return found != m_busVolumes.end() ? found->second : 1.0f;
    }

    void AudioRuntime::ConfigureBus(BusId bus, std::size_t cap, StealPolicy policy)
    {
        if (bus == Buses::Master || !bus.IsValid())
        {
            m_masterPolicy = policy;
            m_masterPolicyConfigured = true;
            SetPhysicalVoiceLimit(cap);
        }
        else
        {
            m_busLimits[bus.value] = { cap, policy };
            EnforceLimits();
        }
    }

    void AudioRuntime::ConfigureConcurrencyGroup(ConcurrencyGroupId group,
        std::size_t cap, StealPolicy policy)
    {
        if (!group.IsValid())
        {
            return;
        }
        m_concurrencyLimits[group.value] = { cap, policy };
        std::vector<VoiceHandle> handles;
        m_voices.ForEachAlive([&](VoiceHandle handle, const VoiceRecord& record)
        {
            if (record.request.concurrencyGroup.value == group.value)
            {
                handles.push_back(handle);
            }
        });
        while (handles.size() > cap)
        {
            const VoiceHandle victim = FindVictim({}, group,
                policy == StealPolicy::Reject ? StealPolicy::Oldest : policy, false);
            Stop(victim);
            ++m_metrics.stolen;
            handles.pop_back();
        }
    }

    void AudioRuntime::SetPhysicalVoiceLimit(std::size_t limit)
    {
        m_physicalLimit = std::min(limit, m_voices.Capacity());
        EnforceLimits();
    }

    void AudioRuntime::EnforceLimits()
    {
        auto reduce = [&](BusId bus, std::size_t cap, StealPolicy policy)
        {
            while (PhysicalCount(bus) > cap)
            {
                const VoiceHandle handle = FindVictim(bus, {},
                    policy == StealPolicy::Reject ? StealPolicy::LowestPriority : policy, true);
                VoiceRecord* record = m_voices.Find(handle);
                if (record == nullptr)
                {
                    break;
                }
                if (CanVirtualize(*record))
                {
                    Virtualize(*record);
                }
                else
                {
                    Stop(handle);
                    ++m_metrics.stolen;
                }
            }
        };
        reduce({}, m_physicalLimit, StealPolicy::LowestPriority);
        for (const auto& [bus, limit] : m_busLimits)
        {
            reduce(BusId{ bus }, limit.cap, limit.policy);
        }
    }

    void AudioRuntime::SetReverbPreset(ReverbPreset preset)
    {
        m_backend.SetReverbPreset(preset);
    }

    VoiceMetrics AudioRuntime::Metrics() const
    {
        VoiceMetrics result = m_metrics;
        result.active = m_voices.AliveCount();
        result.physical = 0u;
        result.virtualized = 0u;
        result.paused = 0u;
        m_voices.ForEachAlive([&](VoiceHandle, const VoiceRecord& record)
        {
            if (record.backendVoice.IsValid())
            {
                ++result.physical;
            }
            else
            {
                ++result.virtualized;
            }
            if (record.paused)
            {
                ++result.paused;
            }
        });
        return result;
    }

    bool AudioRuntime::LoadClip(const ClipKey& key, const std::filesystem::path& source)
    {
        if (key.IsEmpty() || !m_started || !m_backend.LoadClip(key, source))
        {
            return false;
        }
        m_clips.insert(key);
        return true;
    }

    bool AudioRuntime::LoadCookedClip(const experiment::cooked::CookedAudioClipSource& source)
    {
        const ClipKey key = ClipKey::FromGuid(source.Id().value);
        if (key.IsEmpty() || !m_started || !m_backend.LoadCookedClip(key, source))
        {
            return false;
        }
        m_clips.insert(key);
        return true;
    }

    void AudioRuntime::UnloadClip(const ClipKey& key)
    {
        m_voices.ForEachAlive([&](VoiceHandle handle, const VoiceRecord& record)
        {
            if (record.clip == key)
            {
                Stop(handle);
            }
        });
        m_backend.UnloadClip(key);
        m_clips.erase(key);
    }

    std::vector<ClipKey> AudioRuntime::ListClipKeys() const
    {
        std::vector<ClipKey> keys(m_clips.begin(), m_clips.end());
        std::sort(keys.begin(), keys.end(), [](const ClipKey& left, const ClipKey& right)
        {
            if (left.Text() != right.Text())
            {
                return left.Text() < right.Text();
            }
            return left.IsGuid() < right.IsGuid();
        });
        return keys;
    }

    void AudioRuntime::Update(float deltaSeconds)
    {
        if (!m_started)
        {
            return;
        }
        ++m_frame;
        const double elapsed = std::isfinite(deltaSeconds) ? std::max(0.0f, deltaSeconds) : 0.0;
        m_backend.Advance(deltaSeconds);
        std::vector<VoiceHandle> virtualVoices;
        m_voices.ForEachAlive([&](VoiceHandle handle, VoiceRecord& record)
        {
            if (record.transformDirty || record.settingsDirty)
            {
                RefreshGain(record);
                PushGain(record);
                record.settingsDirty = false;
                record.transformDirty = false;
            }
            if (record.paused)
            {
                return;
            }
            if (record.backendVoice.IsValid())
            {
                if (!m_backend.IsVoicePlaying(record.backendVoice))
                {
                    Stop(handle);
                    return;
                }
                if (CanVirtualize(record) && record.EffectiveGain() <= kSilentGain)
                {
                    Virtualize(record);
                }
            }
            else
            {
                record.playheadFrame += elapsed * record.clipInfo.sampleRate * record.request.pitch * record.request.dopplerPitch;
                if (record.clipInfo.frameCount > 0u && record.playheadFrame >= record.clipInfo.frameCount)
                {
                    if (!record.loop)
                    {
                        Stop(handle);
                        return;
                    }
                    record.playheadFrame = std::fmod(record.playheadFrame,
                        static_cast<double>(record.clipInfo.frameCount));
                }
            }
            if (!record.backendVoice.IsValid())
            {
                virtualVoices.push_back(handle);
            }
        });
        std::sort(virtualVoices.begin(), virtualVoices.end(), [&](VoiceHandle left, VoiceHandle right)
        {
            const VoiceRecord& a = *m_voices.Find(left);
            const VoiceRecord& b = *m_voices.Find(right);
            if (a.priority != b.priority)
            {
                return a.priority < b.priority;
            }
            if (a.EffectiveGain() != b.EffectiveGain())
            {
                return a.EffectiveGain() > b.EffectiveGain();
            }
            return left.index < right.index;
        });
        for (VoiceHandle handle : virtualVoices)
        {
            if (VoiceRecord* record = m_voices.Find(handle))
            {
                const std::uint64_t failuresBefore = m_metrics.backendFailures;
                if (!MakePhysical(handle, *record)
                    && (!CanVirtualize(*record) || m_metrics.backendFailures != failuresBefore))
                {
                    Stop(handle);
                    ++m_metrics.dropped;
                }
            }
        }
    }
}
