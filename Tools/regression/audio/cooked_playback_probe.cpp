// Exact-source CEAC -> miniaudio resident/stream integration; no Pak substitute.
#include "Audio/AudioRuntime.h"
#include "Audio/MiniaudioBackend.h"
#include "Audio/NullAudioBackend.h"
#include "Experiment/Cooked/CookedAudioClipSource.h"

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <thread>

namespace
{
    namespace ck = experiment::cooked;
    namespace fs = std::filesystem;
    unsigned checks = 0u;
    unsigned failures = 0u;

    void Check(bool condition, const char* label)
    {
        ++checks;
        failures += condition ? 0u : 1u;
        std::printf("[%s] %s\n", condition ? "PASS" : "FAIL", label);
    }

    class MemoryArtifact final : public ck::ArtifactByteSource
    {
    public:
        std::vector<std::byte> bytes;
        std::string path;
        mutable std::atomic<std::uint64_t> reads{ 0u };
        std::atomic<bool> failReads{ false };

        bool Size(std::string_view requested, std::uint64_t& out, std::string& error) const override
        {
            if (requested != path)
            {
                error = "Unknown test artifact";
                return false;
            }
            out = bytes.size();
            return true;
        }

        bool ReadAt(std::string_view requested, std::uint64_t offset,
            std::span<std::byte> out, std::string& error) const override
        {
            ++reads;
            if (failReads || requested != path || offset > bytes.size() || out.size() > bytes.size() - offset)
            {
                error = "Test artifact read failed or exceeded bounds";
                return false;
            }
            std::copy_n(bytes.begin() + static_cast<std::ptrdiff_t>(offset), out.size(), out.begin());
            return true;
        }
    };

    ck::CookedAudioClipSource MakeSource(const fs::path& input, ck::CookedAudioClipHeader header,
        std::shared_ptr<MemoryArtifact>& memory)
    {
        std::ifstream stream(input, std::ios::binary);
        std::vector<char> raw{ std::istreambuf_iterator<char>(stream), {} };
        header.payloadBytes = raw.size();
        header.payloadSha256 = Hash::Sha256::Compute(raw.data(), raw.size());
        auto encoded = ck::WriteAudioClipHeader(header);
        memory = std::make_shared<MemoryArtifact>();
        memory->bytes.assign(encoded.begin(), encoded.end());
        for (const char byte : raw)
        {
            memory->bytes.push_back(std::byte(static_cast<unsigned char>(byte)));
        }
        const experiment::AssetId id{ Uuid::Parse("01234567-89ab-4cde-8fab-0123456789ab") };
        ck::CookedAssetManifestEntry entry;
        entry.assetId = id;
        entry.kind = ck::CookedAssetKind::AudioClip;
        entry.formatVersion = ck::kAudioClipArtifactVersion;
        entry.byteSize = memory->bytes.size();
        entry.artifactPath = ck::MakeDerivedAudioClipArtifactPath(id);
        entry.contentSha256 = Hash::Sha256::Compute(memory->bytes.data(), memory->bytes.size());
        memory->path = entry.artifactPath;
        ck::CookedAudioClipSource source;
        std::string error;
        Check(ck::OpenCookedAudioClipEntry(entry, memory, source, error), "Real CEAC source verifies header/extent and both SHA256 digests");
        return source;
    }

    bool RenderBlocks(wave::MiniaudioBackend& backend, wave::AudioRuntime& audio,
        unsigned count, double& energy)
    {
        std::vector<float> pcm(1024u * 2u);
        bool valid = true;
        energy = 0.0;
        for (unsigned block = 0u; block < count; ++block)
        {
            // Yield to bounded resource workers; this is an offline throughput
            // contract, not a real-time device underrun or latency measurement.
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            valid = backend.Render(pcm.data(), 1024u) && valid;
            for (const float sample : pcm)
            {
                valid = valid && std::isfinite(sample);
                energy += static_cast<double>(sample) * sample;
            }
            audio.Update(1024.0f / 48000.0f);
        }
        return valid;
    }
}

int main(int argc, char** argv)
{
    if (argc != 2)
    {
        std::fprintf(stderr, "usage: cooked_playback_probe GENERATED_FORMAT_DIRECTORY\n");
        return 2;
    }
    const fs::path formats(argv[1]);
    struct Format
    {
        const char* name;
        assets::AudioCodec codec;
        unsigned sampleRate;
        std::uint64_t frames;
    };
    for (const auto& format : { Format{ "sine.wav", assets::AudioCodec::Wav, 48000u, 4800u },
        Format{ "silent.mp3", assets::AudioCodec::Mp3, 44100u, 55296u },
        Format{ "silent.flac", assets::AudioCodec::Flac, 48000u, 49152u } })
    {
        for (const auto mode : { ck::AudioLoadMode::Resident, ck::AudioLoadMode::Stream })
        {
            std::printf("FORMAT %s load=%s\n", format.name,
                mode == ck::AudioLoadMode::Stream ? "stream" : "resident");
            wave::MiniaudioBackend backend;
            wave::AudioRuntime audio(backend, 4u);
            wave::DeviceSettings settings;
            settings.noDevice = true;
            Check(audio.Start(settings), "No-device miniaudio starts with resource workers");
            ck::CookedAudioClipHeader header;
            header.codec = format.codec;
            header.loadMode = mode;
            header.spatialKind = ck::AudioSpatialKind::PointMono;
            header.channels = 1u;
            header.sampleRate = format.sampleRate;
            header.frameCount = format.frames;
            std::shared_ptr<MemoryArtifact> memory;
            auto source = MakeSource(formats / format.name, header, memory);
            Check(audio.LoadCookedClip(source), "Cooked source loads through exact production decoder");
            wave::PlayRequest request;
            request.clip = wave::ClipKey::FromGuid(source.Id().value);
            request.loop = true;
            const auto voice = audio.Play(request);
            Check(voice.IsValid(), "Cooked clip produces valid logical voice");
            double energy = 0.0;
            Check(RenderBlocks(backend, audio, 64u, energy) && audio.IsAlive(voice),
                "Cooked looping decoder renders finite PCM across multiple page/loop boundaries");
            Check(format.codec == assets::AudioCodec::Wav ? energy > 1.0 : energy < 0.000001,
                "Decoded cooked PCM matches non-silent WAV / silent MP3 and FLAC fixture");
            if (mode == ck::AudioLoadMode::Stream)
            {
                Check(backend.StreamBytesRead() > 0u && backend.StreamReadFailures() == 0u,
                    "Stream uses mounted byte source successfully rather than resident decoder shortcut");
            }
            audio.SetPaused(voice, true);
            const auto paused = audio.GetPlayhead(voice);
            Check(RenderBlocks(backend, audio, 4u, energy) && audio.GetPlayhead(voice) == paused,
                "Cooked stream/resident pause freezes source-frame playhead");
            audio.SetPaused(voice, false);
            Check(audio.Seek(voice, format.frames / 2u), "Cooked decoder accepts source-frame seek");
            Check(RenderBlocks(backend, audio, 8u, energy) && audio.IsAlive(voice),
                "Cooked decoder continues across seek and loop");
            auto weak = std::weak_ptr<MemoryArtifact>(memory);
            source = {};
            memory.reset();
            Check(mode != ck::AudioLoadMode::Stream || !weak.expired(),
                "Streaming playback pins mounted bytes after caller releases its source");
            audio.UnloadClip(request.clip);
            Check(!audio.IsAlive(voice) && audio.ListClipKeys().empty(), "Unload stops voices and releases registered clip");
            audio.Shutdown();
            Check(weak.expired() && !backend.IsRunning(), "Shutdown drains resource workers and releases mounted byte ownership");
        }
        // Bad decoded metadata must fail before an accepted playable resource exists.
        wave::MiniaudioBackend backend;
        wave::AudioRuntime audio(backend, 2u);
        wave::DeviceSettings settings;
        settings.noDevice = true;
        Check(audio.Start(settings), "Mismatch-validation backend starts");
        ck::CookedAudioClipHeader header;
        header.codec = format.codec;
        header.loadMode = ck::AudioLoadMode::Resident;
        header.spatialKind = ck::AudioSpatialKind::PointMono;
        header.channels = 1u;
        header.sampleRate = format.sampleRate;
        header.frameCount = format.frames + 123u;
        std::shared_ptr<MemoryArtifact> memory;
        auto source = MakeSource(formats / format.name, header, memory);
        Check(!audio.LoadCookedClip(source), "Cooked resident rejects mismatched decoded PCM length");
        header.frameCount = format.frames;
        header.sampleRate += 1u;
        source = MakeSource(formats / format.name, header, memory);
        Check(!audio.LoadCookedClip(source), "Cooked resident rejects mismatched decoded sample rate");
        header.sampleRate = format.sampleRate;
        header.frameCount = 64ull * 1024ull * 1024ull / 4ull + 1ull;
        source = MakeSource(formats / format.name, header, memory);
        Check(!audio.LoadCookedClip(source), "Oversize resident metadata rejected before allocation");
        audio.Shutdown();
    }
    {
        wave::MiniaudioBackend backend;
        wave::AudioRuntime audio(backend, 2u);
        wave::DeviceSettings settings;
        settings.noDevice = true;
        Check(audio.Start(settings), "Clip generation lease backend starts");
        ck::CookedAudioClipHeader header;
        header.codec = assets::AudioCodec::Wav;
        header.loadMode = ck::AudioLoadMode::Resident;
        header.spatialKind = ck::AudioSpatialKind::PointMono;
        header.channels = 1u;
        header.sampleRate = 48000u;
        header.frameCount = 2400u;
        std::shared_ptr<MemoryArtifact> firstMemory;
        auto firstSource = MakeSource(formats / "loop.wav", header, firstMemory);
        Check(audio.LoadCookedClip(firstSource), "Original content generation loads");
        wave::PlayRequest request;
        request.clip = wave::ClipKey::FromGuid(firstSource.Id().value);
        request.loop = true;
        const auto voice = audio.Play(request);
        audio.SetPhysicalVoiceLimit(0u);
        Check(audio.GetVoiceState(voice) == wave::VoiceState::Virtual,
            "Original content voice virtualizes before same-GUID replacement");
        header.frameCount = 4800u;
        std::shared_ptr<MemoryArtifact> secondMemory;
        auto secondSource = MakeSource(formats / "silence.wav", header, secondMemory);
        Check(audio.LoadCookedClip(secondSource), "Same GUID publishes new silent content generation");
        Check(audio.Seek(voice, 3500u) && audio.GetPlayhead(voice) == 1100u,
            "Virtual old voice retains original 2400-frame metadata after GUID replacement");
        firstSource = {};
        auto oldLease = std::weak_ptr<MemoryArtifact>(firstMemory);
        firstMemory.reset();
        // Resident content owns decoded PCM, so encoded-byte ownership may
        // end here. The rehydrated PCM assertion below proves the old lease.

        audio.SetPhysicalVoiceLimit(1u);
        audio.Update(0.0f);
        double energy = 0.0;
        Check(audio.GetVoiceState(voice) == wave::VoiceState::Physical
            && RenderBlocks(backend, audio, 8u, energy) && energy > 1.0,
            "Rehydration uses original non-silent source rather than replacement GUID content");
        audio.Stop(voice);
        const auto replacementVoice = audio.Play(request);
        (void)RenderBlocks(backend, audio, 1u, energy); // Drain at most one cached node block from old source.
        Check(replacementVoice.IsValid() && RenderBlocks(backend, audio, 8u, energy) && energy < 0.000001,
            "New plays use newly published silent content generation");
        audio.Shutdown();
        Check(oldLease.expired(), "Final release retires old generation lease");
        wave::NullAudioBackend nullBackend;
        wave::AudioRuntime nullAudio(nullBackend, 2u);
        Check(nullAudio.Start({}) && nullAudio.LoadCookedClip(secondSource),
            "Logical Null backend knows cooked source duration");
        request.loop = false;
        bool cleaned = true;
        for (unsigned index = 0u; index < 128u; ++index)
        {
            const auto shot = nullAudio.Play(request);
            cleaned = cleaned && shot.IsValid();
            nullAudio.Update(0.101f);
            cleaned = cleaned && !nullAudio.IsAlive(shot) && nullAudio.AliveVoiceCount() == 0u;
        }
        Check(cleaned, "Known-length Null one-shots naturally recycle beyond fixed voice capacity");
        nullAudio.Shutdown();
    }
    {
        // A long generated source forces reads after the initial 250ms pages.
        std::ifstream input(formats / "sine.wav", std::ios::binary);
        const std::vector<char> shortWave{ std::istreambuf_iterator<char>(input), {} };
        std::vector<char> longWave(shortWave.begin(), shortWave.begin() + 44u);
        for (unsigned repeat = 0u; repeat < 20u; ++repeat)
        {
            longWave.insert(longWave.end(), shortWave.begin() + 44u, shortWave.end());
        }
        const auto put = [&](std::size_t offset, std::uint32_t value)
        {
            for (unsigned index = 0u; index < 4u; ++index)
            {
                longWave[offset + index] = static_cast<char>(value >> (index * 8u));
            }
        };
        put(4u, static_cast<std::uint32_t>(longWave.size() - 8u));
        put(40u, static_cast<std::uint32_t>(longWave.size() - 44u));
        const auto longPath = formats / "stream-failure-generated.wav";
        {
            std::ofstream output(longPath, std::ios::binary);
            output.write(longWave.data(), static_cast<std::streamsize>(longWave.size()));
        }
        ck::CookedAudioClipHeader header;
        header.codec = assets::AudioCodec::Wav;
        header.loadMode = ck::AudioLoadMode::Stream;
        header.spatialKind = ck::AudioSpatialKind::PointMono;
        header.channels = 1u;
        header.sampleRate = 48000u;
        header.frameCount = 96000u;
        std::shared_ptr<MemoryArtifact> memory;
        auto source = MakeSource(longPath, header, memory);
        wave::MiniaudioBackend backend;
        wave::AudioRuntime audio(backend, 2u);
        wave::DeviceSettings settings;
        settings.noDevice = true;
        Check(audio.Start(settings) && audio.LoadCookedClip(source),
            "Long mounted stream opens for injected read-failure contract");
        wave::PlayRequest request;
        request.clip = wave::ClipKey::FromGuid(source.Id().value);
        request.loop = true;
        const auto voice = audio.Play(request);
        double energy = 0.0;
        Check(voice.IsValid() && RenderBlocks(backend, audio, 4u, energy),
            "Long stream renders initial prefetched pages");
        memory->failReads = true;
        const bool finite = RenderBlocks(backend, audio, 200u, energy);
        Check(finite && backend.StreamReadFailures() > 0u,
            "Injected mounted-source I/O failure is counted and leaves finite mixer output");
        Check(!audio.IsAlive(voice), "Failed asynchronous stream retires logical voice instead of hanging playback");
        audio.Shutdown();
        Check(!backend.IsRunning(), "Shutdown drains failed-stream workers within process deadline");
    }
    std::printf("SUMMARY checks=%u passed=%u failed=%u physical_device=not-tested pak=not-tested\n",
        checks, checks - failures, failures);
    return failures ? 1 : 0;
}
