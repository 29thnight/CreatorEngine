#pragma once

#include <cstddef>
#include <memory>

#include "AudioRuntime.h"

namespace wave
{
    enum class AudioHostMode
    {
        Stopped,
        Device,
        Null,
        DegradedDevice,
        Offline,
    };

    struct AudioHostCounters final
    {
        VoiceMetrics voices;
        std::uint64_t runtimeUpdateNanoseconds{};
        std::uint64_t callbackCount{};
        std::uint64_t callbackP99Nanoseconds{};
        std::uint64_t callbackMaxNanoseconds{};
        std::uint64_t callbackOverHalfPeriod{};
        std::uint64_t streamBytesRead{};
        std::uint64_t streamReadFailures{};
        bool backendCountersAvailable{};
    };

    // Host-owned audio lifetime. A failed device start is stopped before the
    // Null backend starts. Runtime instances survive Start/Shutdown cycles so
    // their voice generations cannot restart at one within this Host. The
    // fallback runtime has a distinct generation namespace so recovered
    // device voices cannot inherit a stale Null voice handle.
    class AudioHost final
    {
    public:
        AudioHost(std::unique_ptr<AudioBackend> deviceBackend, std::size_t voiceCapacity);
        ~AudioHost();

        AudioHost(const AudioHost&) = delete;
        AudioHost& operator=(const AudioHost&) = delete;

        [[nodiscard]] bool Start(const DeviceSettings& settings);
        void Update(float deltaSeconds);
        void Shutdown();

        [[nodiscard]] AudioService* Service() noexcept { return m_active; }
        [[nodiscard]] const AudioService* Service() const noexcept { return m_active; }
        [[nodiscard]] AudioHostMode Mode() const noexcept;
        [[nodiscard]] AudioHostCounters Counters() const;

    private:
        std::unique_ptr<AudioBackend> m_deviceBackend;
        std::unique_ptr<AudioBackend> m_nullBackend;
        std::unique_ptr<AudioRuntime> m_deviceRuntime;
        std::unique_ptr<AudioRuntime> m_nullRuntime;
        AudioRuntime* m_active{ nullptr };
        AudioHostMode m_mode{ AudioHostMode::Stopped };
        std::uint64_t m_updateNanoseconds{};
    };
}
