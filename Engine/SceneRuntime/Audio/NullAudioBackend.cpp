#include "NullAudioBackend.h"

#include <algorithm>
#include <cmath>
#include "../../RenderEngine/Experiment/Cooked/CookedAudioClipSource.h"

namespace wave
{
    bool NullAudioBackend::Start(const DeviceSettings& settings)
    {
        m_settings = settings;
        m_running = true;
        return true;
    }

    void NullAudioBackend::Stop()
    {
        // ★ 정지는 "울리던 것을 실제로 끈다" 는 뜻이어야 한다. 플래그만 내리면
        //   상위가 Stop 뒤에도 살아 있는 보이스를 보게 되고, 그 차이가 실제
        //   백엔드에서만 드러나면 Null 로 잡을 수 있던 결함을 놓친다.
        for (Voice& voice : m_voices)
        {
            voice.playing = false;
            voice.paused = false;
        }
        m_voices.clear();
        m_freeVoices.clear();
        m_clips.clear();
        m_clipInfo.clear();
        m_clipLeases.clear();
        m_busVolumes.clear();
        m_running = false;
    }

    bool NullAudioBackend::LoadClip(const ClipKey& key, const std::filesystem::path& source)
    {
        (void)source;
        if (key.IsEmpty())
        {
            return false;
        }
        m_clips.insert(key);
        return true;
    }

    bool NullAudioBackend::LoadCookedClip(const ClipKey& key,
        const experiment::cooked::CookedAudioClipSource& source)
    {
        if (!m_running || !key.IsGuid()
            || key != ClipKey::FromGuid(source.Id().value))
        {
            return false;
        }
        m_clips.insert(key);
        const auto& metadata = source.Metadata();
        m_clipInfo[key] = { metadata.channels, metadata.sampleRate, metadata.frameCount,
            metadata.loadMode == experiment::cooked::AudioLoadMode::Stream };
        return true;
    }

    BackendClipId NullAudioBackend::RetainClip(const ClipKey& key)
    {
        if (!HasClip(key))
        {
            return {};
        }
        const std::uint64_t lease = m_nextClipLease++;
        m_clipLeases.emplace(lease, GetClipInfo(key));
        return BackendClipId{ lease };
    }

    void NullAudioBackend::ReleaseClip(BackendClipId clip)
    {
        m_clipLeases.erase(clip.value);
    }

    void NullAudioBackend::UnloadClip(const ClipKey& key)
    {
        m_clips.erase(key);
        m_clipInfo.erase(key);
    }

    bool NullAudioBackend::HasClip(const ClipKey& key) const
    {
        return m_clips.find(key) != m_clips.end();
    }

    BackendVoiceId NullAudioBackend::StartVoice(const PlayRequest& request)
    {
        return StartVoice(request, {});
    }

    BackendVoiceId NullAudioBackend::StartVoice(const PlayRequest& request, BackendClipId retained)
    {
        // 가드보다 먼저 센다 — 이 값이 재는 것은 "성공했는가" 가 아니라
        // "요청이 여기까지 왔는가" 다.
        ++m_startAttempts;

        if (!m_running)
        {
            return BackendVoiceId{};
        }
        const auto lease = m_clipLeases.find(retained.value);
        if (retained.IsValid() ? lease == m_clipLeases.end() : !HasClip(request.clip))
        {
            return BackendVoiceId{};
        }

        Voice voice{};
        voice.info = retained.IsValid() ? lease->second : GetClipInfo(request.clip);
        voice.active = true;
        voice.clip = request.clip;
        voice.bus = request.bus;
        voice.volume = request.volume;
        voice.pitch = request.pitch * request.dopplerPitch;
        voice.position = request.position;
        voice.velocity = request.velocity;
        voice.playing = true;
        voice.loop = request.loop;

        if (!m_freeVoices.empty())
        {
            const std::size_t index = m_freeVoices.back();
            m_freeVoices.pop_back();
            m_voices[index] = std::move(voice);
            return BackendVoiceId{ index + 1u };
        }
        m_voices.push_back(std::move(voice));
        return BackendVoiceId{ static_cast<std::uint64_t>(m_voices.size()) };
    }

    NullAudioBackend::Voice* NullAudioBackend::Resolve(BackendVoiceId voice) noexcept
    {
        const Voice* const resolved = static_cast<const NullAudioBackend*>(this)->Resolve(voice);
        return const_cast<Voice*>(resolved);
    }

    const NullAudioBackend::Voice* NullAudioBackend::Resolve(BackendVoiceId voice) const noexcept
    {
        if (!voice.IsValid())
        {
            return nullptr;
        }
        if (voice.value > m_voices.size())
        {
            return nullptr;
        }
        return &m_voices[static_cast<std::size_t>(voice.value - 1u)];
    }

    void NullAudioBackend::StopVoice(BackendVoiceId voice)
    {
        Voice* const resolved = Resolve(voice);
        if (nullptr == resolved)
        {
            return;
        }
        if (!resolved->active)
        {
            return;
        }
        *resolved = {};
        m_freeVoices.push_back(static_cast<std::size_t>(voice.value - 1u));
    }

    void NullAudioBackend::SetVoicePaused(BackendVoiceId voice, bool paused)
    {
        Voice* const resolved = Resolve(voice);
        if (nullptr == resolved)
        {
            return;
        }
        if (!resolved->playing)
        {
            return;
        }
        resolved->paused = paused;
    }

    bool NullAudioBackend::IsVoicePlaying(BackendVoiceId voice) const
    {
        const Voice* const resolved = Resolve(voice);
        if (nullptr == resolved)
        {
            return false;
        }
        return resolved->playing && !resolved->paused;
    }

    void NullAudioBackend::SetVoiceVolume(BackendVoiceId voice, float linearGain)
    {
        Voice* const resolved = Resolve(voice);
        if (nullptr == resolved)
        {
            return;
        }
        resolved->volume = linearGain;
    }

    void NullAudioBackend::SetVoicePitch(BackendVoiceId voice, float pitch)
    {
        Voice* const resolved = Resolve(voice);
        if (nullptr == resolved)
        {
            return;
        }
        resolved->pitch = pitch;
    }

    void NullAudioBackend::SetVoiceTransform(BackendVoiceId voice,
        const math::vector3& position, const math::vector3& velocity)
    {
        Voice* const resolved = Resolve(voice);
        if (nullptr == resolved)
        {
            return;
        }
        resolved->position = position;
        resolved->velocity = velocity;
    }

    void NullAudioBackend::SetVoiceSettings(BackendVoiceId voice, const PlayRequest& request)
    {
        SetVoiceVolume(voice, request.volume);
        SetVoicePitch(voice, request.pitch * request.dopplerPitch);
        SetVoiceTransform(voice, request.position, request.velocity);
        SetVoiceLooping(voice, request.loop);
    }

    void NullAudioBackend::SetVoiceLooping(BackendVoiceId voice, bool loop)
    {
        if (Voice* resolved = Resolve(voice))
        {
            resolved->loop = loop;
        }
    }

    bool NullAudioBackend::SeekVoice(BackendVoiceId voice, std::uint64_t frame)
    {
        if (Voice* resolved = Resolve(voice))
        {
            resolved->playhead = frame;
            return true;
        }
        return false;
    }

    std::uint64_t NullAudioBackend::VoicePlayhead(BackendVoiceId voice) const
    {
        const Voice* resolved = Resolve(voice);
        return resolved != nullptr ? static_cast<std::uint64_t>(resolved->playhead) : 0u;
    }

    ClipInfo NullAudioBackend::GetClipInfo(const ClipKey& key) const
    {
        const auto found = m_clipInfo.find(key);
        return found != m_clipInfo.end() ? found->second : ClipInfo{};
    }

    void NullAudioBackend::SetBusVolume(BusId bus, float linearGain)
    {
        m_busVolumes[bus.value] = linearGain;
    }

    void NullAudioBackend::SetListener(const ListenerState& listener)
    {
        m_listener = listener;
    }

    void NullAudioBackend::Update()
    {
        // Hosts supply deterministic elapsed time through Advance().
    }

    void NullAudioBackend::Advance(float deltaSeconds)
    {
        const double elapsed = std::isfinite(deltaSeconds) ? std::max(0.0f, deltaSeconds) : 0.0;
        for (Voice& voice : m_voices)
        {
            if (!voice.active || !voice.playing || voice.paused)
            {
                continue;
            }
            voice.playhead += elapsed * voice.info.sampleRate * voice.pitch;
            if (voice.info.frameCount > 0u && voice.playhead >= voice.info.frameCount)
            {
                if (voice.loop)
                {
                    voice.playhead = std::fmod(voice.playhead, static_cast<double>(voice.info.frameCount));
                }
                else
                {
                    voice.playhead = static_cast<double>(voice.info.frameCount);
                    voice.playing = false;
                }
            }
        }
    }

    std::size_t NullAudioBackend::PlayingCount() const noexcept
    {
        std::size_t count = 0;
        for (const Voice& voice : m_voices)
        {
            if (voice.playing && !voice.paused)
            {
                ++count;
            }
        }
        return count;
    }

    float NullAudioBackend::VoiceVolume(BackendVoiceId voice) const noexcept
    {
        const Voice* const resolved = Resolve(voice);
        return (nullptr == resolved) ? 0.0f : resolved->volume;
    }

    float NullAudioBackend::VoicePitch(BackendVoiceId voice) const noexcept
    {
        const Voice* const resolved = Resolve(voice);
        return (nullptr == resolved) ? 0.0f : resolved->pitch;
    }
}
