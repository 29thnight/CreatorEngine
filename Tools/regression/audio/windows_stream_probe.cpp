// Real WASAPI mixed resident/CEAC-stream workload. No device replacement or Pak stub.
#define main phase22_fixture_main
#include "../audio_voice_contract_probe.cpp"
#undef main
#define main phase22_cooked_main
#include "cooked_playback_probe.cpp"
#undef main

int main(int argc, char** argv)
{
    if (argc != 3)
    {
        return 2;
    }
    const auto work = file::absolute(argv[1]);
    const int seconds = std::stoi(argv[2]);
    if (work.generic_string().find("/Build/Validation/") == std::string::npos || seconds < 1 || seconds > 3600)
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
    Check(device.backend == "WASAPI", "Real WASAPI backend");
    ck::CookedAudioClipHeader header{};
    header.codec = assets::AudioCodec::Mp3;
    header.loadMode = ck::AudioLoadMode::Stream;
    header.channels = 1u;
    header.sampleRate = 44100u;
    header.frameCount = 55296u;
    std::shared_ptr<MemoryArtifact> memory;
    const auto source = MakeSource(fixture.root / "Formats" / "silent.mp3", header, memory);
    const wave::ClipKey streamKey = wave::ClipKey::FromGuid(Uuid::Parse("01234567-89ab-4cde-8fab-0123456789ab"));
    Check(runtime.LoadCookedClip(source), "CEAC MP3 stream load");
    const wave::ClipKey wavKey("hardware-wav");
    const wave::ClipKey flacKey("hardware-flac");
    Check(runtime.LoadClip(wavKey, fixture.assets / "Sounds" / "probe_tone.wav"), "Resident WAV load");
    Check(runtime.LoadClip(flacKey, fixture.root / "Formats" / "silent.flac"), "Resident FLAC load");
    wave::PlayRequest request{};
    request.clip = streamKey;
    request.loop = true;
    request.volume = 0.001f;
    const auto music = runtime.Play(request);
    Check(music.IsValid(), "Continuous MP3 stream voice admitted");
    const auto started = std::chrono::steady_clock::now();
    std::vector<wave::VoiceHandle> extras;
    const int counts[] = { 1, 32, 128 };
    int stage = -1;
    while (std::chrono::steady_clock::now() - started < std::chrono::seconds(seconds))
    {
        const auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - started).count();
        const int next = std::min(2, static_cast<int>(elapsed * 3 / seconds));
        if (next != stage)
        {
            for (const auto voice : extras)
            {
                runtime.Stop(voice);
            }
            extras.clear();
            stage = next;
            runtime.SetReverbPreset(stage > 0 ? wave::ReverbPreset::Hall : wave::ReverbPreset::Off);
            for (int index = 1; index < counts[stage]; ++index)
            {
                request.clip = index % 2 == 0 ? wavKey : flacKey;
                request.useReverbSend = stage > 0;
                request.reverbSendDecibels = -12.0f;
                const auto voice = runtime.Play(request);
                Check(voice.IsValid(), "Mixed resident voice admitted");
                extras.push_back(voice);
            }
            std::printf("WORKLOAD voices=%d continuous_mp3=true seconds=%lld\n", counts[stage], elapsed);
            std::fflush(stdout);
        }
        runtime.Update(0.01f);
        if (!runtime.IsAlive(music) || !backend.IsOutputAvailable())
        {
            Check(false, "Continuous stream and physical output retained");
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    const auto metrics = backend.CallbackMetrics();
    const auto settings = backend.ActualSettings();
    Check(backend.StreamBytesRead() > 0 && memory->reads > 1, "Actual bounded CEAC streaming reads");
    Check(backend.StreamReadFailures() == 0, "No stream I/O failures");
    Check(metrics.count > 0 && metrics.minimumFrames > 0 && metrics.p99UpperNanoseconds <
        static_cast<std::uint64_t>(metrics.minimumFrames) * 1000000000ull / settings.sampleRate / 2,
        "Callback p99 below half minimum callback period");
    std::printf("METRICS seconds=%d callbacks=%llu p99_ns=%llu max_ns=%llu half_exceeded=%llu full_exceeded=%llu stream_bytes=%llu failures=%llu\n",
        seconds, metrics.count, metrics.p99UpperNanoseconds, metrics.maxNanoseconds,
        metrics.overHalfPeriod, metrics.overFullPeriod, backend.StreamBytesRead(), backend.StreamReadFailures());
    runtime.Shutdown();
    std::printf("SUMMARY checks=%u passed=%u failed=%u\n", checks, checks - failures, failures);
    std::printf("EXCLUSIONS hardware-underrun/loopback/listening/Pak/Editor/CLR/physical-unplug\n");
    return failures ? 1 : 0;
}
