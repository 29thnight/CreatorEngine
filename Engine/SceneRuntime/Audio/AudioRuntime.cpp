#include "AudioRuntime.h"
#include "../../RenderEngine/Experiment/Cooked/CookedAudioClipSource.h"

#include <algorithm>

namespace wave
{
    AudioRuntime::AudioRuntime(AudioBackend& backend, std::size_t voiceCapacity,
        std::uint32_t generationNamespace)
        : m_backend(backend)
        , m_voices(voiceCapacity, generationNamespace)
    {
    }

    AudioRuntime::~AudioRuntime()
    {
        Shutdown();
    }

    bool AudioRuntime::Start(const DeviceSettings& settings)
    {
        if (m_started) return true;
        m_started = m_backend.Start(settings);
        return m_started;
    }

    void AudioRuntime::Shutdown()
    {
        if (!m_started) return;

        // 살아 있는 보이스를 먼저 건다. 표를 순회하며 지우면 안 되므로 핸들을
        // 모은 뒤 정지한다.
        std::vector<VoiceHandle> alive;
        alive.reserve(m_voices.AliveCount());
        m_voices.ForEachAlive([&alive](VoiceHandle handle, VoiceRecord&) { alive.push_back(handle); });
        for (const VoiceHandle handle : alive) Stop(handle);

        for (const ClipKey& key : m_clips)
        {
            m_backend.UnloadClip(key);
        }
        m_clips.clear();

        m_backend.Stop();
        m_started = false;
    }

    VoiceHandle AudioRuntime::Play(const PlayRequest& request)
    {
        if (!m_started) return VoiceHandle{};
        if (request.clip.IsEmpty()) return VoiceHandle{};

        // ★ 없는 클립은 무효 핸들이다. 옛 배선은 여기서 에러 로그만 남기고 진행해,
        //   부르는 쪽은 "재생했다" 와 "못 했다" 를 구분할 수 없었다.
        if (!m_backend.HasClip(request.clip)) return VoiceHandle{};

        const VoiceHandle handle = m_voices.Acquire(request, m_frame);
        if (!handle.IsValid()) return VoiceHandle{};

        const BackendVoiceId backendVoice = m_backend.StartVoice(request);
        if (!backendVoice.IsValid())
        {
            // 백엔드가 못 내면 슬롯을 되돌린다 — 표에 유령이 남으면 cap 이 조용히 준다.
            m_voices.Release(handle);
            return VoiceHandle{};
        }

        VoiceRecord* const record = m_voices.Find(handle);
        record->backendVoice = backendVoice;
        record->state = VoiceState::Physical;
        return handle;
    }

    void AudioRuntime::Stop(VoiceHandle handle)
    {
        VoiceRecord* const record = m_voices.Find(handle);
        if (nullptr == record) return;

        m_backend.StopVoice(record->backendVoice);
        m_voices.Release(handle);
    }

    void AudioRuntime::SetPaused(VoiceHandle handle, bool paused)
    {
        VoiceRecord* const record = m_voices.Find(handle);
        if (nullptr == record) return;

        m_backend.SetVoicePaused(record->backendVoice, paused);
        record->state = paused ? VoiceState::Paused : VoiceState::Physical;
    }

    bool AudioRuntime::IsAlive(VoiceHandle handle) const
    {
        return m_voices.IsAlive(handle);
    }

    void AudioRuntime::StopByOwner(std::uint64_t ownerId)
    {
        if (0u == ownerId) return;

        std::vector<VoiceHandle> targets;
        m_voices.ForEachAlive([&targets, ownerId](VoiceHandle handle, VoiceRecord& record)
        {
            if (record.ownerId == ownerId) targets.push_back(handle);
        });
        for (const VoiceHandle handle : targets) Stop(handle);
    }

    void AudioRuntime::SetVoiceTransform(VoiceHandle handle,
        const math::vector3& position, const math::vector3& velocity)
    {
        const VoiceRecord* const record = m_voices.Find(handle);
        if (nullptr == record) return;
        m_backend.SetVoiceTransform(record->backendVoice, position, velocity);
    }

    void AudioRuntime::SetVoiceParameters(VoiceHandle handle,
        float volume, float pitch, int priority)
    {
        VoiceRecord* const record = m_voices.Find(handle);
        if (nullptr == record) return;

        record->baseGain = volume;
        record->priority = priority;
        PushGain(*record);
        m_backend.SetVoicePitch(record->backendVoice, pitch);
    }

    void AudioRuntime::SetVoiceGain(VoiceHandle handle, float linearGain)
    {
        VoiceRecord* const record = m_voices.Find(handle);
        if (nullptr == record) return;

        record->attenuationGain = linearGain;
        PushGain(*record);
    }

    void AudioRuntime::PushGain(const VoiceRecord& record)
    {
        m_backend.SetVoiceVolume(record.backendVoice, record.EffectiveGain());
    }

    void AudioRuntime::SetListener(const ListenerState& listener)
    {
        m_backend.SetListener(listener);
    }

    bool AudioRuntime::LoadClip(const ClipKey& key, const std::filesystem::path& source)
    {
        if (key.IsEmpty()) return false;
        if (!m_backend.LoadClip(key, source)) return false;

        m_clips.insert(key);
        return true;
    }

    bool AudioRuntime::LoadCookedClip(
        const experiment::cooked::CookedAudioClipSource& source)
    {
        const ClipKey key = ClipKey::FromGuid(source.Id().value);
        if (key.IsEmpty() || !m_started) return false;
        if (!m_backend.LoadCookedClip(key, source)) return false;
        m_clips.insert(key);
        return true;
    }

    void AudioRuntime::UnloadClip(const ClipKey& key)
    {
        // ★ 울리고 있는 보이스를 먼저 끊는다. 자원을 먼저 놓으면 백엔드가 해제된
        //   것을 읽는다 — 옛 배선이 정확히 그 모양이었다(맵 잠금이 클립 수명을
        //   지키지 않아, 락 밖에서 재생하는 사이 로더 스레드가 release 할 수 있었다).
        std::vector<VoiceHandle> targets;
        m_voices.ForEachAlive([&targets, &key](VoiceHandle handle, VoiceRecord& record)
        {
            if (record.clip == key) targets.push_back(handle);
        });
        for (const VoiceHandle handle : targets) Stop(handle);

        m_backend.UnloadClip(key);
        m_clips.erase(key);
    }

    std::vector<ClipKey> AudioRuntime::ListClipKeys() const
    {
        std::vector<ClipKey> keys;
        keys.reserve(m_clips.size());
        for (const ClipKey& key : m_clips)
        {
            keys.push_back(key);
        }
        std::sort(keys.begin(), keys.end(), [](const ClipKey& left, const ClipKey& right)
        {
            if (left.Text() != right.Text()) return left.Text() < right.Text();
            return left.IsGuid() < right.IsGuid();
        });
        return keys;
    }

    void AudioRuntime::Update(float deltaSeconds)
    {
        (void)deltaSeconds;
        ++m_frame;
        m_backend.Update();

        // 자연 종료 회수. 백엔드가 "끝났다" 고 답한 보이스의 슬롯을 돌려준다.
        //
        // ★ 일시정지는 회수 대상이 아니다. 백엔드에게 "울리고 있는가" 를 물으면
        //   pause 도 아니라고 답하는데, 그것을 종료로 읽으면 pause 한 소리가
        //   조용히 사라진다.
        std::vector<VoiceHandle> finished;
        m_voices.ForEachAlive([this, &finished](VoiceHandle handle, VoiceRecord& record)
        {
            if (VoiceState::Paused == record.state) return;
            if (!m_backend.IsVoicePlaying(record.backendVoice)) finished.push_back(handle);
        });
        for (const VoiceHandle handle : finished) Stop(handle);
    }
}
