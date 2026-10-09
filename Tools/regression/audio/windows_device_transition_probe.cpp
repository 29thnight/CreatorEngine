#define main phase22_contract_main
#include "../audio_voice_contract_probe.cpp"
#undef main

#include <chrono>
#include <cstdio>
#include <fstream>
#include <thread>

int main(int argc, char** argv)
{
    if (argc != 3)
    {
        return 2;
    }
    const std::filesystem::path work(argv[1]);
    const int seconds = std::atoi(argv[2]);
    if (work.generic_string().find("/Build/Validation/") == std::string::npos || seconds < 1 || seconds > 600)
    {
        return 2;
    }
    std::filesystem::create_directories(work);
    std::filesystem::current_path(work);
    Fixture fixture{};
    if (!PrepareFixture(work, true, fixture))
    {
        return 2;
    }
    wave::MiniaudioBackend backend(true);
    wave::AudioRuntime runtime(backend, 128);
    Report(runtime.Start({}), "real-device runtime start");
    Report(backend.DeviceDiagnostics().backend == "WASAPI", "actual WASAPI backend");
    const wave::ClipKey key("transition-tone");
    Report(runtime.LoadClip(key, fixture.assets / "Sounds" / "probe_tone.wav"), "resident WAV load");
    wave::PlayRequest request{};
    request.clip = key;
    request.loop = true;
    request.volume = 0.001f;
    const auto voice = runtime.Play(request);
    Report(voice.IsValid(), "logical voice created");
    std::ofstream(work / "ready.txt") << backend.DeviceDiagnostics().deviceName;
    std::uint64_t observedReroutes = 0;
    std::uint64_t observedRestarts = 0;
    bool interrupted = false;
    unsigned unavailableTicks = 0;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(seconds);
    while (std::chrono::steady_clock::now() < deadline)
    {
        runtime.Update(0.01f);
        const auto state = backend.DeviceDiagnostics();
        const bool unavailable = !backend.IsOutputAvailable();
        if (unavailable)
        {
            ++unavailableTicks;
        }
        if (state.rerouteCount != observedReroutes || state.successfulRestarts != observedRestarts || interrupted != unavailable)
        {
            observedReroutes = state.rerouteCount;
            observedRestarts = state.successfulRestarts;
            interrupted = unavailable;
            std::printf("TRANSITION reroutes=%llu restarts=%llu available=%d name=%s alive=%d\n",
                observedReroutes, observedRestarts, !unavailable, state.deviceName.c_str(), runtime.IsAlive(voice));
            std::fflush(stdout);
        }
        if (!runtime.IsAlive(voice))
        {
            Report(false, "logical voice survived endpoint transition");
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    Report(backend.IsOutputAvailable(), "physical output available after transition sequence");
    Report(runtime.IsAlive(voice), "logical voice retained after transition sequence");
    std::printf("METRICS reroutes=%llu restarts=%llu unavailable_ticks=%u callbacks=%llu\n",
        observedReroutes, observedRestarts, unavailableTicks, backend.CallbackMetrics().count);
    runtime.Shutdown();
    std::printf("SUMMARY checks=%d passed=%d failed=%d\n", g_checks, g_checks - g_failures, g_failures);
    return g_failures ? 1 : 0;
}
