#include "Experiment/Cooked/CookedAssetCatalog.h"
#include "Experiment/Cooked/CookedAudioClipSource.h"
#include "Experiment/Cooked/PakAudioClipByteSource.h"
#include "Audio/AudioRuntime.h"
#include "Audio/MiniaudioBackend.h"
#include "Audio/NullAudioBackend.h"

#include <array>
#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace
{
    namespace ck = experiment::cooked;
    namespace fs = std::filesystem;

    std::vector<std::byte> ReadAll(const fs::path& path)
    {
        std::ifstream input(path, std::ios::binary | std::ios::ate);
        if (!input) throw std::runtime_error("fixture cannot be opened");
        const std::streamoff length = input.tellg();
        if (length < 0) throw std::runtime_error("fixture size is unavailable");
        std::vector<std::byte> bytes(static_cast<std::size_t>(length));
        input.seekg(0);
        if (!bytes.empty()) input.read(reinterpret_cast<char*>(bytes.data()), length);
        if (!input && !bytes.empty()) throw std::runtime_error("fixture cannot be read");
        return bytes;
    }

    void Require(bool valid, const char* message)
    {
        if (!valid) throw std::runtime_error(message);
    }

    std::string Ascii(std::wstring_view text)
    {
        std::string result;
        result.reserve(text.size());
        for (const wchar_t value : text)
        {
            if (value > 0x7f) throw std::runtime_error("fixture identity is not ASCII");
            result.push_back(static_cast<char>(value));
        }
        return result;
    }

    void CheckSource(const ck::CookedAssetCatalog& catalog,
        const experiment::AssetId& id,
        std::shared_ptr<const ck::ArtifactByteSource> bytes,
        std::span<const std::byte> expected)
    {
        ck::CookedAudioClipSource clip;
        std::string failure;
        Require(catalog.OpenAudioClip(id, std::move(bytes), clip, failure),
            failure.empty() ? "audio source open failed" : failure.c_str());
        Require(clip.PayloadSize() == expected.size(), "payload size differs");
        std::vector<std::byte> actual(expected.size());
        Require(clip.ReadPayload(0u, actual, failure), "payload read failed");
        Require(std::ranges::equal(actual, expected), "payload differs from source");
        std::array<std::byte, 1> outside{};
        Require(!clip.ReadPayload(clip.PayloadSize(), outside, failure),
            "out-of-range payload read succeeded");
        Require(clip.ReadPayload(clip.PayloadSize(), std::span<std::byte>{}, failure),
            "empty end read failed");
    }

    void CheckPlayback(const ck::CookedAssetCatalog& catalog,
        const experiment::AssetId& id,
        std::shared_ptr<const ck::ArtifactByteSource> bytes)
    {
        ck::CookedAudioClipSource source;
        std::string failure;
        Require(catalog.OpenAudioClip(id, std::move(bytes), source, failure),
            "playback source could not be opened");
        const wave::ClipKey key = wave::ClipKey::FromGuid(id.value);
        Require(key.IsGuid() && key != wave::ClipKey(key.Text()),
            "cooked GUID aliases a legacy filename key");
        wave::PlayRequest request{};
        request.clip = key;

        wave::NullAudioBackend nullBackend;
        wave::AudioRuntime nullRuntime(nullBackend, 4u);
        Require(nullRuntime.Start({}), "Null audio runtime did not start");
        Require(nullRuntime.LoadCookedClip(source), "Null cooked clip did not load");
        const wave::VoiceHandle nullVoice = nullRuntime.Play(request);
        Require(nullVoice.IsValid(), "Null cooked clip did not play");
        nullRuntime.UnloadClip(key);
        Require(!nullRuntime.IsAlive(nullVoice), "Null cooked voice survived unload");
        nullRuntime.Shutdown();

        wave::MiniaudioBackend deviceBackend;
        wave::AudioRuntime deviceRuntime(deviceBackend, 4u);
        if (deviceRuntime.Start({}))
        {
            Require(deviceRuntime.LoadCookedClip(source),
                deviceBackend.LastError().c_str());
            const wave::VoiceHandle voice = deviceRuntime.Play(request);
            Require(voice.IsValid(), deviceBackend.LastError().c_str());
            deviceRuntime.UnloadClip(key);
            Require(!deviceRuntime.IsAlive(voice),
                "cooked device voice survived unload");
            deviceRuntime.Shutdown();
            std::cout << "AUDIO_COOKED_PLAYBACK_OK device=pass guid="
                << key.Text() << '\n';
        }
        else
        {
            std::cout << "AUDIO_COOKED_DEVICE_SKIPPED no-device\n";
        }
    }
}

int wmain(int argc, wchar_t** argv)
{
    if (argc != 6)
    {
        std::cerr << "usage: audio_cooked_source_probe <cooked-root> <pak> "
            "<source-audio> <guid> <virtual-artifact-path>\n";
        return 2;
    }
    try
    {
        const fs::path cookedRoot = argv[1];
        const fs::path pakPath = argv[2];
        const fs::path source = argv[3];
        const std::wstring guidWide = argv[4];
        const std::wstring virtualWide = argv[5];
        const std::string guid = Ascii(guidWide);
        const std::string virtualPath = Ascii(virtualWide);
        const fs::path artifact = cookedRoot / fs::path(virtualWide);
        const std::vector<std::byte> artifactBytes = ReadAll(artifact);
        const std::vector<std::byte> sourceBytes = ReadAll(source);
        Require(artifactBytes.size() >= ck::kAudioClipHeaderBytes,
            "artifact is too small");

        experiment::AssetId clipId{};
        Require(experiment::TryParseCanonicalAssetId(guid, clipId),
            "fixture GUID is invalid");
        const auto manifestBytes = ReadAll(cookedRoot / "Derived/asset-manifest.cemf");
        std::vector<ck::AssetManifestIssue> issues;
        const ck::CookedAssetCatalog catalog = ck::CookedAssetCatalog::Load(
            manifestBytes, cookedRoot, issues);
        Require(!catalog.IsEmpty() && issues.empty()
            && catalog.CountOfKind(ck::CookedAssetKind::AudioClip) >= 1u,
            "audio catalog could not be loaded");
        const ck::CookedAssetManifestEntry* entry = catalog.Find(clipId);
        Require(entry && entry->artifactPath == virtualPath
            && entry->byteSize == artifactBytes.size()
            && entry->contentSha256 == Hash::Sha256::Compute(
                artifactBytes.data(), artifactBytes.size()),
            "CEMF audio entry differs from artifact");

        CheckSource(catalog, clipId,
            std::make_shared<ck::LooseArtifactByteSource>(cookedRoot),
            sourceBytes);
        const auto packaged = std::make_shared<Pak::Archive>(pakPath);
        CheckSource(catalog, clipId,
            std::make_shared<ck::PakAudioClipByteSource>(packaged),
            sourceBytes);
        CheckPlayback(catalog, clipId,
            std::make_shared<ck::PakAudioClipByteSource>(packaged));

        const fs::path localPak = cookedRoot.parent_path() / "audio-range-encrypted.pak";
        Pak::BuildOptions options{};
        options.chunkSize = 97u;
        options.compress = true;
        options.encrypt = true;
        std::array<Pak::u8, 32> key{};
        for (std::size_t index = 0u; index < key.size(); ++index)
            key[index] = static_cast<Pak::u8>(index + 1u);
        Pak::Builder builder(localPak, options);
        builder.setKey(key);
        const std::string pakVirtualPath = "Assets/" + virtualPath;
        builder.addFile(pakVirtualPath, artifact);
        builder.finish();
        const auto encrypted = std::make_shared<Pak::Archive>(localPak,
            Pak::OpenOptions{ key });
        {
            std::fstream writer(localPak,
                std::ios::binary | std::ios::in | std::ios::out);
            Require(!writer.is_open(),
                "mounted pak allowed a writer to replace verified payload bytes");
        }
        CheckSource(catalog, clipId,
            std::make_shared<ck::PakAudioClipByteSource>(encrypted),
            sourceBytes);
        const auto encryptedAll = encrypted->readAll(pakVirtualPath);
        Require(encryptedAll.size() == artifactBytes.size(),
            "encrypted pak readAll size differs");
        for (std::size_t index = 0u; index < encryptedAll.size(); ++index)
            Require(encryptedAll[index] == std::to_integer<Pak::u8>(artifactBytes[index]),
                "encrypted pak readAll differs");

        std::array<Pak::u8, 30> crossing{};
        encrypted->readRange(pakVirtualPath, 90u, crossing);
        for (std::size_t index = 0u; index < crossing.size(); ++index)
        {
            Require(crossing[index] == std::to_integer<Pak::u8>(
                artifactBytes[90u + index]), "cross-chunk pak range differs");

        std::atomic<bool> concurrentReadsValid{ true };
        std::array<std::thread, 3> readers;
        for (auto& reader : readers)
        {
            reader = std::thread([&]
            {
                try
                {
                    for (unsigned repeat = 0u; repeat < 20u; ++repeat)
                    {
                        std::array<Pak::u8, 30> bytes{};
                        encrypted->readRange(pakVirtualPath, 90u, bytes);
                        for (std::size_t index = 0u; index < bytes.size(); ++index)
                            if (bytes[index] != std::to_integer<Pak::u8>(
                                artifactBytes[90u + index]))
                                concurrentReadsValid.store(false);
                    }
                }
                catch (const std::exception&)
                {
                    concurrentReadsValid.store(false);
                }
            });
        }
        for (auto& reader : readers) reader.join();
        Require(concurrentReadsValid.load(), "mounted pak concurrent ranges differ");
        }
        bool rejectedRange = false;
        try
        {
            std::array<Pak::u8, 1> outside{};
            encrypted->readRange(pakVirtualPath, artifactBytes.size(), outside);
        }
        catch (const Pak::WinError&)
        {
            rejectedRange = true;
        }
        Require(rejectedRange, "pak accepted an out-of-range read");
        Require(!encrypted->contains(pakVirtualPath + "-missing"),
            "pak reported a missing entry");

        const fs::path corruptRoot = cookedRoot.parent_path() / "AudioCookCorrupt";
        const fs::path corruptArtifact = corruptRoot / fs::path(virtualWide);
        fs::create_directories(corruptArtifact.parent_path());
        std::vector<std::byte> corruptBytes = artifactBytes;
        corruptBytes.back() ^= std::byte{ 1 };
        std::ofstream corrupt(corruptArtifact, std::ios::binary | std::ios::trunc);
        corrupt.write(reinterpret_cast<const char*>(corruptBytes.data()),
            static_cast<std::streamsize>(corruptBytes.size()));
        corrupt.close();
        ck::CookedAudioClipSource rejected;
        std::string failure;
        Require(!catalog.OpenAudioClip(clipId,
            std::make_shared<ck::LooseArtifactByteSource>(corruptRoot),
            rejected, failure), "mutated audio artifact was accepted");

        const fs::path changedRoot = cookedRoot.parent_path() / "AudioCookChangedAfterOpen";
        const fs::path changedArtifact = changedRoot / fs::path(virtualWide);
        fs::create_directories(changedArtifact.parent_path());
        {
            std::ofstream original(changedArtifact, std::ios::binary | std::ios::trunc);
            original.write(reinterpret_cast<const char*>(artifactBytes.data()),
                static_cast<std::streamsize>(artifactBytes.size()));
        }
        ck::CookedAudioClipSource changedAfterOpen;
        Require(catalog.OpenAudioClip(clipId,
            std::make_shared<ck::LooseArtifactByteSource>(changedRoot),
            changedAfterOpen, failure), "changed-after-open fixture did not open");
        {
            std::fstream changed(changedArtifact,
                std::ios::binary | std::ios::in | std::ios::out);
            changed.seekp(-1, std::ios::end);
            const char flipped = static_cast<char>(
                std::to_integer<unsigned char>(artifactBytes.back() ^ std::byte{ 1 }));
            changed.write(&flipped, 1);
        }
        wave::MiniaudioBackend changedBackend;
        if (changedBackend.Start({}))
        {
            Require(!changedBackend.LoadCookedClip(wave::ClipKey::FromGuid(clipId.value),
                changedAfterOpen), "changed-after-open payload was accepted");
            changedBackend.Stop();
        }

        const fs::path streamRoot = cookedRoot.parent_path() / "AudioCookStreamMode";
        const fs::path streamArtifact = streamRoot / fs::path(virtualWide);
        fs::create_directories(streamArtifact.parent_path());
        std::vector<std::byte> streamBytes = artifactBytes;
        streamBytes[9] = std::byte{ 2 }; // CEAC Stream mode, payload unchanged.
        {
            std::ofstream stream(streamArtifact, std::ios::binary | std::ios::trunc);
            stream.write(reinterpret_cast<const char*>(streamBytes.data()),
                static_cast<std::streamsize>(streamBytes.size()));
        }
        ck::CookedAssetManifestEntry streamEntry = *entry;
        streamEntry.contentSha256 = Hash::Sha256::Compute(
            streamBytes.data(), streamBytes.size());
        ck::CookedAudioClipSource streamSource;
        Require(ck::OpenCookedAudioClipEntry(streamEntry,
            std::make_shared<ck::LooseArtifactByteSource>(streamRoot),
            streamSource, failure), "Stream mode fixture did not open");
        wave::NullAudioBackend streamNull;
        Require(streamNull.Start({}), "Stream rejection Null backend did not start");
        Require(!streamNull.LoadCookedClip(wave::ClipKey::FromGuid(clipId.value),
            streamSource), "Stream mode silently loaded as resident");
        streamNull.Stop();
        wave::MiniaudioBackend streamDevice;
        if (streamDevice.Start({}))
        {
            Require(!streamDevice.LoadCookedClip(wave::ClipKey::FromGuid(clipId.value),
                streamSource), "device silently loaded Stream mode as resident");
            streamDevice.Stop();
        }
        Require(!catalog.OpenAudioClip(experiment::AssetId{},
            std::make_shared<ck::LooseArtifactByteSource>(cookedRoot),
            rejected, failure), "missing audio GUID was accepted");
        ck::CookedAssetManifestEntry mismatched = *entry;
        Require(experiment::TryParseCanonicalAssetId(
            "ad5532f6-5d94-4683-878b-d09a34345ee4", mismatched.assetId),
            "mismatch fixture GUID is invalid");
        Require(!ck::OpenCookedAudioClipEntry(mismatched,
            std::make_shared<ck::LooseArtifactByteSource>(cookedRoot),
            rejected, failure), "audio GUID/path mismatch was accepted");

        std::cout << "AUDIO_COOKED_SOURCE_OK loose=pass packaged=pass "
            "encryptedCrossChunk=pass bounded=pass corruption=reject\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "audio cooked source probe failed: " << error.what() << '\n';
        return 1;
    }
}
