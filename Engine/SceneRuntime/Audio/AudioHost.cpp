#include "AudioHost.h"

#include "NullAudioBackend.h"
#include "MiniaudioBackend.h"

#include <chrono>

namespace wave
{
    AudioHost::AudioHost(std::unique_ptr<AudioBackend> deviceBackend, std::size_t voiceCapacity)
        : m_deviceBackend(std::move(deviceBackend))
        , m_nullBackend(std::make_unique<NullAudioBackend>())
    {
        if (m_deviceBackend)
        {
            m_deviceRuntime = std::make_unique<AudioRuntime>(*m_deviceBackend, voiceCapacity);
        }
        // Reserve the high generation bit for fallback voices. An old Null
        // handle cannot alias a newly started device voice after recovery.
        m_nullRuntime = std::make_unique<AudioRuntime>(
            *m_nullBackend, voiceCapacity, 0x80000000u);
    }

    AudioHost::~AudioHost()
    {
        Shutdown();
    }

    bool AudioHost::Start(const DeviceSettings& settings)
    {
        if (m_active)
        {
            return true;
        }

        if (m_deviceRuntime && m_deviceRuntime->Start(settings))
        {
            m_active = m_deviceRuntime.get();
            m_mode = settings.noDevice ? AudioHostMode::Offline : AudioHostMode::Device;
            return true;
        }

        // A backend may have acquired resources before reporting failure.
        if (m_deviceBackend)
        {
            m_deviceBackend->Stop();
        }

        if (m_nullRuntime->Start(settings))
        {
            m_active = m_nullRuntime.get();
            m_mode = AudioHostMode::Null;
            return true;
        }

        m_nullBackend->Stop();
        return false;
    }

    void AudioHost::Update(float deltaSeconds)
    {
        const auto started = std::chrono::steady_clock::now();
        if (m_active)
        {
            m_active->Update(deltaSeconds);
        }
        m_updateNanoseconds = static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now() - started).count());
    }

    void AudioHost::Shutdown()
    {
        if (m_active)
        {
            m_active->Shutdown();
        }
        m_active = nullptr;
        m_mode = AudioHostMode::Stopped;
    }

    AudioHostMode AudioHost::Mode() const noexcept
    {
        if (m_mode == AudioHostMode::Device && m_deviceBackend && !m_deviceBackend->IsOutputAvailable())
        {
            return AudioHostMode::DegradedDevice;
        }
        return m_mode;
    }

    AudioHostCounters AudioHost::Counters() const
    {
        AudioHostCounters result;
        result.runtimeUpdateNanoseconds = m_updateNanoseconds;
        if (m_active)
        {
            result.voices = m_active->Metrics();
        }
        if (m_mode == AudioHostMode::Device || m_mode == AudioHostMode::Offline)
        {
            if (const auto* backend = dynamic_cast<const MiniaudioBackend*>(m_deviceBackend.get()))
            {
                const auto callback = backend->CallbackMetrics();
                result.backendCountersAvailable = true;
                result.callbackCount = callback.count;
                result.callbackP99Nanoseconds = callback.p99UpperNanoseconds;
                result.callbackMaxNanoseconds = callback.maxNanoseconds;
                result.callbackOverHalfPeriod = callback.overHalfPeriod;
                result.streamBytesRead = backend->StreamBytesRead();
                result.streamReadFailures = backend->StreamReadFailures();
            }
        }
        return result;
    }

}
