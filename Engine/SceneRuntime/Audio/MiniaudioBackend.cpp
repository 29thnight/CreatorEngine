#include "MiniaudioBackend.h"
#include "../../RenderEngine/Experiment/Cooked/CookedAudioClipSource.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <deque>
#include <limits>
#include <unordered_map>
#include <vector>

#if defined(WAVE_AUDIO_PROBE)
#include "../../../Tools/regression/audio_fixture_decoder.h"
#endif

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
#if defined(WAVE_AUDIO_PROBE)
    namespace probe
    {
        bool DecodePcmS16Mono48k(const std::filesystem::path& source,
            std::size_t wantedFrames, std::vector<std::int16_t>& samples,
            std::uint64_t& length)
        {
            ma_decoder decoder{};
            const ma_decoder_config config = ma_decoder_config_init(ma_format_s16, 1u, 48000u);
            if (MA_SUCCESS != ma_decoder_init_file(source.string().c_str(), &config, &decoder))
                return false;
            length = 0u;
            (void)ma_decoder_get_length_in_pcm_frames(&decoder, &length);
            samples.assign(wantedFrames, 0);
            ma_uint64 framesRead = 0u;
            const ma_result read = ma_decoder_read_pcm_frames(&decoder, samples.data(),
                static_cast<ma_uint64>(wantedFrames), &framesRead);
            ma_decoder_uninit(&decoder);
            samples.resize(static_cast<std::size_t>(framesRead));
            return (MA_SUCCESS == read || MA_AT_END == read) && framesRead == wantedFrames;
        }
    }
#endif

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
        static constexpr std::uint64_t kHistogramStepNs = 10'000u;
        static constexpr std::size_t kHistogramBins = 2049u;
        static constexpr std::uint64_t kResidentPcmBudgetBytes = 64u * 1024u * 1024u;

        struct ResidentClip final
        {
            std::vector<float> pcm;
            ma_uint32 channels{};
            ma_uint32 sampleRate{};
            ma_uint64 frames{};
        };

        // 슬롯 하나가 백엔드 보이스 하나다.
        //
        // ★ `std::deque` 를 쓴다. `ma_sound` 는 **주소가 고정돼야 한다**(엔진의 노드
        //   그래프가 그 주소를 물고 있다). vector 는 재할당에서 옮기므로 쓸 수 없다.
        struct VoiceSlot final
        {
            ma_sound sound{};
            ma_audio_buffer buffer{};
            std::shared_ptr<ResidentClip> resident;
            bool initialized{ false };
            bool bufferInitialized{ false };
            bool paused{ false };
        };

        ma_engine engine{};
        bool engineReady{ false };
        bool profileCallbacks{ false };
        std::array<std::atomic<std::uint64_t>, kHistogramBins> callbackHistogram{};
        std::atomic<std::uint64_t> callbackNanoseconds{ 0 };
        std::atomic<std::uint64_t> callbackMaximumNs{ 0 };
        std::atomic<std::uint64_t> callbacksOverHalfPeriod{ 0 };
        std::atomic<std::uint64_t> callbacksOverFullPeriod{ 0 };
        std::atomic<std::uint32_t> callbackMinimumFrames{ std::numeric_limits<std::uint32_t>::max() };
        std::atomic<std::uint32_t> callbackMaximumFrames{ 0 };
        AudioDeviceDiagnostics deviceDiagnostics{};

        void ResetCallbackMetrics() noexcept
        {
            for (auto& bin : callbackHistogram) bin.store(0, std::memory_order_relaxed);
            callbackNanoseconds.store(0, std::memory_order_relaxed);
            callbackMaximumNs.store(0, std::memory_order_relaxed);
            callbacksOverHalfPeriod.store(0, std::memory_order_relaxed);
            callbacksOverFullPeriod.store(0, std::memory_order_relaxed);
            callbackMinimumFrames.store(std::numeric_limits<std::uint32_t>::max(),
                std::memory_order_relaxed);
            callbackMaximumFrames.store(0, std::memory_order_relaxed);
        }

        static void ProfiledDataCallback(ma_device* device, void* output,
            const void* input, ma_uint32 frames)
        {
            (void)input;
            auto* const engine = static_cast<ma_engine*>(device->pUserData);
            auto* const state = static_cast<Implementation*>(engine->pProcessUserData);
            const auto begin = std::chrono::steady_clock::now();
            (void)ma_engine_read_pcm_frames(engine, output, frames, nullptr);
            const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now() - begin).count();
            const std::uint64_t duration = static_cast<std::uint64_t>(std::max(elapsed, 0ll));
            const std::size_t bin = static_cast<std::size_t>(std::min<std::uint64_t>(
                duration / kHistogramStepNs, kHistogramBins - 1u));
            state->callbackHistogram[bin].fetch_add(1, std::memory_order_relaxed);
            state->callbackNanoseconds.fetch_add(duration, std::memory_order_relaxed);
            std::uint64_t maximum = state->callbackMaximumNs.load(std::memory_order_relaxed);
            while (maximum < duration && !state->callbackMaximumNs.compare_exchange_weak(
                maximum, duration, std::memory_order_relaxed)) {}
            std::uint32_t minimumFrames = state->callbackMinimumFrames.load(std::memory_order_relaxed);
            while (minimumFrames > frames && !state->callbackMinimumFrames.compare_exchange_weak(
                minimumFrames, frames, std::memory_order_relaxed)) {}
            std::uint32_t maximumFrames = state->callbackMaximumFrames.load(std::memory_order_relaxed);
            while (maximumFrames < frames && !state->callbackMaximumFrames.compare_exchange_weak(
                maximumFrames, frames, std::memory_order_relaxed)) {}
            if (device->sampleRate > 0u)
            {
                const std::uint64_t periodNs =
                    1'000'000'000ull * frames / device->sampleRate;
                if (duration * 2u >= periodNs)
                    state->callbacksOverHalfPeriod.fetch_add(1, std::memory_order_relaxed);
                if (duration >= periodNs)
                    state->callbacksOverFullPeriod.fetch_add(1, std::memory_order_relaxed);
            }
        }

        std::unordered_map<ClipKey, std::filesystem::path> clips;
        std::unordered_map<ClipKey, std::shared_ptr<ResidentClip>> cookedClips;
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
            if (slot.bufferInitialized)
            {
                ma_audio_buffer_uninit(&slot.buffer);
                slot.bufferInitialized = false;
            }
            slot.resident.reset();
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

    MiniaudioBackend::MiniaudioBackend(bool profileCallbacks)
        : m_implementation(std::make_unique<Implementation>())
    {
        m_implementation->profileCallbacks = profileCallbacks;
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
        state.ResetCallbackMetrics();
        state.deviceDiagnostics = {};
        if (state.profileCallbacks)
        {
            config.dataCallback = &Implementation::ProfiledDataCallback;
            config.pProcessUserData = &state;
        }

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
        if (const ma_device* const device = ma_engine_get_device(&state.engine))
        {
            state.deviceDiagnostics.backend = ma_get_backend_name(device->pContext->backend);
            state.deviceDiagnostics.deviceName = device->playback.name;
            state.deviceDiagnostics.periodFrames = device->playback.internalPeriodSizeInFrames;
            state.deviceDiagnostics.bufferFrames = device->playback.internalPeriodSizeInFrames *
                device->playback.internalPeriods;
            if (device->pContext->backend == ma_backend_wasapi)
                state.deviceDiagnostics.bufferFrames =
                    device->wasapi.actualBufferSizeInFramesPlayback;
        }
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
        state.cookedClips.clear();
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

        const std::string path = source.string();
        // Opening a sound can succeed from a valid FLAC STREAMINFO even when
        // the first audio frame is truncated. Require one decoded PCM frame.
        // Later stream corruption is an AU3/AU9 decode-error responsibility.
        ma_decoder decoder{};
        const ma_decoder_config decoderConfig = ma_decoder_config_init(ma_format_f32, 1u, 0u);
        const ma_result decoderResult = ma_decoder_init_file(path.c_str(), &decoderConfig, &decoder);
        if (MA_SUCCESS != decoderResult)
        {
            state.lastError = std::string("클립을 디코드하지 못했다: ") + path + " — " +
                ma_result_description(decoderResult);
            return false;
        }
        float firstSample = 0.0f;
        ma_uint64 framesRead = 0u;
        const ma_result readResult = ma_decoder_read_pcm_frames(&decoder, &firstSample, 1u, &framesRead);
        ma_decoder_uninit(&decoder);
        if (MA_SUCCESS != readResult || 1u != framesRead)
        {
            state.lastError = "클립의 첫 오디오 프레임을 읽지 못했다: " + path;
            return false;
        }

        // The probe validates resident decoding before publishing the key.
        // It is released here; StartVoice uses DECODE so mixing does not
        // decode the encoded file on the device callback.
        ma_sound probe{};
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
        state.cookedClips.erase(key);
        state.lastError.clear();
        return true;
    }

    bool MiniaudioBackend::LoadCookedClip(const ClipKey& key,
        const experiment::cooked::CookedAudioClipSource& source)
    {
        namespace ck = experiment::cooked;
        Implementation& state = *m_implementation;
        if (!state.engineReady || !key.IsGuid()
            || key != ClipKey::FromGuid(source.Id().value)) return false;
        const auto& metadata = source.Metadata();
        if (metadata.loadMode == ck::AudioLoadMode::Stream)
        {
            state.lastError = "stream 클립은 아직 작업자와 종료 계약이 없다";
            return false;
        }
        if (metadata.frameCount == 0u || metadata.channels == 0u
            || metadata.frameCount > Implementation::kResidentPcmBudgetBytes /
                (metadata.channels * sizeof(float))
            || source.PayloadSize() > Implementation::kResidentPcmBudgetBytes)
        {
            state.lastError = "resident 클립이 64 MiB 예산을 넘었다";
            return false;
        }

        std::vector<std::byte> encoded(static_cast<std::size_t>(source.PayloadSize()));
        std::string failure;
        if (!source.ReadPayload(0u, encoded, failure)
            || Hash::Sha256::Compute(encoded.data(), encoded.size()) != metadata.payloadSha256)
        {
            state.lastError = "cooked 클립 payload가 바뀌었거나 읽기에 실패했다: " + failure;
            return false;
        }

        ma_decoder decoder{};
        const ma_decoder_config config = ma_decoder_config_init(ma_format_f32, 0u, 0u);
        const ma_result initialized = ma_decoder_init_memory(
            encoded.data(), encoded.size(), &config, &decoder);
        if (initialized != MA_SUCCESS)
        {
            state.lastError = std::string("cooked 클립 디코더 초기화 실패: ") +
                ma_result_description(initialized);
            return false;
        }
        ma_format format = ma_format_unknown;
        ma_uint32 channels = 0u;
        ma_uint32 sampleRate = 0u;
        if (ma_decoder_get_data_format(&decoder, &format, &channels,
            &sampleRate, nullptr, 0u) != MA_SUCCESS
            || format != ma_format_f32 || channels != metadata.channels
            || sampleRate != metadata.sampleRate)
        {
            ma_decoder_uninit(&decoder);
            state.lastError = "cooked 클립 디코더 형식이 CEAC metadata와 다르다";
            return false;
        }
        auto resident = std::make_shared<Implementation::ResidentClip>();
        resident->channels = metadata.channels;
        resident->sampleRate = metadata.sampleRate;
        resident->frames = metadata.frameCount;
        resident->pcm.resize(static_cast<std::size_t>(
            metadata.frameCount * metadata.channels));
        ma_uint64 decoded = 0u;
        ma_result readResult = MA_SUCCESS;
        while (decoded < metadata.frameCount)
        {
            ma_uint64 framesRead = 0u;
            readResult = ma_decoder_read_pcm_frames(&decoder,
                resident->pcm.data() + decoded * metadata.channels,
                std::min<ma_uint64>(4096u, metadata.frameCount - decoded),
                &framesRead);
            decoded += framesRead;
            if ((readResult != MA_SUCCESS && readResult != MA_AT_END)
                || framesRead == 0u) break;
        }
        float extra[2]{};
        ma_uint64 extraFrames = 0u;
        if (decoded == metadata.frameCount)
            readResult = ma_decoder_read_pcm_frames(&decoder, extra, 1u, &extraFrames);
        ma_decoder_uninit(&decoder);
        if (decoded != metadata.frameCount || extraFrames != 0u
            || (readResult != MA_SUCCESS && readResult != MA_AT_END))
        {
            state.lastError = "cooked 클립 PCM 길이가 CEAC metadata와 다르다";
            return false;
        }
        state.cookedClips[key] = std::move(resident);
        state.clips.erase(key);
        state.lastError.clear();
        return true;
    }

    void MiniaudioBackend::UnloadClip(const ClipKey& key)
    {
        m_implementation->clips.erase(key);
        m_implementation->cookedClips.erase(key);
    }

    bool MiniaudioBackend::HasClip(const ClipKey& key) const
    {
        const Implementation& state = *m_implementation;
        return state.clips.find(key) != state.clips.end()
            || state.cookedClips.find(key) != state.cookedClips.end();
    }

    BackendVoiceId MiniaudioBackend::StartVoice(const PlayRequest& request)
    {
        Implementation& state = *m_implementation;
        if (!state.engineReady) return BackendVoiceId{};

        const auto clip = state.clips.find(request.clip);
        const auto cooked = state.cookedClips.find(request.clip);
        if (clip == state.clips.end() && cooked == state.cookedClips.end())
            return BackendVoiceId{};

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
        const ma_uint32 spatialFlag = spatial ? 0u : MA_SOUND_FLAG_NO_SPATIALIZATION;
        const std::string name = request.clip.Text();
        ma_result result = MA_SUCCESS;
        if (cooked != state.cookedClips.end())
        {
            slot.resident = cooked->second;
            ma_audio_buffer_config config = ma_audio_buffer_config_init(ma_format_f32,
                slot.resident->channels, slot.resident->frames,
                slot.resident->pcm.data(), nullptr);
            config.sampleRate = slot.resident->sampleRate;
            result = ma_audio_buffer_init(&config, &slot.buffer);
            if (result == MA_SUCCESS)
            {
                slot.bufferInitialized = true;
                result = ma_sound_init_from_data_source(&state.engine,
                    reinterpret_cast<ma_data_source*>(&slot.buffer), spatialFlag,
                    state.GroupFor(request.bus), &slot.sound);
            }
        }
        else
        {
            // Legacy authoring files still decode before playback.
            result = ma_sound_init_from_file(&state.engine, clip->second.string().c_str(),
                MA_SOUND_FLAG_DECODE | spatialFlag,
                state.GroupFor(request.bus), nullptr, &slot.sound);
        }
        if (MA_SUCCESS != result)
        {
            if (slot.bufferInitialized)
            {
                ma_audio_buffer_uninit(&slot.buffer);
                slot.bufferInitialized = false;
            }
            slot.resident.reset();
            state.lastError = std::string("보이스를 만들지 못했다: ") + name + " — " +
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
            state.lastError = "보이스를 시작하지 못했다: " + name;
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

    AudioCallbackMetrics MiniaudioBackend::CallbackMetrics() const noexcept
    {
        const Implementation& state = *m_implementation;
        AudioCallbackMetrics result{};
        std::uint64_t cumulative = 0;
        for (const auto& bin : state.callbackHistogram)
            result.count += bin.load(std::memory_order_relaxed);
        const std::uint64_t p99Rank = (result.count * 99u + 99u) / 100u;
        for (std::size_t index = 0; index < state.callbackHistogram.size(); ++index)
        {
            cumulative += state.callbackHistogram[index].load(std::memory_order_relaxed);
            if (p99Rank > 0u && cumulative >= p99Rank)
            {
                result.p99UpperNanoseconds = (index + 1u) * Implementation::kHistogramStepNs;
                break;
            }
        }
        if (result.count > 0u)
        {
            result.meanNanoseconds = state.callbackNanoseconds.load(std::memory_order_relaxed) /
                result.count;
            result.minimumFrames = state.callbackMinimumFrames.load(std::memory_order_relaxed);
            result.maximumFrames = state.callbackMaximumFrames.load(std::memory_order_relaxed);
        }
        result.maxNanoseconds = state.callbackMaximumNs.load(std::memory_order_relaxed);
        result.overHalfPeriod = state.callbacksOverHalfPeriod.load(std::memory_order_relaxed);
        result.overFullPeriod = state.callbacksOverFullPeriod.load(std::memory_order_relaxed);
        return result;
    }

    AudioDeviceDiagnostics MiniaudioBackend::DeviceDiagnostics() const
    {
        return m_implementation->deviceDiagnostics;
    }
}
