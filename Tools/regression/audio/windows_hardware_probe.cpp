// Actual WASAPI output and controlled reopen; not a physical unplug test.
// Reuse the deterministic fixture generator and exact engine-source contracts.
#define main phase22_contract_main
#include "../audio_voice_contract_probe.cpp"
#undef main
#include <windows.h>
#include <psapi.h>

int main(int argc, char** argv)
{
    if (argc != 3)
    {
        return 2;
    }
    const auto work = file::absolute(argv[1]);
    if (work.generic_string().find("/Build/Validation/") == std::string::npos)
    {
        std::fprintf(stderr, "Owned Build/Validation work directory required\n");
        return 2;
    }
    const int seconds = std::stoi(argv[2]);
    if (seconds < 1 || seconds > 3600)
    {
        return 2;
    }
    Fixture fixture{};
    if (!PrepareFixture(work, true, fixture))
    {
        return 2;
    }
    wave::MiniaudioBackend backend(true);
    wave::AudioRuntime runtime(backend, 128);
    if (!runtime.Start({}))
    {
        std::printf("DEVICE_UNAVAILABLE %s\n", backend.LastError().c_str());
        return 3;
    }
    const auto device = backend.DeviceDiagnostics();
    std::printf("DEVICE backend=%s name=%s frames=%u buffer=%u\n",
        device.backend.c_str(), device.deviceName.c_str(), device.periodFrames, device.bufferFrames);
    Report(device.backend == "WASAPI" || device.backend == "wasapi", "actual WASAPI backend");
    Report(backend.IsOutputAvailable(), "physical output available");
    const wave::ClipKey key("hardware-tone");
    Report(runtime.LoadClip(key, fixture.assets / "Sounds" / "probe_tone.wav"), "WAV load");
    wave::PlayRequest request{};
    request.clip = key;
    request.loop = true;
    request.volume = 0.001f;
    const auto restartVoice = runtime.Play(request);
    Report(restartVoice.IsValid(), "restart voice created");
    for (int iteration = 0; iteration < 20; ++iteration)
    {
        const auto before = backend.DeviceDiagnostics().successfulRestarts;
        backend.RequestDeviceRestart();
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        do
        {
            runtime.Update(0.01f);
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        } while (backend.DeviceDiagnostics().successfulRestarts == before &&
            std::chrono::steady_clock::now() < deadline);
        Report(backend.IsOutputAvailable() && backend.DeviceDiagnostics().successfulRestarts > before,
            "controlled real-device reopen succeeds");
        Report(runtime.IsAlive(restartVoice), "voice survives real-device reopen");
    }
    runtime.Stop(restartVoice);
    const auto started = std::chrono::steady_clock::now();
    const int counts[] = { 0, 1, 32, 128 };
    int stage = -1;
    std::vector<wave::VoiceHandle> voices;
    SIZE_T peakPrivate = 0;
    while (std::chrono::steady_clock::now() - started < std::chrono::seconds(seconds))
    {
        const auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::steady_clock::now() - started).count();
        const int next = std::min(3, static_cast<int>(elapsed * 4 / seconds));
        if (next != stage)
        {
            for (const auto voice : voices)
            {
                runtime.Stop(voice);
            }
            voices.clear();
            stage = next;
            backend.SetReverbPreset(stage >= 2 ? wave::ReverbPreset::Hall : wave::ReverbPreset::Off);
            request.useReverbSend = stage >= 2;
            request.reverbSendDecibels = -12.0f;
            for (int index = 0; index < counts[stage]; ++index)
            {
                const auto voice = runtime.Play(request);
                Report(voice.IsValid(), "workload voice admitted");
                voices.push_back(voice);
            }
            std::printf("WORKLOAD voices=%d seconds=%lld\n", counts[stage], elapsed);
            std::fflush(stdout);
        }
        runtime.Update(0.01f);
        PROCESS_MEMORY_COUNTERS_EX memory{};
        memory.cb = sizeof(memory);
        if (GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memory), sizeof(memory)))
        {
            peakPrivate = std::max(peakPrivate, memory.PrivateUsage);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    const auto metrics = backend.CallbackMetrics();
    const auto settings = backend.ActualSettings();
    Report(metrics.count > 0, "real device callbacks observed");
    Report(metrics.minimumFrames > 0 && metrics.p99UpperNanoseconds <
        static_cast<std::uint64_t>(metrics.minimumFrames) * 1000000000ull / settings.sampleRate / 2,
        "callback p99 below half minimum observed callback period");
    std::printf("METRICS seconds=%d callbacks=%llu p99_ns=%llu max_ns=%llu half_exceeded=%llu full_exceeded=%llu private_peak=%llu\n",
        seconds, metrics.count, metrics.p99UpperNanoseconds, metrics.maxNanoseconds,
        metrics.overHalfPeriod, metrics.overFullPeriod, static_cast<unsigned long long>(peakPrivate));
    runtime.Shutdown();
    for (int iteration = 0; iteration < 100; ++iteration)
    {
        Report(runtime.Start({}), "real-device host cycle start");
        runtime.Shutdown();
    }
    std::printf("SUMMARY checks=%d passed=%d failed=%d\n", g_checks, g_checks - g_failures, g_failures);
    std::printf("EXCLUSIONS physical-unplug/default-device-switch/loopback/listening/stream/Editor/CLR/package\n");
    return g_failures ? 1 : 0;
}
