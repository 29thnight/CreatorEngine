#include "MiniaudioBackend.h"
#include "AudioSourceInspection.h"
#include "AudioSourceValidation.h"
#include "../../RenderEngine/Experiment/Cooked/CookedAudioClipSource.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <deque>
#include <limits>
#include <mutex>
#include <unordered_map>
#include <vector>

#if defined(WAVE_AUDIO_PROBE)
#include "../../../Tools/regression/audio_fixture_decoder.h"
#endif

// One implementation translation unit, pinned by ThirdParty/miniaudio/PROVENANCE.md.
#define MINIAUDIO_IMPLEMENTATION
#define MA_NO_ENCODING
// Two 250 ms pages per stream. Decode and byte reads run on two owned workers.
#define MA_RESOURCE_MANAGER_PAGE_SIZE_IN_MILLISECONDS 250
#if defined(_MSC_VER)
#pragma warning(push, 0)
#endif
#include "../../../ThirdParty/miniaudio/miniaudio.h"
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

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
            {
                return false;
            }
            ma_uint64 decodedLength = 0u;
            (void)ma_decoder_get_length_in_pcm_frames(&decoder, &decodedLength);
            length = static_cast<std::uint64_t>(decodedLength);
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
        constexpr float kHalfPi = 1.57079632679489661923f;

        float SafeGain(float value)
        {
            return std::isfinite(value) ? std::clamp(value, 0.0f, 16.0f) : 0.0f;
        }

        // Only resource-manager workers and the game thread enter this VFS.
        // Open pins a shared immutable cooked source; later reads never consult
        // the registry or any engine object. Registry locking cannot reach mixing.
        struct CookedVfs final
        {
            ma_vfs_callbacks callbacks{}; // Required first member by the VFS ABI.
            ma_default_vfs files{};
            std::mutex registryMutex;
            std::unordered_map<std::string, std::shared_ptr<const experiment::cooked::CookedAudioClipSource>> sources;
            std::atomic<bool> cancelling{ false };
            std::atomic<std::uint64_t> readFailures{ 0u };
            std::atomic<std::uint64_t> bytesRead{ 0u };

            struct File final
            {
                std::shared_ptr<const experiment::cooked::CookedAudioClipSource> source;
                ma_vfs_file native{};
                std::uint64_t cursor{ 0u };
            };

            CookedVfs()
            {
                (void)ma_default_vfs_init(&files, nullptr);
                callbacks.onOpen = &Open;
                callbacks.onOpenW = nullptr;
                callbacks.onClose = &Close;
                callbacks.onRead = &Read;
                callbacks.onWrite = &Write;
                callbacks.onSeek = &Seek;
                callbacks.onTell = &Tell;
                callbacks.onInfo = &Info;
            }

            static ma_result Open(ma_vfs* vfs, const char* path, ma_uint32 mode, ma_vfs_file* output)
            {
                auto& self = *static_cast<CookedVfs*>(vfs);
                if (self.cancelling.load(std::memory_order_acquire))
                {
                    return MA_CANCELLED;
                }
                if (mode != MA_OPEN_MODE_READ)
                {
                    return MA_ACCESS_DENIED;
                }
                auto file = std::make_unique<File>();
                if (std::strncmp(path, "ceac:", 5u) == 0)
                {
                    std::lock_guard lock(self.registryMutex);
                    const auto found = self.sources.find(path);
                    if (found == self.sources.end())
                    {
                        return MA_DOES_NOT_EXIST;
                    }
                    file->source = found->second;
                }
                else
                {
                    const ma_result result = ma_vfs_open(&self.files, path, mode, &file->native);
                    if (result != MA_SUCCESS)
                    {
                        return result;
                    }
                }
                *output = file.release();
                return MA_SUCCESS;
            }

            static ma_result Close(ma_vfs* vfs, ma_vfs_file handle)
            {
                auto& self = *static_cast<CookedVfs*>(vfs);
                std::unique_ptr<File> file(static_cast<File*>(handle));
                return file->source ? MA_SUCCESS : ma_vfs_close(&self.files, file->native);
            }

            static ma_result Read(ma_vfs* vfs, ma_vfs_file handle, void* destination,
                std::size_t bytes, std::size_t* read)
            {
                auto& self = *static_cast<CookedVfs*>(vfs);
                auto& file = *static_cast<File*>(handle);
                *read = 0u;
                if (self.cancelling.load(std::memory_order_acquire))
                {
                    return MA_CANCELLED;
                }
                if (!file.source)
                {
                    return ma_vfs_read(&self.files, file.native, destination, bytes, read);
                }
                const std::uint64_t size = file.source->PayloadSize();
                const std::size_t count = static_cast<std::size_t>(
                    std::min<std::uint64_t>(bytes, size - file.cursor));
                if (count == 0u)
                {
                    return bytes == 0u ? MA_SUCCESS : MA_AT_END;
                }
                std::string failure;
                if (!file.source->ReadPayload(file.cursor,
                    std::span(static_cast<std::byte*>(destination), count), failure))
                {
                    self.readFailures.fetch_add(1u, std::memory_order_relaxed);
                    return MA_IO_ERROR;
                }
                file.cursor += count;
                *read = count;
                self.bytesRead.fetch_add(count, std::memory_order_relaxed);
                return MA_SUCCESS;
            }

            static ma_result Write(ma_vfs*, ma_vfs_file, const void*, std::size_t, std::size_t* written)
            {
                if (written != nullptr)
                {
                    *written = 0u;
                }
                return MA_ACCESS_DENIED;
            }

            static ma_result Seek(ma_vfs* vfs, ma_vfs_file handle, ma_int64 offset, ma_seek_origin origin)
            {
                auto& self = *static_cast<CookedVfs*>(vfs);
                auto& file = *static_cast<File*>(handle);
                if (!file.source)
                {
                    return ma_vfs_seek(&self.files, file.native, offset, origin);
                }
                const std::uint64_t size = file.source->PayloadSize();
                std::uint64_t base = 0u;
                if (origin == ma_seek_origin_current)
                {
                    base = file.cursor;
                }
                else if (origin == ma_seek_origin_end)
                {
                    base = size;
                }
                else if (origin != ma_seek_origin_start)
                {
                    return MA_INVALID_ARGS;
                }
                if (offset >= 0)
                {
                    const std::uint64_t distance = static_cast<std::uint64_t>(offset);
                    if (distance > size - base)
                    {
                        return MA_BAD_SEEK;
                    }
                    file.cursor = base + distance;
                }
                else
                {
                    const std::uint64_t distance = static_cast<std::uint64_t>(-(offset + 1)) + 1u;
                    if (distance > base)
                    {
                        return MA_BAD_SEEK;
                    }
                    file.cursor = base - distance;
                }
                return MA_SUCCESS;
            }

            static ma_result Tell(ma_vfs* vfs, ma_vfs_file handle, ma_int64* cursor)
            {
                auto& self = *static_cast<CookedVfs*>(vfs);
                const auto& file = *static_cast<File*>(handle);
                if (!file.source)
                {
                    return ma_vfs_tell(&self.files, file.native, cursor);
                }
                *cursor = static_cast<ma_int64>(file.cursor);
                return MA_SUCCESS;
            }

            static ma_result Info(ma_vfs* vfs, ma_vfs_file handle, ma_file_info* info)
            {
                auto& self = *static_cast<CookedVfs*>(vfs);
                const auto& file = *static_cast<File*>(handle);
                if (!file.source)
                {
                    return ma_vfs_info(&self.files, file.native, info);
                }
                info->sizeInBytes = file.source->PayloadSize();
                return MA_SUCCESS;
            }
        };

        // Schroeder room reverb: four damped combs and two all-pass stages per
        // channel. Storage is allocated before node attachment, never in Process.
        struct RoomReverb final
        {
            ma_node_base node{}; // Required first member by the node ABI.
            struct Delay final
            {
                std::vector<float> samples;
                std::size_t cursor{ 0u };
                float filter{ 0.0f };
            };
            std::array<std::array<Delay, 6u>, 2u> delays;
            std::atomic<float> feedback{ 0.72f };
            std::atomic<float> damping{ 0.25f };
            bool ready{ false };

            bool Initialize(ma_engine& engine)
            {
                const ma_uint32 channels = ma_engine_get_channels(&engine);
                if (channels != 2u)
                {
                    return false;
                }
                constexpr std::array<float, 6u> seconds{ 0.0297f, 0.0371f, 0.0411f, 0.0437f, 0.005f, 0.0017f };
                const ma_uint32 sampleRate = ma_engine_get_sample_rate(&engine);
                for (std::size_t channel = 0u; channel < delays.size(); ++channel)
                {
                    for (std::size_t index = 0u; index < seconds.size(); ++index)
                    {
                        const auto frames = static_cast<std::size_t>(seconds[index] * static_cast<float>(sampleRate)) + channel * 23u;
                        delays[channel][index].samples.assign(std::max<std::size_t>(1u, frames), 0.0f);
                        delays[channel][index].cursor = 0u;
                        delays[channel][index].filter = 0.0f;
                    }
                }
                static const ma_node_vtable vtable{ &Process, nullptr, 1u, 1u,
                    MA_NODE_FLAG_CONTINUOUS_PROCESSING | MA_NODE_FLAG_ALLOW_NULL_INPUT };
                ma_node_config config = ma_node_config_init();
                config.vtable = &vtable;
                config.pInputChannels = &channels;
                config.pOutputChannels = &channels;
                ready = ma_node_init(ma_engine_get_node_graph(&engine), &config, nullptr, &node) == MA_SUCCESS;
                return ready;
            }

            static void Process(ma_node* raw, const float** input, ma_uint32* inputFrames,
                float** output, ma_uint32* outputFrames)
            {
                auto& self = *static_cast<RoomReverb*>(raw);
                const float feedback = self.feedback.load(std::memory_order_relaxed);
                const float damping = self.damping.load(std::memory_order_relaxed);
                const float* source = input != nullptr ? input[0] : nullptr;
                for (ma_uint32 frame = 0u; frame < *outputFrames; ++frame)
                {
                    for (std::size_t channel = 0u; channel < 2u; ++channel)
                    {
                        const float sample = source != nullptr && frame < *inputFrames ? source[frame * 2u + channel] : 0.0f;
                        float wet = 0.0f;
                        for (std::size_t index = 0u; index < 4u; ++index)
                        {
                            Delay& delay = self.delays[channel][index];
                            const float previous = delay.samples[delay.cursor];
                            delay.filter = previous * (1.0f - damping) + delay.filter * damping;
                            delay.samples[delay.cursor] = sample * 0.2f + delay.filter * feedback;
                            delay.cursor = (delay.cursor + 1u) % delay.samples.size();
                            wet += previous;
                        }
                        for (std::size_t index = 4u; index < 6u; ++index)
                        {
                            Delay& delay = self.delays[channel][index];
                            const float previous = delay.samples[delay.cursor];
                            const float next = wet + previous * 0.5f;
                            wet = previous - wet * 0.5f;
                            delay.samples[delay.cursor] = next;
                            delay.cursor = (delay.cursor + 1u) % delay.samples.size();
                        }
                        output[0][frame * 2u + channel] = wet;
                    }
                }
            }

            void Uninitialize()
            {
                if (ready)
                {
                    ma_node_uninit(&node, nullptr);
                    ready = false;
                }
            }
        };
    }

    struct MiniaudioBackend::Implementation final
    {
        static constexpr std::uint64_t kHistogramStepNs = 10'000u;
        static constexpr std::size_t kHistogramBins = 2049u;
        static constexpr std::uint64_t kResidentPcmBudgetBytes = 64u * 1024u * 1024u;

        struct Clip final
        {
            std::vector<float> pcm;
            ClipInfo info{};
            std::string path;
            bool cooked{ false };
            CookedVfs* registry{ nullptr };

            ~Clip()
            {
                // The last logical/physical lease unregisters this generation.
                if (registry != nullptr && cooked && info.streaming)
                {
                    std::lock_guard lock(registry->registryMutex);
                    registry->sources.erase(path);
                }
            }
        };

        struct Source final
        {
            ma_sound sound{};
            ma_audio_buffer buffer{};
            ma_splitter_node splitter{};
            bool initialized{ false };
            bool bufferInitialized{ false };
            bool splitterInitialized{ false };
        };

        // Stable addresses are required by the graph and pending resource jobs.
        struct VoiceSlot final
        {
            std::array<Source, 2u> sources;
            std::shared_ptr<Clip> clip;
            PlayRequest request{};
            bool initialized{ false };
            bool paused{ false };
            bool failed{ false };
        };

        CookedVfs vfs;
        ma_resource_manager resources{};
        bool resourcesReady{ false };
        ma_engine engine{};
        ma_device device{};
        bool deviceReady{ false };
        bool engineReady{ false };
        bool accepting{ false };
        bool profileCallbacks{ false };
        std::atomic<bool> outputInterrupted{ false };
        std::atomic<std::uint64_t> rerouteCount{ 0u };
        std::atomic<bool> restartRequested{ false };
        std::uint64_t restartAttempts{ 0u };
        std::uint64_t successfulRestarts{ 0u };
        std::chrono::steady_clock::time_point nextDeviceAttempt{};
        std::chrono::steady_clock::time_point lastUpdate{};
        double silentFramesPending{ 0.0 };
        float retrySeconds{ 1.0f };
        RoomReverb reverb;
        ReverbPreset preset{ ReverbPreset::Room };
        std::unordered_map<ClipKey, std::shared_ptr<Clip>> clips;
        std::unordered_map<std::uint64_t, std::shared_ptr<Clip>> clipLeases;
        std::uint64_t nextClipLease{ 1u };
        std::unordered_map<std::uint16_t, std::unique_ptr<ma_sound_group>> busGroups;
        std::unordered_map<std::uint16_t, float> busVolumes;
        std::deque<VoiceSlot> voices;
        std::vector<std::size_t> freeVoices;
        std::uint64_t nextSource{ 1u };
        std::string lastError;
        DeviceSettings actual{};
        AudioDeviceDiagnostics deviceDiagnostics{};
        std::array<std::atomic<std::uint64_t>, kHistogramBins> callbackHistogram{};
        std::atomic<std::uint64_t> callbackNanoseconds{ 0u };
        std::atomic<std::uint64_t> callbackMaximumNs{ 0u };
        std::atomic<std::uint64_t> callbacksOverHalfPeriod{ 0u };
        std::atomic<std::uint64_t> callbacksOverFullPeriod{ 0u };
        std::atomic<std::uint32_t> callbackMinimumFrames{ std::numeric_limits<std::uint32_t>::max() };
        std::atomic<std::uint32_t> callbackMaximumFrames{ 0u };

        void ResetCallbackMetrics() noexcept
        {
            for (auto& bin : callbackHistogram)
            {
                bin.store(0u, std::memory_order_relaxed);
            }
            callbackNanoseconds.store(0u, std::memory_order_relaxed);
            callbackMaximumNs.store(0u, std::memory_order_relaxed);
            callbacksOverHalfPeriod.store(0u, std::memory_order_relaxed);
            callbacksOverFullPeriod.store(0u, std::memory_order_relaxed);
            callbackMinimumFrames.store(std::numeric_limits<std::uint32_t>::max(), std::memory_order_relaxed);
            callbackMaximumFrames.store(0u, std::memory_order_relaxed);
        }

        static void DeviceNotification(const ma_device_notification* notification)
        {
            auto* engine = static_cast<ma_engine*>(notification->pDevice->pUserData);
            auto* state = static_cast<Implementation*>(engine->pProcessUserData);
            if (state == nullptr)
            {
                return;
            }
            if (notification->type == ma_device_notification_type_stopped
                || notification->type == ma_device_notification_type_interruption_began)
            {
                state->outputInterrupted.store(true, std::memory_order_release);
            }
            else if (notification->type == ma_device_notification_type_started
                || notification->type == ma_device_notification_type_interruption_ended)
            {
                state->outputInterrupted.store(false, std::memory_order_release);
            }
            else if (notification->type == ma_device_notification_type_rerouted)
            {
                state->rerouteCount.fetch_add(1u, std::memory_order_relaxed);
            }
        }

        static void ProfiledDataCallback(ma_device* device, void* output, const void*, ma_uint32 frames)
        {
            auto* engine = static_cast<ma_engine*>(device->pUserData);
            auto* state = static_cast<Implementation*>(engine->pProcessUserData);
            const auto begin = std::chrono::steady_clock::now();
            (void)ma_engine_read_pcm_frames(engine, output, frames, nullptr);
            const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now() - begin).count();
            const std::uint64_t duration = elapsed > 0 ? static_cast<std::uint64_t>(elapsed) : 0u;
            const std::size_t bin = static_cast<std::size_t>(std::min<std::uint64_t>(
                duration / kHistogramStepNs, kHistogramBins - 1u));
            state->callbackHistogram[bin].fetch_add(1u, std::memory_order_relaxed);
            state->callbackNanoseconds.fetch_add(duration, std::memory_order_relaxed);
            std::uint64_t maximum = state->callbackMaximumNs.load(std::memory_order_relaxed);
            while (maximum < duration && !state->callbackMaximumNs.compare_exchange_weak(maximum, duration, std::memory_order_relaxed))
            {
            }
            std::uint32_t minimumFrames = state->callbackMinimumFrames.load(std::memory_order_relaxed);
            while (minimumFrames > frames && !state->callbackMinimumFrames.compare_exchange_weak(minimumFrames, frames, std::memory_order_relaxed))
            {
            }
            std::uint32_t maximumFrames = state->callbackMaximumFrames.load(std::memory_order_relaxed);
            while (maximumFrames < frames && !state->callbackMaximumFrames.compare_exchange_weak(maximumFrames, frames, std::memory_order_relaxed))
            {
            }
            if (device->sampleRate > 0u)
            {
                const std::uint64_t periodNs = 1'000'000'000ull * frames / device->sampleRate;
                if (duration * 2u >= periodNs)
                {
                    state->callbacksOverHalfPeriod.fetch_add(1u, std::memory_order_relaxed);
                }
                if (duration >= periodNs)
                {
                    state->callbacksOverFullPeriod.fetch_add(1u, std::memory_order_relaxed);
                }
            }
        }

        static void DeviceData(ma_device* device, void* output, const void* input, ma_uint32 frames)
        {
            auto* engine = static_cast<ma_engine*>(device->pUserData);
            auto* state = static_cast<Implementation*>(engine->pProcessUserData);
            if (state->profileCallbacks)
            {
                ProfiledDataCallback(device, output, input, frames);
            }
            else
            {
                (void)ma_engine_read_pcm_frames(engine, output, frames, nullptr);
            }
        }

        ma_result OpenDevice(const DeviceSettings& settings)
        {
            ma_device_config config = ma_device_config_init(ma_device_type_playback);
            config.playback.format = ma_format_f32;
            config.playback.channels = settings.channels;
            config.sampleRate = settings.sampleRate;
            config.dataCallback = &DeviceData;
            config.notificationCallback = &DeviceNotification;
            config.pUserData = &engine;
            config.noPreSilencedOutputBuffer = MA_TRUE;
            config.noClip = MA_TRUE;
            ma_context_config context = ma_context_config_init();
            context.pLog = ma_resource_manager_get_log(&resources);
            // Default backend order is WASAPI-first on Windows and then the
            // vendor-supported fallback list. The actual choice is recorded.
            const ma_result result = ma_device_init_ex(nullptr, 0u, &context, &config, &device);
            deviceReady = result == MA_SUCCESS;
            if (deviceReady)
            {
                deviceDiagnostics.backend = ma_get_backend_name(device.pContext->backend);
                deviceDiagnostics.deviceName = device.playback.name;
                deviceDiagnostics.periodFrames = device.playback.internalPeriodSizeInFrames;
                deviceDiagnostics.bufferFrames = device.playback.internalPeriodSizeInFrames * device.playback.internalPeriods;
#if defined(MA_HAS_WASAPI)
                if (device.pContext->backend == ma_backend_wasapi)
                {
                    deviceDiagnostics.bufferFrames = device.wasapi.actualBufferSizeInFramesPlayback;
                }
#endif
            }
            return result;
        }

        void RecoverDevice()
        {
            const auto now = std::chrono::steady_clock::now();
            const double elapsed = std::chrono::duration<double>(now - lastUpdate).count();
            lastUpdate = now;
            if (actual.noDevice || !accepting)
            {
                return;
            }
            const bool requested = restartRequested.exchange(false, std::memory_order_acq_rel);
            if (!requested && !outputInterrupted.load(std::memory_order_acquire))
            {
                silentFramesPending = 0.0;
                return;
            }
            // Stop joins the callback before the game thread touches the graph.
            const bool hadDevice = deviceReady;
            if (deviceReady)
            {
                (void)ma_device_stop(&device);
                ma_device_uninit(&device);
                deviceReady = false;
                engine.pDevice = nullptr;
                nextDeviceAttempt = now;
                outputInterrupted.store(true, std::memory_order_release);
            }
            if (!hadDevice)
            {
                silentFramesPending += std::max(0.0, elapsed) * actual.sampleRate;
            }
            std::array<float, 512u * 2u> silence{};
            // Bound work per tick while carrying, rather than losing, elapsed time.
            ma_uint64 remaining = static_cast<ma_uint64>(std::min(silentFramesPending,
                static_cast<double>(actual.sampleRate) * 0.25));
            while (remaining > 0u)
            {
                const ma_uint64 frames = std::min<ma_uint64>(512u, remaining);
                (void)ma_engine_read_pcm_frames(&engine, silence.data(), frames, nullptr);
                silentFramesPending -= static_cast<double>(frames);
                remaining -= frames;
            }
            if (!requested && now < nextDeviceAttempt)
            {
                return;
            }
            ++restartAttempts;
            ma_result result = OpenDevice(actual);
            if (result == MA_SUCCESS)
            {
                // The public device format is fixed to the existing engine's mix
                // rate/channels; the device handles conversion to its native format.
                engine.pDevice = &device;
                result = ma_device_start(&device);
            }
            if (result == MA_SUCCESS)
            {
                outputInterrupted.store(false, std::memory_order_release);
                ++successfulRestarts;
                retrySeconds = 1.0f;
                silentFramesPending = 0.0;
                lastError.clear();
            }
            else
            {
                if (deviceReady)
                {
                    ma_device_uninit(&device);
                    deviceReady = false;
                }
                engine.pDevice = nullptr;
                outputInterrupted.store(true, std::memory_order_release);
                lastError = std::string("Audio output unavailable; automatic retry pending: ") + ma_result_description(result);
                nextDeviceAttempt = now + std::chrono::milliseconds(static_cast<int>(retrySeconds * 1000.0f));
                retrySeconds = std::min(5.0f, retrySeconds * 2.0f);
            }
        }

        VoiceSlot* Resolve(BackendVoiceId voice)
        {
            if (!voice.IsValid() || voice.value > voices.size())
            {
                return nullptr;
            }
            VoiceSlot& slot = voices[static_cast<std::size_t>(voice.value - 1u)];
            return slot.initialized ? &slot : nullptr;
        }

        void ReleaseSlot(std::size_t index)
        {
            VoiceSlot& slot = voices[index];
            for (Source& source : slot.sources)
            {
                if (source.initialized)
                {
                    (void)ma_sound_stop(&source.sound);
                    // Detaches the mixer node and drains/cancels pending stream jobs.
                    ma_sound_uninit(&source.sound);
                    source.initialized = false;
                }
                if (source.splitterInitialized)
                {
                    ma_splitter_node_uninit(&source.splitter, nullptr);
                    source.splitterInitialized = false;
                }
                if (source.bufferInitialized)
                {
                    ma_audio_buffer_uninit(&source.buffer);
                    source.bufferInitialized = false;
                }
            }
            slot.clip.reset();
            slot.request = {};
            slot.initialized = false;
            slot.paused = false;
            slot.failed = false;
            freeVoices.push_back(index);
        }

        ma_sound_group* GroupFor(BusId bus)
        {
            if (!bus.IsValid())
            {
                bus = Buses::SFX;
            }
            const auto found = busGroups.find(bus.value);
            if (found != busGroups.end())
            {
                return found->second.get();
            }
            ma_sound_group* parent = bus == Buses::Master ? nullptr : GroupFor(Buses::Master);
            auto group = std::make_unique<ma_sound_group>();
            if (ma_sound_group_init(&engine, MA_SOUND_FLAG_NO_SPATIALIZATION, parent, group.get()) != MA_SUCCESS)
            {
                return nullptr;
            }
            auto* raw = group.get();
            busGroups.emplace(bus.value, std::move(group));
            const auto gain = busVolumes.find(bus.value);
            if (gain != busVolumes.end())
            {
                ma_sound_group_set_volume(raw, gain->second);
            }
            return raw;
        }

        void Apply(VoiceSlot& slot)
        {
            const PlayRequest& request = slot.request;
            const float angle = std::clamp(request.spatialBlend, 0.0f, 1.0f) * kHalfPi;
            const std::array<float, 2u> gains{
                request.spatialBlend >= 1.0f ? 0.0f : std::cos(angle),
                request.spatialBlend <= 0.0f ? 0.0f : std::sin(angle) * SafeGain(request.attenuationGain) };
            const auto busGain = busVolumes.find(request.bus.value);
            const float sendBusGain = request.bus != Buses::Master && busGain != busVolumes.end() ? busGain->second : 1.0f;
            const float send = request.useReverbSend && request.reverbBus == Buses::Room ?
                std::pow(10.0f, std::clamp(request.reverbSendDecibels, -80.0f, 10.0f) / 20.0f) * sendBusGain : 0.0f;
            for (std::size_t index = 0u; index < slot.sources.size(); ++index)
            {
                Source& source = slot.sources[index];
                ma_sound_set_volume(&source.sound, SafeGain(request.volume) * gains[index]);
                ma_sound_set_pitch(&source.sound, request.pitch * request.dopplerPitch);
                ma_sound_set_looping(&source.sound, request.loop ? MA_TRUE : MA_FALSE);
                ma_sound_set_position(&source.sound, request.position.x, request.position.y, -request.position.z);
                ma_sound_set_velocity(&source.sound, request.velocity.x, request.velocity.y, -request.velocity.z);
                // The Runtime samples every attenuation curve exactly once.
                // 'none' bypasses the vendor spatializer's master gain and
                // panning too. Zero rolloff preserves panning/gain without a
                // second distance attenuation pass.
                ma_sound_set_attenuation_model(&source.sound, ma_attenuation_model_inverse);
                ma_sound_set_rolloff(&source.sound, 0.0f);
                ma_sound_set_doppler_factor(&source.sound, 0.0f);
                ma_sound_set_spatialization_enabled(&source.sound, index == 1u && slot.clip->info.channels == 1u ? MA_TRUE : MA_FALSE);
                (void)ma_node_set_output_bus_volume(&source.splitter, 1u, send);
            }
        }

        bool InitializeSource(Source& source, const std::shared_ptr<Clip>& clip, BusId bus)
        {
            ma_result result = MA_SUCCESS;
            if (!clip->pcm.empty())
            {
                ma_audio_buffer_config bufferConfig = ma_audio_buffer_config_init(ma_format_f32,
                    clip->info.channels, clip->info.frameCount, clip->pcm.data(), nullptr);
                bufferConfig.sampleRate = clip->info.sampleRate;
                result = ma_audio_buffer_init(&bufferConfig, &source.buffer);
                if (result == MA_SUCCESS)
                {
                    source.bufferInitialized = true;
                    result = ma_sound_init_from_data_source(&engine, &source.buffer,
                        MA_SOUND_FLAG_NO_DEFAULT_ATTACHMENT, nullptr, &source.sound);
                }
            }
            else
            {
                const ma_uint32 mode = clip->info.streaming ? MA_SOUND_FLAG_STREAM : MA_SOUND_FLAG_DECODE;
                result = ma_sound_init_from_file(&engine, clip->path.c_str(),
                    mode | MA_SOUND_FLAG_NO_DEFAULT_ATTACHMENT, nullptr, nullptr, &source.sound);
            }
            if (result != MA_SUCCESS)
            {
                lastError = std::string("Cannot initialize audio source: ") + ma_result_description(result);
                return false;
            }
            source.initialized = true;
            const ma_splitter_node_config config = ma_splitter_node_config_init(actual.channels);
            if (ma_splitter_node_init(ma_engine_get_node_graph(&engine), &config, nullptr, &source.splitter) != MA_SUCCESS)
            {
                lastError = "Cannot initialize audio send splitter";
                return false;
            }
            source.splitterInitialized = true;
            ma_sound_group* group = GroupFor(bus);
            if (group == nullptr
                || ma_node_attach_output_bus(&source.sound, 0u, &source.splitter, 0u) != MA_SUCCESS
                || ma_node_attach_output_bus(&source.splitter, 0u, group, 0u) != MA_SUCCESS
                || ma_node_attach_output_bus(&source.splitter, 1u, &reverb.node, 0u) != MA_SUCCESS)
            {
                lastError = "Cannot attach audio dry/wet routing";
                return false;
            }
            return true;
        }
    };

    bool InspectAudioSource(const std::filesystem::path& source, ClipInfo& decoded, std::string& error)
    {
        decoded = {};
        std::uint64_t declaredFrames = 0u;
        if (!ValidateAudioSourceContainer(source, declaredFrames, error))
        {
            return false;
        }
        ma_decoder decoder{};
        const ma_decoder_config config = ma_decoder_config_init(ma_format_f32, 0u, 0u);
#if defined(_WIN32)
        const ma_result initialized = ma_decoder_init_file_w(source.c_str(), &config, &decoder);
#else
        const ma_result initialized = ma_decoder_init_file(source.c_str(), &config, &decoder);
#endif
        if (initialized != MA_SUCCESS)
        {
            error = std::string("Cannot initialize audio source decoder: ") + ma_result_description(initialized);
            return false;
        }
        ma_format format{};
        ma_uint32 channels = 0u;
        ma_uint32 sampleRate = 0u;
        if (ma_decoder_get_data_format(&decoder, &format, &channels, &sampleRate, nullptr, 0u) != MA_SUCCESS
            || channels == 0u || channels > 2u || sampleRate == 0u)
        {
            ma_decoder_uninit(&decoder);
            error = "Audio sources require mono or stereo PCM with a valid sample rate";
            return false;
        }
        std::array<float, 4096u * 2u> samples{};
        ma_uint64 frames = 0u;
        ma_result result = MA_SUCCESS;
        while (result == MA_SUCCESS)
        {
            ma_uint64 read = 0u;
            result = ma_decoder_read_pcm_frames(&decoder, samples.data(), 4096u, &read);
            frames += read;
            if (!std::all_of(samples.begin(), samples.begin() + static_cast<std::size_t>(read * channels),
                [](float value) { return std::isfinite(value); }))
            {
                result = MA_INVALID_DATA;
                break;
            }
            if (read == 0u)
            {
                break;
            }
        }
        ma_decoder_uninit(&decoder);
        if ((result != MA_SUCCESS && result != MA_AT_END) || frames == 0u
            || (declaredFrames != 0u && frames != declaredFrames))
        {
            error = "Audio source is empty, truncated, or differs from its declared sample count";
            return false;
        }
        decoded = { channels, sampleRate, static_cast<std::uint64_t>(frames), false };
        error.clear();
        return true;
    }

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
        if (state.engineReady)
        {
            return true;
        }
        if (settings.channels != 2u || settings.sampleRate < 8000u || settings.sampleRate > 192000u)
        {
            state.lastError = "Audio output requires stereo and a sample rate from 8000 to 192000 Hz";
            return false;
        }
        state.vfs.cancelling.store(false, std::memory_order_release);
        state.vfs.readFailures.store(0u, std::memory_order_relaxed);
        state.vfs.bytesRead.store(0u, std::memory_order_relaxed);
        ma_resource_manager_config resources = ma_resource_manager_config_init();
        resources.decodedFormat = ma_format_f32;
        resources.decodedSampleRate = 0u; // Source-frame seek/playhead contract.
        resources.jobThreadCount = 2u;
        resources.jobQueueCapacity = 1024u;
        resources.pVFS = &state.vfs;
        ma_result result = ma_resource_manager_init(&resources, &state.resources);
        if (result != MA_SUCCESS)
        {
            state.lastError = std::string("Cannot start audio decode workers: ") + ma_result_description(result);
            return false;
        }
        state.resourcesReady = true;
        state.deviceDiagnostics = {};
        state.restartAttempts = 0u;
        state.successfulRestarts = 0u;
        state.retrySeconds = 1.0f;
        state.silentFramesPending = 0.0;
        state.restartRequested.store(false, std::memory_order_relaxed);
        ma_engine_config config = ma_engine_config_init();
        config.sampleRate = settings.sampleRate;
        config.channels = settings.channels;
        config.noDevice = MA_TRUE;
        config.noAutoStart = MA_TRUE;
        config.pLog = ma_resource_manager_get_log(&state.resources);
        if (!settings.noDevice)
        {
            result = state.OpenDevice(settings);
            if (result != MA_SUCCESS)
            {
                // Keep one stable engine/service when output is absent at startup.
                // Its graph advances silently until device-only recovery succeeds.
                state.lastError = std::string("Audio output unavailable; automatic retry pending: ")
                    + ma_result_description(result);
            }
            else
            {
                config.noDevice = MA_FALSE;
                config.pDevice = &state.device;
            }
        }
        config.pResourceManager = &state.resources;
        config.pProcessUserData = &state;
        config.notificationCallback = &Implementation::DeviceNotification;
        state.outputInterrupted.store(!settings.noDevice && !state.deviceReady, std::memory_order_relaxed);
        state.rerouteCount.store(0u, std::memory_order_relaxed);
        state.ResetCallbackMetrics();
        if (state.profileCallbacks)
        {
            config.dataCallback = &Implementation::ProfiledDataCallback;
            config.pProcessUserData = &state;
        }
        result = ma_engine_init(&config, &state.engine);
        if (result != MA_SUCCESS)
        {
            state.lastError = std::string("Cannot open audio device: ") + ma_result_description(result);
            Stop();
            return false;
        }
        state.engineReady = true;
        state.actual = { ma_engine_get_sample_rate(&state.engine), ma_engine_get_channels(&state.engine), settings.noDevice };
        if (const ma_device* device = ma_engine_get_device(&state.engine))
        {
            state.deviceDiagnostics.backend = ma_get_backend_name(device->pContext->backend);
            state.deviceDiagnostics.deviceName = device->playback.name;
            state.deviceDiagnostics.periodFrames = device->playback.internalPeriodSizeInFrames;
            state.deviceDiagnostics.bufferFrames = device->playback.internalPeriodSizeInFrames * device->playback.internalPeriods;
#if defined(MA_HAS_WASAPI)
            if (device->pContext->backend == ma_backend_wasapi)
            {
                state.deviceDiagnostics.bufferFrames = device->wasapi.actualBufferSizeInFramesPlayback;
            }
#endif
        }
        else
        {
            state.deviceDiagnostics.backend = settings.noDevice ? "offline" : "degraded";
            state.deviceDiagnostics.deviceName = settings.noDevice ? "Explicit no-device renderer" : "No output device; retry pending";
        }
        for (BusId bus : { Buses::Master, Buses::BGM, Buses::SFX, Buses::Player, Buses::Monster, Buses::UI })
        {
            if (state.GroupFor(bus) == nullptr)
            {
                state.lastError = "Cannot initialize default audio buses";
                Stop();
                return false;
            }
        }
        if (!state.reverb.Initialize(state.engine)
            || ma_node_attach_output_bus(&state.reverb.node, 0u, state.GroupFor(Buses::Master), 0u) != MA_SUCCESS)
        {
            state.lastError = "Cannot initialize room reverb bus";
            Stop();
            return false;
        }
        SetReverbPreset(state.preset);
        result = state.deviceReady ? ma_engine_start(&state.engine) : MA_SUCCESS;
        if (result != MA_SUCCESS)
        {
            state.lastError = std::string("Cannot start audio output; automatic retry pending: ") + ma_result_description(result);
            ma_device_uninit(&state.device);
            state.deviceReady = false;
            state.engine.pDevice = nullptr;
            state.outputInterrupted.store(true, std::memory_order_release);
            state.deviceDiagnostics.backend = "degraded";
            state.deviceDiagnostics.deviceName = "Output start failed; retry pending";
        }
        state.accepting = true;
        state.lastUpdate = std::chrono::steady_clock::now();
        state.nextDeviceAttempt = state.lastUpdate + std::chrono::seconds(1);
        if (settings.noDevice || state.deviceReady)
        {
            state.lastError.clear();
        }
        return true;
    }

    void MiniaudioBackend::Stop()
    {
        Implementation& state = *m_implementation;
        state.accepting = false;
        if (state.deviceReady)
        {
            (void)ma_device_stop(&state.device);
            ma_device_uninit(&state.device);
            state.deviceReady = false;
            state.engine.pDevice = nullptr;
        }
        state.vfs.cancelling.store(true, std::memory_order_release);
        if (state.engineReady)
        {
            for (std::size_t index = 0u; index < state.voices.size(); ++index)
            {
                if (state.voices[index].initialized)
                {
                    state.ReleaseSlot(index);
                }
            }
            state.voices.clear();
            state.freeVoices.clear();
            state.reverb.Uninitialize();
            // Children detach before the Master group they reference.
            for (auto& [bus, group] : state.busGroups)
            {
                if (bus != Buses::Master.value)
                {
                    ma_sound_group_uninit(group.get());
                }
            }
            const auto master = state.busGroups.find(Buses::Master.value);
            if (master != state.busGroups.end())
            {
                ma_sound_group_uninit(master->second.get());
            }
            state.busGroups.clear();
            ma_engine_uninit(&state.engine);
            state.engineReady = false;
        }
        // Join the owned decode workers before releasing any mounted byte source.
        if (state.resourcesReady)
        {
            ma_resource_manager_uninit(&state.resources);
            state.resourcesReady = false;
        }
        state.clips.clear();
        state.clipLeases.clear();
        std::lock_guard lock(state.vfs.registryMutex);
        state.vfs.sources.clear();
    }

    bool MiniaudioBackend::IsRunning() const
    {
        return m_implementation->accepting;
    }

    bool MiniaudioBackend::IsOutputAvailable() const
    {
        const Implementation& state = *m_implementation;
        return state.accepting && !state.actual.noDevice && state.deviceReady
            && !state.outputInterrupted.load(std::memory_order_acquire);
    }

    bool MiniaudioBackend::LoadClip(const ClipKey& key, const std::filesystem::path& path)
    {
        Implementation& state = *m_implementation;
        if (!state.accepting || key.IsEmpty())
        {
            return false;
        }
        std::uint64_t declaredFrames = 0u;
        if (!ValidateAudioSourceContainer(path, declaredFrames, state.lastError))
        {
            const auto utf8 = path.u8string();
            state.lastError += ": " + std::string(utf8.begin(), utf8.end());
            return false;
        }
        ma_decoder decoder{};
        const ma_decoder_config config = ma_decoder_config_init(ma_format_f32, 0u, 0u);
#if defined(_WIN32)
        const ma_result initialized = ma_decoder_init_file_w(path.c_str(), &config, &decoder);
#else
        const ma_result initialized = ma_decoder_init_file(path.c_str(), &config, &decoder);
#endif
        if (initialized != MA_SUCCESS)
        {
            state.lastError = "Cannot decode audio file: " + path.string();
            return false;
        }
        ma_format format{};
        ma_uint32 channels = 0u;
        ma_uint32 sampleRate = 0u;
        ma_uint64 frames = 0u;
        bool valid = ma_decoder_get_data_format(&decoder, &format, &channels, &sampleRate, nullptr, 0u) == MA_SUCCESS
            && channels > 0u && channels <= 2u
            && ma_decoder_get_length_in_pcm_frames(&decoder, &frames) == MA_SUCCESS
            && frames > 0u && frames <= Implementation::kResidentPcmBudgetBytes / (channels * sizeof(float));
        auto clip = std::make_shared<Implementation::Clip>();
        if (valid)
        {
            clip->info = { channels, sampleRate, frames, false };
            clip->pcm.resize(static_cast<std::size_t>(frames * channels));
            ma_uint64 decoded = 0u;
            while (decoded < frames)
            {
                ma_uint64 read = 0u;
                const ma_result result = ma_decoder_read_pcm_frames(&decoder, clip->pcm.data() + decoded * channels,
                    std::min<ma_uint64>(4096u, frames - decoded), &read);
                decoded += read;
                if ((result != MA_SUCCESS && result != MA_AT_END) || read == 0u)
                {
                    break;
                }
            }
            std::array<float, 2u> extra{};
            ma_uint64 extraFrames = 0u;
            const ma_result result = ma_decoder_read_pcm_frames(&decoder, extra.data(), 1u, &extraFrames);
            valid = decoded == frames && extraFrames == 0u && (result == MA_SUCCESS || result == MA_AT_END)
                && (declaredFrames == 0u || decoded == declaredFrames)
                && std::all_of(clip->pcm.begin(), clip->pcm.end(), [](float value) { return std::isfinite(value); });
        }
        ma_decoder_uninit(&decoder);
        if (!valid)
        {
            state.lastError = "Invalid/truncated audio or resident 64 MiB budget exceeded; large clips require cooked streaming: " + path.string();
            return false;
        }
        // Authoring preview is fully validated and pinned as PCM. Runtime streams
        // must use the immutable cooked VFS rather than reopening mutable paths.
        state.clips[key] = std::move(clip);
        state.lastError.clear();
        return true;
    }

    bool MiniaudioBackend::LoadCookedClip(const ClipKey& key,
        const experiment::cooked::CookedAudioClipSource& source)
    {
        namespace ck = experiment::cooked;
        Implementation& state = *m_implementation;
        if (!state.accepting || !key.IsGuid() || key != ClipKey::FromGuid(source.Id().value))
        {
            return false;
        }
        const auto& metadata = source.Metadata();
        const bool oversized = metadata.channels == 0u || metadata.frameCount == 0u
            || metadata.frameCount > Implementation::kResidentPcmBudgetBytes / (metadata.channels * sizeof(float));
        const bool streaming = metadata.loadMode == ck::AudioLoadMode::Stream
            || (metadata.loadMode == ck::AudioLoadMode::Auto
                && (oversized || metadata.frameCount > static_cast<std::uint64_t>(metadata.sampleRate) * 10u));
        if (metadata.channels == 0u || metadata.channels > 2u || metadata.frameCount == 0u
            || metadata.sampleRate == 0u || source.PayloadSize() > static_cast<std::uint64_t>(std::numeric_limits<ma_int64>::max())
            || (!streaming && (oversized || source.PayloadSize() > Implementation::kResidentPcmBudgetBytes)))
        {
            state.lastError = "Invalid cooked audio metadata or resident 64 MiB budget exceeded";
            return false;
        }
        auto clip = std::make_shared<Implementation::Clip>();
        clip->cooked = true;
        clip->registry = &state.vfs;
        clip->info = { metadata.channels, metadata.sampleRate, metadata.frameCount, streaming };
        clip->path = "ceac:" + key.Text() + ":" + std::to_string(state.nextSource++);
        {
            std::lock_guard lock(state.vfs.registryMutex);
            state.vfs.sources.emplace(clip->path, std::make_shared<const ck::CookedAudioClipSource>(source));
        }
        auto removeSource = [&]()
        {
            std::lock_guard lock(state.vfs.registryMutex);
            state.vfs.sources.erase(clip->path);
        };
        ma_decoder decoder{};
        const ma_decoder_config config = ma_decoder_config_init(ma_format_f32, 0u, 0u);
        const ma_result initialized = ma_decoder_init_vfs(&state.vfs, clip->path.c_str(), &config, &decoder);
        if (initialized != MA_SUCCESS)
        {
            removeSource();
            state.lastError = std::string("Cannot open cooked audio decoder: ") + ma_result_description(initialized);
            return false;
        }
        ma_format format{};
        ma_uint32 channels = 0u;
        ma_uint32 sampleRate = 0u;
        ma_uint64 declaredFrames = 0u;
        bool valid = ma_decoder_get_data_format(&decoder, &format, &channels, &sampleRate, nullptr, 0u) == MA_SUCCESS
            && format == ma_format_f32 && channels == metadata.channels && sampleRate == metadata.sampleRate
            && ma_decoder_get_length_in_pcm_frames(&decoder, &declaredFrames) == MA_SUCCESS
            && declaredFrames == metadata.frameCount
            && ma_decoder_seek_to_pcm_frame(&decoder, 0u) == MA_SUCCESS;
        if (valid && streaming)
        {
            std::array<float, 2u> first{};
            ma_uint64 read = 0u;
            valid = ma_decoder_read_pcm_frames(&decoder, first.data(), 1u, &read) == MA_SUCCESS && read == 1u
                && std::all_of(first.begin(), first.end(), [](float value) { return std::isfinite(value); });
        }
        else if (valid)
        {
            clip->pcm.resize(static_cast<std::size_t>(metadata.frameCount * metadata.channels));
            ma_uint64 decoded = 0u;
            while (decoded < metadata.frameCount)
            {
                ma_uint64 read = 0u;
                const ma_result result = ma_decoder_read_pcm_frames(&decoder,
                    clip->pcm.data() + decoded * metadata.channels,
                    std::min<ma_uint64>(4096u, metadata.frameCount - decoded), &read);
                decoded += read;
                if ((result != MA_SUCCESS && result != MA_AT_END) || read == 0u)
                {
                    break;
                }
            }
            std::array<float, 2u> extra{};
            ma_uint64 extraFrames = 0u;
            const ma_result result = ma_decoder_read_pcm_frames(&decoder, extra.data(), 1u, &extraFrames);
            valid = decoded == metadata.frameCount && extraFrames == 0u && (result == MA_SUCCESS || result == MA_AT_END)
                && std::all_of(clip->pcm.begin(), clip->pcm.end(), [](float value) { return std::isfinite(value); });
        }
        ma_decoder_uninit(&decoder);
        if (!valid)
        {
            removeSource();
            state.lastError = "Cooked audio decoder differs from verified metadata or payload is truncated";
            return false;
        }
        // Keep only streaming entries registered. Resident voices pin decoded PCM.
        if (!streaming)
        {
            removeSource();
        }
        state.clips[key] = std::move(clip);
        state.lastError.clear();
        return true;
    }

    BackendClipId MiniaudioBackend::RetainClip(const ClipKey& key)
    {
        Implementation& state = *m_implementation;
        const auto found = state.clips.find(key);
        if (found == state.clips.end())
        {
            return {};
        }
        const std::uint64_t lease = state.nextClipLease++;
        state.clipLeases.emplace(lease, found->second);
        return BackendClipId{ lease };
    }

    void MiniaudioBackend::ReleaseClip(BackendClipId clip)
    {
        m_implementation->clipLeases.erase(clip.value);
    }

    void MiniaudioBackend::UnloadClip(const ClipKey& key)
    {
        m_implementation->clips.erase(key);
    }

    bool MiniaudioBackend::HasClip(const ClipKey& key) const
    {
        return m_implementation->clips.find(key) != m_implementation->clips.end();
    }

    BackendVoiceId MiniaudioBackend::StartVoice(const PlayRequest& request)
    {
        return StartVoice(request, {});
    }

    BackendVoiceId MiniaudioBackend::StartVoice(const PlayRequest& request, BackendClipId retained)
    {
        Implementation& state = *m_implementation;
        if (!state.accepting)
        {
            return {};
        }
        std::shared_ptr<Implementation::Clip> clip;
        if (retained.IsValid())
        {
            const auto found = state.clipLeases.find(retained.value);
            if (found == state.clipLeases.end())
            {
                return {};
            }
            clip = found->second;
        }
        else
        {
            const auto found = state.clips.find(request.clip);
            if (found == state.clips.end())
            {
                return {};
            }
            clip = found->second;
        }
        if (request.spatialBlend > 0.0f && clip->info.channels != 1u)
        {
            state.lastError = "A spatial point source requires an explicitly imported mono clip";
            return {};
        }
        std::size_t index = 0u;
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
        slot.clip = std::move(clip);
        slot.request = request;
        slot.request.pitch = std::isfinite(request.pitch) ? std::clamp(request.pitch, 0.01f, 4.0f) : 1.0f;
        for (Implementation::Source& source : slot.sources)
        {
            if (!state.InitializeSource(source, slot.clip, request.bus))
            {
                state.ReleaseSlot(index);
                return {};
            }
        }
        slot.initialized = true;
        slot.paused = false;
        slot.failed = false;
        state.Apply(slot);
        const ma_uint64 start = ma_engine_get_time_in_pcm_frames(&state.engine)
            + (state.actual.noDevice ? 0u : state.deviceDiagnostics.bufferFrames);
        for (Implementation::Source& source : slot.sources)
        {
            ma_sound_set_start_time_in_pcm_frames(&source.sound, start);
            if (ma_sound_start(&source.sound) != MA_SUCCESS)
            {
                state.ReleaseSlot(index);
                state.lastError = "Cannot start audio voice";
                return {};
            }
        }
        state.lastError.clear();
        return BackendVoiceId{ index + 1u };
    }

    void MiniaudioBackend::StopVoice(BackendVoiceId voice)
    {
        if (m_implementation->Resolve(voice) != nullptr)
        {
            m_implementation->ReleaseSlot(static_cast<std::size_t>(voice.value - 1u));
        }
    }

    void MiniaudioBackend::SetVoicePaused(BackendVoiceId voice, bool paused)
    {
        Implementation::VoiceSlot* slot = m_implementation->Resolve(voice);
        if (slot == nullptr)
        {
            return;
        }
        slot->paused = paused;
        for (Implementation::Source& source : slot->sources)
        {
            if (paused)
            {
                (void)ma_sound_stop(&source.sound);
            }
            else
            {
                (void)ma_sound_start(&source.sound);
            }
        }
    }

    bool MiniaudioBackend::IsVoicePlaying(BackendVoiceId voice) const
    {
        const Implementation::VoiceSlot* slot = m_implementation->Resolve(voice);
        return slot != nullptr && !slot->paused && !slot->failed
            && (ma_sound_at_end(&slot->sources[0].sound) == MA_FALSE
                || ma_sound_at_end(&slot->sources[1].sound) == MA_FALSE);
    }

    void MiniaudioBackend::SetVoiceVolume(BackendVoiceId voice, float gain)
    {
        if (Implementation::VoiceSlot* slot = m_implementation->Resolve(voice))
        {
            slot->request.volume = SafeGain(gain);
            m_implementation->Apply(*slot);
        }
    }

    void MiniaudioBackend::SetVoicePitch(BackendVoiceId voice, float pitch)
    {
        if (Implementation::VoiceSlot* slot = m_implementation->Resolve(voice))
        {
            slot->request.pitch = std::isfinite(pitch) ? std::clamp(pitch, 0.01f, 4.0f) : 1.0f;
            m_implementation->Apply(*slot);
        }
    }

    void MiniaudioBackend::SetVoiceTransform(BackendVoiceId voice,
        const math::vector3& position, const math::vector3& velocity)
    {
        if (Implementation::VoiceSlot* slot = m_implementation->Resolve(voice))
        {
            slot->request.position = position;
            slot->request.velocity = velocity;
            m_implementation->Apply(*slot);
        }
    }

    void MiniaudioBackend::SetVoiceSettings(BackendVoiceId voice, const PlayRequest& request)
    {
        if (Implementation::VoiceSlot* slot = m_implementation->Resolve(voice))
        {
            if (request.spatialBlend > 0.0f && slot->clip->info.channels != 1u)
            {
                m_implementation->lastError = "Spatial settings were rejected: a mono point source is required";
                return;
            }
            if (slot->request.bus != request.bus)
            {
                ma_sound_group* group = m_implementation->GroupFor(request.bus);
                if (group == nullptr)
                {
                    m_implementation->lastError = "Cannot reroute voice to destination bus";
                    return;
                }
                for (Implementation::Source& source : slot->sources)
                {
                    (void)ma_node_attach_output_bus(&source.splitter, 0u, group, 0u);
                }
            }
            slot->request = request;
            slot->request.pitch = std::isfinite(request.pitch) ? std::clamp(request.pitch, 0.01f, 4.0f) : 1.0f;
            m_implementation->Apply(*slot);
            m_implementation->lastError.clear();
        }
    }

    void MiniaudioBackend::SetVoiceLooping(BackendVoiceId voice, bool loop)
    {
        if (Implementation::VoiceSlot* slot = m_implementation->Resolve(voice))
        {
            slot->request.loop = loop;
            for (Implementation::Source& source : slot->sources)
            {
                ma_sound_set_looping(&source.sound, loop ? MA_TRUE : MA_FALSE);
            }
        }
    }

    bool MiniaudioBackend::SeekVoice(BackendVoiceId voice, std::uint64_t frame)
    {
        Implementation::VoiceSlot* slot = m_implementation->Resolve(voice);
        if (slot == nullptr || frame > slot->clip->info.frameCount)
        {
            return false;
        }
        bool success = true;
        for (Implementation::Source& source : slot->sources)
        {
            success = ma_sound_seek_to_pcm_frame(&source.sound, frame) == MA_SUCCESS && success;
        }
        return success;
    }

    std::uint64_t MiniaudioBackend::VoicePlayhead(BackendVoiceId voice) const
    {
        const Implementation::VoiceSlot* slot = m_implementation->Resolve(voice);
        ma_uint64 frame = 0u;
        if (slot != nullptr)
        {
            (void)ma_sound_get_cursor_in_pcm_frames(&slot->sources[0].sound, &frame);
        }
        return static_cast<std::uint64_t>(frame);
    }

    ClipInfo MiniaudioBackend::GetClipInfo(const ClipKey& key) const
    {
        const auto found = m_implementation->clips.find(key);
        return found != m_implementation->clips.end() ? found->second->info : ClipInfo{};
    }

    void MiniaudioBackend::SetBusVolume(BusId bus, float gain)
    {
        Implementation& state = *m_implementation;
        if (!bus.IsValid())
        {
            bus = Buses::Master;
        }
        gain = SafeGain(gain);
        state.busVolumes[bus.value] = gain;
        if (!state.engineReady)
        {
            return;
        }
        if (bus == Buses::Room)
        {
            SetReverbPreset(state.preset);
        }
        else if (ma_sound_group* group = state.GroupFor(bus))
        {
            ma_sound_group_set_volume(group, gain);
        }
        for (Implementation::VoiceSlot& slot : state.voices)
        {
            if (slot.initialized && slot.request.bus == bus)
            {
                state.Apply(slot);
            }
        }
    }

    void MiniaudioBackend::SetReverbPreset(ReverbPreset preset)
    {
        Implementation& state = *m_implementation;
        state.preset = preset;
        state.reverb.feedback.store(preset == ReverbPreset::Hall ? 0.88f : 0.72f, std::memory_order_relaxed);
        state.reverb.damping.store(preset == ReverbPreset::Hall ? 0.4f : 0.25f, std::memory_order_relaxed);
        if (state.reverb.ready)
        {
            const auto gain = state.busVolumes.find(Buses::Room.value);
            (void)ma_node_set_output_bus_volume(&state.reverb.node, 0u,
                preset == ReverbPreset::Off ? 0.0f : (gain != state.busVolumes.end() ? gain->second : 1.0f));
        }
    }

    void MiniaudioBackend::SetListener(const ListenerState& listener)
    {
        Implementation& state = *m_implementation;
        if (!state.engineReady)
        {
            return;
        }
        // Creator uses +Z forward. Reflect Z for every spatial vector before
        // entering the right-handed vendor listener/source space.
        ma_engine_listener_set_position(&state.engine, 0u, listener.position.x, listener.position.y, -listener.position.z);
        ma_engine_listener_set_velocity(&state.engine, 0u, listener.velocity.x, listener.velocity.y, -listener.velocity.z);
        ma_engine_listener_set_direction(&state.engine, 0u, listener.forward.x, listener.forward.y, -listener.forward.z);
        ma_engine_listener_set_world_up(&state.engine, 0u, listener.up.x, listener.up.y, -listener.up.z);
    }

    void MiniaudioBackend::Update()
    {
        Implementation& state = *m_implementation;
        state.RecoverDevice();
        for (Implementation::VoiceSlot& slot : state.voices)
        {
            if (!slot.initialized || slot.failed || !slot.clip->info.streaming)
            {
                continue;
            }
            for (Implementation::Source& source : slot.sources)
            {
                const ma_result result = ma_resource_manager_data_source_result(source.sound.pResourceManagerDataSource);
                if (result != MA_SUCCESS && result != MA_BUSY)
                {
                    slot.failed = true;
                    state.lastError = std::string("Audio stream decode failed: ") + ma_result_description(result);
                    break;
                }
            }
        }
    }

    void MiniaudioBackend::RequestDeviceRestart()
    {
        m_implementation->restartRequested.store(true, std::memory_order_release);
    }

    bool MiniaudioBackend::Render(float* output, std::uint32_t frames)
    {
        Implementation& state = *m_implementation;
        if (!state.accepting || !state.actual.noDevice || output == nullptr)
        {
            return false;
        }
        return ma_engine_read_pcm_frames(&state.engine, output, frames, nullptr) == MA_SUCCESS;
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
        std::uint64_t cumulative = 0u;
        for (const auto& bin : state.callbackHistogram)
        {
            result.count += bin.load(std::memory_order_relaxed);
        }
        const std::uint64_t rank = (result.count * 99u + 99u) / 100u;
        for (std::size_t index = 0u; index < state.callbackHistogram.size(); ++index)
        {
            cumulative += state.callbackHistogram[index].load(std::memory_order_relaxed);
            if (rank > 0u && cumulative >= rank)
            {
                result.p99UpperNanoseconds = (index + 1u) * Implementation::kHistogramStepNs;
                break;
            }
        }
        if (result.count > 0u)
        {
            result.meanNanoseconds = state.callbackNanoseconds.load(std::memory_order_relaxed) / result.count;
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
        AudioDeviceDiagnostics result = m_implementation->deviceDiagnostics;
        result.outputInterrupted = m_implementation->outputInterrupted.load(std::memory_order_acquire);
        result.rerouteCount = m_implementation->rerouteCount.load(std::memory_order_relaxed);
        result.restartAttempts = m_implementation->restartAttempts;
        result.successfulRestarts = m_implementation->successfulRestarts;
        return result;
    }

    std::uint64_t MiniaudioBackend::StreamReadFailures() const noexcept
    {
        return m_implementation->vfs.readFailures.load(std::memory_order_relaxed);
    }

    std::uint64_t MiniaudioBackend::StreamBytesRead() const noexcept
    {
        return m_implementation->vfs.bytesRead.load(std::memory_order_relaxed);
    }
}
