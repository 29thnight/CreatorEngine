// Compile exact MiniaudioBackend.cpp with no eligible output backend, not a fake.
#include "Audio/AudioRuntime.h"
#include "Audio/AudioHost.h"
#include "Audio/MiniaudioBackend.h"

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <thread>

namespace
{
    unsigned checks = 0u;
    unsigned failures = 0u;

    void Check(bool condition, const char* label)
    {
        ++checks;
        failures += condition ? 0u : 1u;
        std::printf("[%s] %s\n", condition ? "PASS" : "FAIL", label);
    }
}

int main(int argc, char** argv)
{
    if (argc != 2)
    {
        return 2;
    }
    wave::MiniaudioBackend backend;
    wave::AudioRuntime runtime(backend, 4u);
    Check(runtime.Start({}) && backend.IsRunning() && !backend.IsOutputAvailable(),
        "Zero eligible device backends retains running silent decoder/mixer graph");
    Check(backend.DeviceDiagnostics().outputInterrupted,
        "Cold-start output failure is visible as interrupted output, never device success");
    const wave::ClipKey key("cold-start-tone");
    Check(runtime.LoadClip(key, std::filesystem::path(argv[1]) / "sine.wav"),
        "Cold-start degraded engine can still load/decode clips");
    wave::PlayRequest request;
    request.clip = key;
    const auto shot = runtime.Play(request);
    request.loop = true;
    const auto loop = runtime.Play(request);
    Check(shot.IsValid() && loop.IsValid(), "Degraded engine accepts independent one-shot and loop handles");
    std::this_thread::sleep_for(std::chrono::milliseconds(180));
    runtime.Update(0.18f);
    Check(!runtime.IsAlive(shot) && runtime.IsAlive(loop),
        "Silent advancement completes known one-shot while preserving loop handle");
    std::this_thread::sleep_for(std::chrono::milliseconds(1100));
    runtime.Update(1.1f);
    const auto state = backend.DeviceDiagnostics();
    Check(state.restartAttempts >= 1u && state.successfulRestarts == 0u && state.outputInterrupted
        && runtime.IsAlive(loop), "Bounded delayed retry preserves same logical loop when output remains unavailable");
    runtime.Shutdown();
    Check(!backend.IsRunning() && runtime.AliveVoiceCount() == 0u,
        "Degraded cold-start shutdown drains engine without a device");
    wave::AudioHost host(std::make_unique<wave::MiniaudioBackend>(), 4u);
    Check(host.Start({}) && host.Mode() == wave::AudioHostMode::DegradedDevice,
        "AudioHost exposes real cold-start failure as DegradedDevice rather than Null or Device");
    auto* service = host.Service();
    const bool loaded = service && service->LoadClip(key, std::filesystem::path(argv[1]) / "sine.wav");
    const auto ownedLoop = loaded ? service->Play(request) : wave::VoiceHandle{};
    host.Update(0.01f);
    Check(ownedLoop.IsValid() && host.Service() == service && service->IsAlive(ownedLoop)
        && host.Mode() == wave::AudioHostMode::DegradedDevice,
        "Degraded Host retains identical service and looping handle across update");
    host.Shutdown();
    Check(host.Service() == nullptr && host.Mode() == wave::AudioHostMode::Stopped,
        "Degraded Host shutdown clears service visibility");
    std::printf("SUMMARY checks=%u passed=%u failed=%u physical_device=not-tested\n", checks, checks - failures, failures);
    return failures ? 1 : 0;
}
