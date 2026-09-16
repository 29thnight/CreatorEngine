#include "MiniaudioBackend.h"

#include <deque>
#include <unordered_map>
#include <vector>

// ★ miniaudio 를 여는 **유일한 자리**다. 구현까지 여기서 만든다
//   (`MINIAUDIO_IMPLEMENTATION`). 벤더의 `miniaudio.c` 는 쓰지 않는다 — 같은 일을
//   하는 번역 단위가 둘이 되기 때문이다(PROVENANCE.md 통합 규약).
//
// ★★ 경고 수준을 0 으로 내리고 연다. 우리 코드는 /W4 /WX 로 짓는데 벤더 소스의
//   경고까지 눈금에 넣으면 고칠 수 없는 것으로 게이트가 붉어진다.
#define MINIAUDIO_IMPLEMENTATION
#define MA_NO_ENCODING          // 쓰기 경로가 없다. 디코더(WAV/MP3/FLAC)만 남긴다.
#pragma warning(push, 0)
#include "../../../ThirdParty/miniaudio/miniaudio.h"
#pragma warning(pop)

namespace wave
{
    namespace
    {
        [[nodiscard]] ma_vec3f ToVector(const math::vector3& value) noexcept
        {
            ma_vec3f out{};
            out.x = value.x;
            out.y = value.y;
            out.z = value.z;
            return out;
        }
    }

    struct MiniaudioBackend::Implementation final
    {
        // 슬롯 하나가 백엔드 보이스 하나다.
        //
        // ★ `std::deque` 를 쓴다. `ma_sound` 는 **주소가 고정돼야 한다**(엔진의 노드
        //   그래프가 그 주소를 물고 있다). vector 는 재할당에서 옮기므로 쓸 수 없다.
        struct VoiceSlot final
        {
            ma_sound sound{};
            bool initialized{ false };
            bool paused{ false };
        };

        ma_engine engine{};
        bool engineReady{ false };

        std::unordered_map<ClipKey, std::filesystem::path> clips;
        std::unordered_map<std::uint16_t, std::unique_ptr<ma_sound_group>> busGroups;

        std::deque<VoiceSlot> voices;
        std::vector<std::size_t> freeVoices;

        std::string lastError;
        DeviceSettings actual{};

        [[nodiscard]] VoiceSlot* Resolve(BackendVoiceId voice) noexcept
        {
            if (!voice.IsValid()) return nullptr;
            const std::size_t index = static_cast<std::size_t>(voice.value - 1u);
            if (index >= voices.size()) return nullptr;
            VoiceSlot& slot = voices[index];
            return slot.initialized ? &slot : nullptr;
        }

        void ReleaseSlot(std::size_t index)
        {
            VoiceSlot& slot = voices[index];
            if (!slot.initialized) return;

            ma_sound_stop(&slot.sound);
            ma_sound_uninit(&slot.sound);
            slot.initialized = false;
            slot.paused = false;
            freeVoices.push_back(index);
        }

        [[nodiscard]] ma_sound_group* GroupFor(BusId bus)
        {
            if (!bus.IsValid()) return nullptr;

            const auto found = busGroups.find(bus.value);
            if (found != busGroups.end()) return found->second.get();

            auto group = std::make_unique<ma_sound_group>();
            if (MA_SUCCESS != ma_sound_group_init(&engine, 0, nullptr, group.get()))
            {
                // 버스를 못 만들면 마스터로 붙인다 — 소리가 사라지는 것보다 낫다.
                return nullptr;
            }
            ma_sound_group* const raw = group.get();
            busGroups.emplace(bus.value, std::move(group));
            return raw;
        }
    };

    MiniaudioBackend::MiniaudioBackend()
        : m_implementation(std::make_unique<Implementation>())
    {
    }

    MiniaudioBackend::~MiniaudioBackend()
    {
        Stop();
    }

    bool MiniaudioBackend::Start(const DeviceSettings& settings)
    {
        Implementation& state = *m_implementation;
        if (state.engineReady) return true;

        ma_engine_config config = ma_engine_config_init();
        config.sampleRate = settings.sampleRate;
        config.channels = settings.channels;

        const ma_result result = ma_engine_init(&config, &state.engine);
        if (MA_SUCCESS != result)
        {
            // ★ 벤더 코드 숫자를 그대로 흘리지 않는다. 부르는 쪽이 처분을 정할 수
            //   있는 문장으로 옮긴다 — 여기서 갈리는 처분은 "Null 로 degrade" 다.
            state.lastError = std::string("오디오 장치를 열지 못했다: ") +
                ma_result_description(result);
            return false;
        }

        state.actual.sampleRate = ma_engine_get_sample_rate(&state.engine);
        state.actual.channels = ma_engine_get_channels(&state.engine);
        state.engineReady = true;
        state.lastError.clear();
        return true;
    }

    void MiniaudioBackend::Stop()
    {
        Implementation& state = *m_implementation;
        if (!state.engineReady) return;

        // 순서가 중요하다: 보이스 → 버스 그룹 → 엔진. 뒤집으면 해제된 그래프를 만진다.
        for (std::size_t index = 0; index < state.voices.size(); ++index)
        {
            state.ReleaseSlot(index);
        }
        state.voices.clear();
        state.freeVoices.clear();

        for (auto& [busValue, group] : state.busGroups)
        {
            (void)busValue;
            ma_sound_group_uninit(group.get());
        }
        state.busGroups.clear();

        ma_engine_uninit(&state.engine);
        state.engineReady = false;
        state.clips.clear();
    }

    bool MiniaudioBackend::IsRunning() const
    {
        return m_implementation->engineReady;
    }

    bool MiniaudioBackend::LoadClip(const ClipKey& key, const std::filesystem::path& source)
    {
        Implementation& state = *m_implementation;
        if (!state.engineReady) return false;
        if (key.IsEmpty()) return false;

        // ★ 경로를 적어 두는 것으로 끝내지 않는다. 한 번 열어 **디코드 가능한지**
        //   확인하고 닫는다. 그래야 손상 파일이 "적재 성공, 재생 실패" 로 뒤늦게
        //   드러나지 않는다. 이 왕복이 resource manager 캐시도 데워 둔다.
        ma_sound probe{};
        const std::string path = source.string();
        const ma_result result = ma_sound_init_from_file(&state.engine, path.c_str(),
            MA_SOUND_FLAG_DECODE | MA_SOUND_FLAG_NO_SPATIALIZATION, nullptr, nullptr, &probe);
        if (MA_SUCCESS != result)
        {
            state.lastError = std::string("클립을 디코드하지 못했다: ") + path + " — " +
                ma_result_description(result);
            return false;
        }
        ma_sound_uninit(&probe);

        state.clips[key] = source;
        return true;
    }

    void MiniaudioBackend::UnloadClip(const ClipKey& key)
    {
        m_implementation->clips.erase(key);
    }

    bool MiniaudioBackend::HasClip(const ClipKey& key) const
    {
        const Implementation& state = *m_implementation;
        return state.clips.find(key) != state.clips.end();
    }

    BackendVoiceId MiniaudioBackend::StartVoice(const PlayRequest& request)
    {
        Implementation& state = *m_implementation;
        if (!state.engineReady) return BackendVoiceId{};

        const auto clip = state.clips.find(request.clip);
        if (clip == state.clips.end()) return BackendVoiceId{};

        std::size_t index = 0;
        if (!state.freeVoices.empty())
        {
            index = state.freeVoices.back();
            state.freeVoices.pop_back();
        }
        else
        {
            state.voices.emplace_back();
            index = state.voices.size() - 1u;
        }

        Implementation::VoiceSlot& slot = state.voices[index];
        const bool spatial = request.spatialBlend > 0.0f;
        const ma_uint32 flags = spatial
            ? static_cast<ma_uint32>(0)
            : static_cast<ma_uint32>(MA_SOUND_FLAG_NO_SPATIALIZATION);

        const std::string path = clip->second.string();
        const ma_result result = ma_sound_init_from_file(&state.engine, path.c_str(),
            flags, state.GroupFor(request.bus), nullptr, &slot.sound);
        if (MA_SUCCESS != result)
        {
            state.lastError = std::string("보이스를 만들지 못했다: ") + path + " — " +
                ma_result_description(result);
            state.freeVoices.push_back(index);
            return BackendVoiceId{};
        }
        slot.initialized = true;
        slot.paused = false;

        ma_sound_set_volume(&slot.sound, request.volume);
        ma_sound_set_pitch(&slot.sound, request.pitch);
        ma_sound_set_looping(&slot.sound, request.loop ? MA_TRUE : MA_FALSE);

        if (spatial)
        {
            ma_sound_set_position(&slot.sound, request.position.x, request.position.y,
                request.position.z);
            ma_sound_set_velocity(&slot.sound, request.velocity.x, request.velocity.y,
                request.velocity.z);
            ma_sound_set_min_distance(&slot.sound, request.minimumDistance);
            ma_sound_set_max_distance(&slot.sound, request.maximumDistance);
            ma_sound_set_attenuation_model(&slot.sound,
                (RolloffKind::Linear == request.rolloff) ? ma_attenuation_model_linear
                                                         : ma_attenuation_model_inverse);
        }

        if (MA_SUCCESS != ma_sound_start(&slot.sound))
        {
            state.ReleaseSlot(index);
            state.lastError = "보이스를 시작하지 못했다: " + path;
            return BackendVoiceId{};
        }
        return BackendVoiceId{ static_cast<std::uint64_t>(index + 1u) };
    }

    void MiniaudioBackend::StopVoice(BackendVoiceId voice)
    {
        Implementation& state = *m_implementation;
        if (nullptr == state.Resolve(voice)) return;
        state.ReleaseSlot(static_cast<std::size_t>(voice.value - 1u));
    }

    void MiniaudioBackend::SetVoicePaused(BackendVoiceId voice, bool paused)
    {
        Implementation::VoiceSlot* const slot = m_implementation->Resolve(voice);
        if (nullptr == slot) return;

        // ★ miniaudio 의 stop 은 되감지 않는다 — 재생 위치를 유지한 채 멈춘다.
        //   그래서 pause 는 stop, resume 은 start 다. 다만 그 상태에서 엔진에게
        //   "울리고 있는가" 를 물으면 아니라고 답하므로, 자연 종료와 구분하려면
        //   여기서 따로 표시해야 한다.
        if (paused)
        {
            ma_sound_stop(&slot->sound);
            slot->paused = true;
            return;
        }
        slot->paused = false;
        ma_sound_start(&slot->sound);
    }

    bool MiniaudioBackend::IsVoicePlaying(BackendVoiceId voice) const
    {
        Implementation::VoiceSlot* const slot = m_implementation->Resolve(voice);
        if (nullptr == slot) return false;
        if (slot->paused) return false;
        return MA_TRUE == ma_sound_is_playing(&slot->sound);
    }

    void MiniaudioBackend::SetVoiceVolume(BackendVoiceId voice, float linearGain)
    {
        Implementation::VoiceSlot* const slot = m_implementation->Resolve(voice);
        if (nullptr == slot) return;
        ma_sound_set_volume(&slot->sound, linearGain);
    }

    void MiniaudioBackend::SetVoicePitch(BackendVoiceId voice, float pitch)
    {
        Implementation::VoiceSlot* const slot = m_implementation->Resolve(voice);
        if (nullptr == slot) return;
        ma_sound_set_pitch(&slot->sound, pitch);
    }

    void MiniaudioBackend::SetVoiceTransform(BackendVoiceId voice,
        const math::vector3& position, const math::vector3& velocity)
    {
        Implementation::VoiceSlot* const slot = m_implementation->Resolve(voice);
        if (nullptr == slot) return;
        ma_sound_set_position(&slot->sound, position.x, position.y, position.z);
        ma_sound_set_velocity(&slot->sound, velocity.x, velocity.y, velocity.z);
    }

    void MiniaudioBackend::SetBusVolume(BusId bus, float linearGain)
    {
        Implementation& state = *m_implementation;
        if (!state.engineReady) return;

        ma_sound_group* const group = state.GroupFor(bus);
        if (nullptr == group) return;
        ma_sound_group_set_volume(group, linearGain);
    }

    void MiniaudioBackend::SetListener(const ListenerState& listener)
    {
        Implementation& state = *m_implementation;
        if (!state.engineReady) return;

        const ma_vec3f position = ToVector(listener.position);
        const ma_vec3f velocity = ToVector(listener.velocity);
        const ma_vec3f forward = ToVector(listener.forward);
        const ma_vec3f up = ToVector(listener.up);

        ma_engine_listener_set_position(&state.engine, 0, position.x, position.y, position.z);
        ma_engine_listener_set_velocity(&state.engine, 0, velocity.x, velocity.y, velocity.z);
        ma_engine_listener_set_direction(&state.engine, 0, forward.x, forward.y, forward.z);
        ma_engine_listener_set_world_up(&state.engine, 0, up.x, up.y, up.z);
    }

    void MiniaudioBackend::Update()
    {
        // miniaudio 는 자기 스레드에서 장치를 돌린다. 여기서 할 일이 없다.
        //
        // ★ 끝난 보이스 회수를 여기서 하지 않는다. 그 판단은 상위(AudioRuntime)가
        //   보이스 표를 보고 한다 — 백엔드가 표를 모르는 채로 자원을 놓으면 상위의
        //   핸들이 가리키는 대상이 조용히 사라진다.
    }

    const std::string& MiniaudioBackend::LastError() const noexcept
    {
        return m_implementation->lastError;
    }

    DeviceSettings MiniaudioBackend::ActualSettings() const noexcept
    {
        return m_implementation->actual;
    }
}
