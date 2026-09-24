#include "NullAudioBackend.h"
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
        m_clips.clear();
        m_busVolumes.clear();
        m_running = false;
    }

    bool NullAudioBackend::LoadClip(const ClipKey& key, const std::filesystem::path& source)
    {
        (void)source;
        if (key.IsEmpty()) return false;
        m_clips.insert(key);
        return true;
    }

    bool NullAudioBackend::LoadCookedClip(const ClipKey& key,
        const experiment::cooked::CookedAudioClipSource& source)
    {
        if (!m_running || !key.IsGuid()
            || key != ClipKey::FromGuid(source.Id().value)
            || source.Metadata().loadMode == experiment::cooked::AudioLoadMode::Stream)
            return false;
        m_clips.insert(key);
        return true;
    }

    void NullAudioBackend::UnloadClip(const ClipKey& key)
    {
        m_clips.erase(key);
    }

    bool NullAudioBackend::HasClip(const ClipKey& key) const
    {
        return m_clips.find(key) != m_clips.end();
    }

    BackendVoiceId NullAudioBackend::StartVoice(const PlayRequest& request)
    {
        // 가드보다 먼저 센다 — 이 값이 재는 것은 "성공했는가" 가 아니라
        // "요청이 여기까지 왔는가" 다.
        ++m_startAttempts;

        if (!m_running) return BackendVoiceId{};
        if (!HasClip(request.clip)) return BackendVoiceId{};

        Voice voice{};
        voice.clip = request.clip;
        voice.bus = request.bus;
        voice.volume = request.volume;
        voice.pitch = request.pitch;
        voice.position = request.position;
        voice.velocity = request.velocity;
        voice.playing = true;

        m_voices.push_back(voice);
        return BackendVoiceId{ static_cast<std::uint64_t>(m_voices.size()) };
    }

    NullAudioBackend::Voice* NullAudioBackend::Resolve(BackendVoiceId voice) noexcept
    {
        const Voice* const resolved = static_cast<const NullAudioBackend*>(this)->Resolve(voice);
        return const_cast<Voice*>(resolved);
    }

    const NullAudioBackend::Voice* NullAudioBackend::Resolve(BackendVoiceId voice) const noexcept
    {
        if (!voice.IsValid()) return nullptr;
        if (voice.value > m_voices.size()) return nullptr;
        return &m_voices[static_cast<std::size_t>(voice.value - 1u)];
    }

    void NullAudioBackend::StopVoice(BackendVoiceId voice)
    {
        Voice* const resolved = Resolve(voice);
        if (nullptr == resolved) return;
        resolved->playing = false;
        resolved->paused = false;
    }

    void NullAudioBackend::SetVoicePaused(BackendVoiceId voice, bool paused)
    {
        Voice* const resolved = Resolve(voice);
        if (nullptr == resolved) return;
        if (!resolved->playing) return;
        resolved->paused = paused;
    }

    bool NullAudioBackend::IsVoicePlaying(BackendVoiceId voice) const
    {
        const Voice* const resolved = Resolve(voice);
        if (nullptr == resolved) return false;
        return resolved->playing && !resolved->paused;
    }

    void NullAudioBackend::SetVoiceVolume(BackendVoiceId voice, float linearGain)
    {
        Voice* const resolved = Resolve(voice);
        if (nullptr == resolved) return;
        resolved->volume = linearGain;
    }

    void NullAudioBackend::SetVoicePitch(BackendVoiceId voice, float pitch)
    {
        Voice* const resolved = Resolve(voice);
        if (nullptr == resolved) return;
        resolved->pitch = pitch;
    }

    void NullAudioBackend::SetVoiceTransform(BackendVoiceId voice,
        const math::vector3& position, const math::vector3& velocity)
    {
        Voice* const resolved = Resolve(voice);
        if (nullptr == resolved) return;
        resolved->position = position;
        resolved->velocity = velocity;
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
        // ★ 끝난 보이스를 여기서 회수하지 않는다. Null 은 길이를 모른다 — 시간을
        //   흉내 내면 "언제 끝나는가" 가 구현마다 달라져 판정이 흔들린다. 루프가
        //   아닌 소리의 자연 종료는 실제 백엔드의 관측으로만 판정한다.
    }

    std::size_t NullAudioBackend::PlayingCount() const noexcept
    {
        std::size_t count = 0;
        for (const Voice& voice : m_voices)
        {
            if (voice.playing && !voice.paused) ++count;
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
