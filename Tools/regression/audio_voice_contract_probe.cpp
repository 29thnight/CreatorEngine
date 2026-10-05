// Phase 22 exact-production-source regression entry point.
// Deterministic fixtures and pre-cutover contracts retained from 86f7efd.
// Device-free PCM rendering and logical Null backend are reported separately.
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "Audio/AudioHost.h"
#include "Audio/AudioRuntime.h"
#include "Audio/ClipDirectory.h"
#include "Audio/MiniaudioBackend.h"
#include "Audio/NullAudioBackend.h"
#include "Audio/VoiceTable.h"
#include "Audio/PlaybackService.h"
#include <limits>
#include <atomic>
#include "Assets/AudioClipSourceMetadata.h"
#include "audio_fixture_decoder.h"
namespace
{
    namespace file = std::filesystem;

    constexpr int kExitPass = 0;
    constexpr int kExitFail = 1;
    constexpr int kExitNoDevice = 3;

    constexpr std::uint32_t kSampleRate = 48000u;
    constexpr int kMaxChannels = 64;

    int g_failures = 0;
    int g_checks = 0;

    void Report(bool condition, const char* what)
    {
        ++g_checks;
        std::printf("  [%s] %s\n", condition ? " ok " : "FAIL", what);
        if (!condition)
        {
            ++g_failures;
        }
    }

    void WriteLittleEndian(std::vector<std::uint8_t>& out, std::uint32_t value, int byteCount)
    {
        for (int index = 0; index < byteCount; ++index)
        {
            out.push_back(static_cast<std::uint8_t>((value >> (8 * index)) & 0xFFu));
        }
    }

    void AppendTag(std::vector<std::uint8_t>& out, const char* tag)
    {
        for (int index = 0; index < 4; ++index)
        {
            out.push_back(static_cast<std::uint8_t>(tag[index]));
        }
    }

    void WriteBigEndian(std::vector<std::uint8_t>& out, std::uint64_t value, int byteCount)
    {
        for (int index = byteCount - 1; index >= 0; --index)
        {
            out.push_back(static_cast<std::uint8_t>((value >> (8 * index)) & 0xFFu));
        }
    }

    bool WriteBytes(const file::path& target, const std::vector<std::uint8_t>& bytes)
    {
        std::ofstream stream(target, std::ios::binary | std::ios::trunc);
        if (!stream.is_open())
        {
            return false;
        }
        stream.write(reinterpret_cast<const char*>(bytes.data()),
            static_cast<std::streamsize>(bytes.size()));
        return stream.good();
    }

    bool ReadBytes(const file::path& source, std::vector<std::uint8_t>& bytes)
    {
        std::ifstream stream(source, std::ios::binary);
        if (!stream.is_open())
        {
            return false;
        }
        bytes.assign(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
        return stream.good() || stream.eof();
    }

    // MPEG-1 Layer III, 32 kb/s, 44.1 kHz, mono. Each silent frame contains
    // 104 bytes and represents 1152 PCM samples. No audio asset is checked in.
    bool WriteSilentMp3(const file::path& target, const file::path& truncatedTarget)
    {
        constexpr std::size_t kFrameBytes = 104u;
        constexpr std::size_t kFrameCount = 48u;
        std::vector<std::uint8_t> bytes(kFrameBytes * kFrameCount, 0u);
        for (std::size_t frame = 0; frame < kFrameCount; ++frame)
        {
            const std::size_t offset = frame * kFrameBytes;
            bytes[offset] = 0xFFu;
            bytes[offset + 1u] = 0xFBu;
            bytes[offset + 2u] = 0x10u;
            bytes[offset + 3u] = 0xC0u;
        }
        if (!WriteBytes(target, bytes))
        {
            return false;
        }
        bytes.resize(kFrameBytes / 2u); // incomplete first frame
        return WriteBytes(truncatedTarget, bytes);
    }

    std::uint8_t FlacCrc8(const std::vector<std::uint8_t>& bytes)
    {
        std::uint8_t crc = 0u;
        for (const std::uint8_t byte : bytes)
        {
            crc ^= byte;
            for (int bit = 0; bit < 8; ++bit)
            {
                crc = static_cast<std::uint8_t>((crc << 1u) ^
                    ((crc & 0x80u) ? 0x07u : 0u));
            }
        }
        return crc;
    }

    std::uint16_t FlacCrc16(const std::vector<std::uint8_t>& bytes)
    {
        std::uint16_t crc = 0u;
        for (const std::uint8_t byte : bytes)
        {
            crc ^= static_cast<std::uint16_t>(byte) << 8u;
            for (int bit = 0; bit < 8; ++bit)
            {
                crc = static_cast<std::uint16_t>((crc << 1u) ^
                    ((crc & 0x8000u) ? 0x8005u : 0u));
            }
        }
        return crc;
    }

    // RFC 9639: STREAMINFO and fixed 4096-sample mono frames containing
    // constant-zero subframes. Both frame checksums are calculated here.
    bool WriteSilentFlac(const file::path& target, const file::path& truncatedTarget)
    {
        constexpr std::uint64_t kBlockSize = 4096u;
        constexpr std::uint64_t kFrameCount = 12u;
        std::vector<std::uint8_t> bytes;
        AppendTag(bytes, "fLaC");
        WriteBigEndian(bytes, 0x80000022u, 4); // last block, STREAMINFO, 34 bytes
        WriteBigEndian(bytes, kBlockSize, 2);
        WriteBigEndian(bytes, kBlockSize, 2);
        WriteBigEndian(bytes, 0u, 3); // unknown minimum frame size
        WriteBigEndian(bytes, 0u, 3); // unknown maximum frame size
        const std::uint64_t streamInfo = (48000ull << 44u) | (15ull << 36u) |
            (kBlockSize * kFrameCount); // mono, 16-bit, known sample count
        WriteBigEndian(bytes, streamInfo, 8);
        for (int index = 0; index < 16; ++index)
        {
            bytes.push_back(0u); // unknown MD5
        }

        for (std::uint64_t frameNumber = 0; frameNumber < kFrameCount; ++frameNumber)
        {
            std::vector<std::uint8_t> frame{
                0xFFu, 0xF8u, 0xCAu, 0x08u,
                static_cast<std::uint8_t>(frameNumber) };
            frame.push_back(FlacCrc8(frame));
            frame.push_back(0u); // constant subframe, no wasted bits
            WriteBigEndian(frame, 0u, 2); // one signed 16-bit zero sample
            WriteBigEndian(frame, FlacCrc16(frame), 2);
            bytes.insert(bytes.end(), frame.begin(), frame.end());
        }
        if (!WriteBytes(target, bytes))
        {
            return false;
        }
        bytes.resize(42u + 5u); // STREAMINFO and incomplete first frame header
        return WriteBytes(truncatedTarget, bytes);
    }

    // 16-bit PCM 모노 sine. 바이트가 결정적이라 같은 인자면 같은 파일이 나온다.
    //
    // ★ amplitude 기본값은 0 이다 — 즉 **무음**을 쓴다. 이 게이트가 재는 것은 보이스
    //   수명(살아 있다/죽는다)이지 소리 내용이 아니고, 검사가 사람 귀에 삐 소리를
    //   내면 반복 실행이 괴롭다. 파형이 필요한 검사(AU6 의 dry/wet capture 같은 것)가
    //   생기면 그때 진폭을 준다. 진폭이 0이어도 프레임 수와 헤더는 그대로라 디코드·
    //   길이·루프 판정에는 영향이 없다(채널 volume 이 아니라 내용이 0이므로
    //   `vol0virtualvol` 가상화 경로도 타지 않는다).
    template <typename Sample>
    bool WritePcmWav(const file::path& target, std::uint32_t frameCount, Sample sampleAt)
    {
        const std::uint32_t dataBytes = frameCount * 2u;

        std::vector<std::uint8_t> bytes;
        bytes.reserve(static_cast<std::size_t>(dataBytes) + 64u);

        AppendTag(bytes, "RIFF");
        WriteLittleEndian(bytes, 36u + dataBytes, 4);
        AppendTag(bytes, "WAVE");
        AppendTag(bytes, "fmt ");
        WriteLittleEndian(bytes, 16u, 4);                    // PCM 청크 크기
        WriteLittleEndian(bytes, 1u, 2);                     // format = PCM
        WriteLittleEndian(bytes, 1u, 2);                     // 채널 = 모노
        WriteLittleEndian(bytes, kSampleRate, 4);
        WriteLittleEndian(bytes, kSampleRate * 2u, 4);       // byte rate
        WriteLittleEndian(bytes, 2u, 2);                     // block align
        WriteLittleEndian(bytes, 16u, 2);                    // bits per sample
        AppendTag(bytes, "data");
        WriteLittleEndian(bytes, dataBytes, 4);

        for (std::uint32_t frame = 0; frame < frameCount; ++frame)
        {
            const std::int16_t quantized = sampleAt(frame);
            WriteLittleEndian(bytes, static_cast<std::uint32_t>(
                static_cast<std::uint16_t>(quantized)), 2);
        }
        return WriteBytes(target, bytes);
    }

    bool WriteSineWav(const file::path& target, float seconds, float frequency,
        float amplitude = 0.0f)
    {
        const std::uint32_t frameCount =
            static_cast<std::uint32_t>(static_cast<float>(kSampleRate) * seconds);
        const double step = 6.283185307179586 * static_cast<double>(frequency) /
            static_cast<double>(kSampleRate);
        return WritePcmWav(target, frameCount, [step, amplitude](std::uint32_t frame)
        {
            return static_cast<std::int16_t>(std::sin(step * static_cast<double>(frame)) *
                static_cast<double>(amplitude) * 32767.0);
        });
    }

    bool WriteImpulseWav(const file::path& target)
    {
        return WritePcmWav(target, 4800u, [](std::uint32_t frame) -> std::int16_t
        {
            return frame == 0u ? 24576 : 0;
        });
    }

    bool WriteShortLoopWav(const file::path& target)
    {
        // 24 complete 480 Hz cycles in 2400 frames (50 ms): a repeatable seam.
        return WritePcmWav(target, 2400u, [](std::uint32_t frame) -> std::int16_t
        {
            const double phase = 6.283185307179586 * 480.0 * frame / kSampleRate;
            return static_cast<std::int16_t>(std::sin(phase) * 8191.0);
        });
    }

    bool WriteMalformedFixtures(const file::path& formats)
    {
        if (!WriteBytes(formats / "corrupt.wav", std::vector<std::uint8_t>(44u, 0u)))
            return false;
        if (!WriteBytes(formats / "corrupt.mp3", std::vector<std::uint8_t>(416u, 0u)))
            return false;
        if (!WriteBytes(formats / "corrupt.flac",
            std::vector<std::uint8_t>{ 'f', 'L', 'a', 'C', 0x80u, 0u, 0u, 0u }))
            return false;
        std::vector<std::uint8_t> wav;
        if (!ReadBytes(formats / "silence.wav", wav) || wav.size() < 44u)
        {
            return false;
        }
        std::vector<std::uint8_t> truncatedWav = wav;
        truncatedWav.resize(20u); // incomplete fmt chunk
        if (!WriteBytes(formats / "truncated.wav", truncatedWav))
        {
            return false;
        }
        std::vector<std::uint8_t> oversizedWav = wav;
        oversizedWav.resize(44u); // no PCM despite an enormous declared data chunk
        for (std::size_t index : { 4u, 5u, 6u, 7u, 40u, 41u, 42u, 43u })
            oversizedWav[index] = 0xFFu;
        if (!WriteBytes(formats / "oversized.wav", oversizedWav))
        {
            return false;
        }
        const std::vector<std::uint8_t> oversizedMp3{
            'I', 'D', '3', 4u, 0u, 0u, 0x7Fu, 0x7Fu, 0x7Fu, 0x7Fu };
        if (!WriteBytes(formats / "oversized.mp3", oversizedMp3))
        {
            return false;
        }
        const std::vector<std::uint8_t> oversizedFlac{
            'f', 'L', 'a', 'C', 0x80u, 0xFFu, 0xFFu, 0xFFu };
        return WriteBytes(formats / "oversized.flac", oversizedFlac);
    }

    bool WriteShortCompressedLoops(const file::path& formats)
    {
        std::vector<std::uint8_t> mp3;
        if (!ReadBytes(formats / "silent.mp3", mp3) || mp3.size() < 4u * 104u)
            return false;
        mp3.resize(4u * 104u); // 4 MPEG frames, about 104 ms at 44.1 kHz
        if (!WriteBytes(formats / "loop.mp3", mp3))
        {
            return false;
        }
        std::vector<std::uint8_t> flac;
        if (!ReadBytes(formats / "silent.flac", flac) || flac.size() < 53u)
            return false;
        flac.resize(53u); // STREAMINFO (42 bytes) and one 4096-sample frame
        std::vector<std::uint8_t> streamInfo;
        WriteBigEndian(streamInfo, (48000ull << 44u) | (15ull << 36u) | 4096ull, 8);
        std::copy(streamInfo.begin(), streamInfo.end(), flac.begin() + 18);
        return WriteBytes(formats / "loop.flac", flac);
    }

    bool WriteTailTruncatedFixtures(const file::path& formats)
    {
        std::vector<std::uint8_t> wav;
        if (!ReadBytes(formats / "sine.wav", wav) || wav.size() != 44u + 4800u * 2u)
            return false;
        wav.resize(44u + 2400u * 2u); // header still declares 4800 frames
        if (!WriteBytes(formats / "tail.wav", wav))
        {
            return false;
        }
        std::vector<std::uint8_t> mp3;
        if (!ReadBytes(formats / "silent.mp3", mp3) || mp3.size() < 52u)
            return false;
        mp3.resize(mp3.size() - 52u); // final MPEG frame cut in half
        if (!WriteBytes(formats / "tail.mp3", mp3))
        {
            return false;
        }
        std::vector<std::uint8_t> flac;
        if (!ReadBytes(formats / "silent.flac", flac) || flac.size() < 5u)
            return false;
        flac.resize(flac.size() - 5u); // final frame lacks its tail and CRC
        return WriteBytes(formats / "tail.flac", flac);
    }

    struct Fixture
    {
        file::path root;
        file::path assets;
    };

    // ★ 저장소 밖이 아니라 `Build/` 아래에 만든다 — git 이 무시하는 자리다.
    //   실행마다 지우고 다시 만들어 이전 실행의 잔재가 판정에 섞이지 않게 한다.
    // withClips=false 는 **제품의 실제 상태**다 — 저장소 어디에도 `Sounds` 디렉터리가
    // 없다. 그러면 로더의 스캔이 매초 예외를 삼키고 `LoadSounds()` 가 한 번도 돌지
    // 않아 `_isSoundLoaderThreadRunning` 이 초기값 true 에서 내려가지 않는다.
    //
    // ★ fixture 를 넣으면 이 조건이 지워진다. 종료 canary 는 반드시 빈 상태로 재야 한다.
    bool PrepareFixture(const file::path& repositoryRoot, bool withClips, Fixture& out)
    {
        std::error_code error;
        out.root = repositoryRoot / "Build" / "Validation" / "AudioVoiceContract";
        out.assets = out.root / "Assets";

        file::remove_all(out.root, error);
        file::create_directories(out.assets, error);
        if (error)
        {
            return false;
        }
        file::create_directories(out.root / "Runtime" / "Data", error);
        if (error)
        {
            return false;
        }
        if (!withClips)
        {
            return true;
        }
        file::create_directories(out.root / "Formats", error);
        if (error)
        {
            return false;
        }
        if (!WriteSilentMp3(out.root / "Formats" / "silent.mp3",
            out.root / "Formats" / "truncated.mp3"))
        {
            return false;
        }
        if (!WriteSilentFlac(out.root / "Formats" / "silent.flac",
            out.root / "Formats" / "truncated.flac"))
        {
            return false;
        }
        if (!WriteShortCompressedLoops(out.root / "Formats"))
        {
            return false;
        }
        if (!WriteSineWav(out.root / "Formats" / "silence.wav", 0.10f, 1000.0f))
        {
            return false;
        }
        if (!WriteSineWav(out.root / "Formats" / "sine.wav", 0.10f, 1000.0f, 0.5f))
        {
            return false;
        }
        if (!WriteImpulseWav(out.root / "Formats" / "impulse.wav"))
        {
            return false;
        }
        if (!WriteShortLoopWav(out.root / "Formats" / "loop.wav"))
        {
            return false;
        }
        if (!WriteTailTruncatedFixtures(out.root / "Formats"))
        {
            return false;
        }
        if (!WriteMalformedFixtures(out.root / "Formats"))
        {
            return false;
        }
        file::create_directories(out.assets / "Sounds", error);
        if (error)
        {
            return false;
        }
        // `SoundManager::LoadSounds` 는 파일명 stem 을 클립 키로 쓴다.
        if (!WriteSineWav(out.assets / "Sounds" / "probe_tone.wav", 0.60f, 440.0f))
        {
            return false;
        }
        if (!WriteSineWav(out.assets / "Sounds" / "probe_blip.wav", 0.12f, 880.0f))
        {
            return false;
        }
        // ★ 미지원 확장자. `.ogg` 는 목록 밖이라 **적재를 시도조차 하지 않는다** —
        //   옛 배선은 목록에 넣어 두고 실패 로그만 남겨, 저작자가 원인을 알 수 없었다.
        {
            std::ofstream unsupported(out.assets / "Sounds" / "probe_ignored.ogg",
                std::ios::binary | std::ios::trunc);
            if (!unsupported.is_open())
            {
                return false;
            }
            unsupported << "ogg placeholder";
            if (!unsupported.good())
            {
                return false;
            }
        }

        // ★ 동명 충돌. 키가 파일명 stem 이라 폴더가 다르면 하나가 진다(AU2 까지).
        //   이기는 쪽을 따지기 전에 **졌다는 사실이 세어지는지**를 먼저 재야 한다.
        file::create_directories(out.assets / "Sounds" / "BGM", error);
        if (error)
        {
            return false;
        }
        if (!WriteSineWav(out.assets / "Sounds" / "BGM" / "probe_tone.wav", 0.20f, 220.0f))
        {
            return false;
        }
        // ★ 손상 파일. "적재 성공, 재생 실패" 로 뒤늦게 드러나는 것을 막는 canary 다.
        //   확장자만 .wav 이고 내용은 RIFF 가 아니다.
        {
            std::ofstream broken(out.assets / "Sounds" / "probe_broken.wav",
                std::ios::binary | std::ios::trunc);
            if (!broken.is_open())
            {
                return false;
            }
            broken << "이것은 WAV 가 아니다. not a riff header at all.";
            if (!broken.good())
            {
                return false;
            }
        }
        return true;
    }

    bool HasClipKey(const std::vector<wave::ClipKey>& keys, const char* wanted)
    {
        const wave::ClipKey target{ std::string(wanted) };
        return std::find(keys.begin(), keys.end(), target) != keys.end();
    }

    int RunOfflineFixtures(const Fixture& fixture)
    {
        std::printf("[AU0] 생성 fixture PCM 검증\n");
        const file::path formats = fixture.root / "Formats";

        assets::AudioClipSourceMetadata wav{}, mp3{}, flac{}, tail{};
        std::string inspectError;
        const bool wavInspected = assets::InspectAudioClipSource(
            formats / "sine.wav", wav, inspectError);
        const bool mp3Inspected = assets::InspectAudioClipSource(
            formats / "silent.mp3", mp3, inspectError);
        const bool flacInspected = assets::InspectAudioClipSource(
            formats / "silent.flac", flac, inspectError);
        Report(wavInspected && mp3Inspected && flacInspected
            && wav.codec == assets::AudioCodec::Wav
            && mp3.codec == assets::AudioCodec::Mp3
            && flac.codec == assets::AudioCodec::Flac
            && wav.payloadSize > 0u && mp3.payloadSize > 0u && flac.payloadSize > 0u,
            "AU2: 세 지원 포맷의 원본 크기·SHA-256을 생성한다");
        Report(assets::InspectAudioClipSource(formats / "tail.wav", tail, inspectError)
            && tail.payloadSize < wav.payloadSize
            && tail.sourceContentHash != wav.sourceContentHash,
            "AU2: 뒤쪽 절단은 원본 크기·해시를 바꾼다");
        Report(!assets::InspectAudioClipSource(formats / "corrupt.mp3", tail, inspectError)
            && !assets::InspectAudioClipSource(formats / "corrupt.flac", tail, inspectError)
            && !assets::InspectAudioClipSource(formats / "probe.ogg", tail, inspectError),
            "AU2: 잘못된 시그니처와 OGG는 원본 검사에서 거부된다");

        std::vector<std::int16_t> pcm;
        std::uint64_t length = 0u;
        bool decoded = wave::probe::DecodePcmS16Mono48k(formats / "sine.wav", 48u, pcm, length);
        Report(decoded && length == 4800u && pcm[0] == 0 &&
            std::abs(pcm[12] - 16383) <= 1 && std::abs(pcm[36] + 16383) <= 1,
            "sine WAV: 48 kHz·1000 Hz·0.5 amplitude PCM 값과 길이");
        decoded = wave::probe::DecodePcmS16Mono48k(formats / "impulse.wav", 128u, pcm, length);
        Report(decoded && length == 4800u && pcm[0] == 24576 &&
            std::all_of(pcm.begin() + 1, pcm.end(), [](std::int16_t value) { return value == 0; }),
            "impulse WAV: 첫 sample 뒤 무음");
        decoded = wave::probe::DecodePcmS16Mono48k(formats / "loop.wav", 2400u, pcm, length);
        Report(decoded && length == 2400u && pcm[0] == 0 && pcm[25] == 8191 &&
            pcm[75] == -8191 && std::abs((pcm[0] - pcm[2399]) - (pcm[1] - pcm[0])) <= 1,
            "short-loop WAV: 24주기와 양 끝 접합 기울기");
        for (const char* name : { "silence.wav", "silent.mp3", "silent.flac",
            "loop.mp3", "loop.flac" })
        {
            decoded = wave::probe::DecodePcmS16Mono48k(formats / name, 128u, pcm, length);
            const bool silent = decoded && std::all_of(pcm.begin(), pcm.end(),
                [](std::int16_t value) { return value == 0; });
            std::printf("  %s: ", name);
            Report(silent, "첫 128 PCM sample 무음");
        }
        std::printf("실패 %d건\n", g_failures);
        return g_failures == 0 ? kExitPass : kExitFail;
    }

    int RunWaveMalformed(const Fixture& fixture)
    {
        std::printf("[AU0] 손상·절단·과장 헤더 matrix\n");
        wave::MiniaudioBackend backend;
        wave::AudioRuntime runtime(backend, 4);
        if (!runtime.Start(wave::DeviceSettings{}))
        {
            std::printf("장치 없음: %s\n", backend.LastError().c_str());
            return kExitNoDevice;
        }
        for (const char* name : { "corrupt.wav", "truncated.wav", "oversized.wav",
            "corrupt.mp3", "truncated.mp3", "oversized.mp3",
            "corrupt.flac", "truncated.flac", "oversized.flac" })
        {
            std::printf("  MALFORMED_CASE %s\n", name);
            std::fflush(stdout);
            const wave::ClipKey key(name);
            const bool accepted = runtime.LoadClip(key, fixture.root / "Formats" / name);
            Report(!accepted && !HasClipKey(runtime.ListClipKeys(), name),
                "손상 입력은 clip 표에 오르지 않는다");
            Report(backend.LastError().find(name) != std::string::npos,
                "거부 사유에 해당 파일명이 남는다");
        }
        runtime.Shutdown();
        std::printf("실패 %d건\n", g_failures);
        return g_failures == 0 ? kExitPass : kExitFail;
    }

    int RunWaveTailAudit(const Fixture& fixture)
    {
        std::printf("[AU0/AU3] 정상 prefix 뒤쪽 절단 감사\n");
        wave::MiniaudioBackend backend;
        wave::AudioRuntime runtime(backend, 4);
        if (!runtime.Start(wave::DeviceSettings{}))
        {
            std::printf("장치 없음: %s\n", backend.LastError().c_str());
            return kExitNoDevice;
        }
        for (const auto& [name, original] : {
            std::pair{ "tail.wav", "sine.wav" },
            std::pair{ "tail.mp3", "silent.mp3" },
            std::pair{ "tail.flac", "silent.flac" } })
        {
            const file::path source = fixture.root / "Formats" / name;
            std::vector<std::int16_t> originalPcm;
            std::uint64_t originalReported = 0u;
            (void)wave::probe::DecodePcmS16Mono48k(
                fixture.root / "Formats" / original, 70000u, originalPcm, originalReported);
            std::vector<std::int16_t> pcm;
            std::uint64_t reported = 0u;
            (void)wave::probe::DecodePcmS16Mono48k(source, 70000u, pcm, reported);
            const wave::ClipKey key(name);
            const bool accepted = runtime.LoadClip(key, source);
            wave::VoiceHandle voice{};
            if (accepted)
            {
                wave::PlayRequest request{};
                request.clip = key;
                request.volume = 0.0f;
                voice = runtime.Play(request);
            }
            std::printf("TAIL_RESULT file=%s original_frames=%llu reported_frames=%llu decoded_frames=%llu load=%u play=%u error=%s\n",
                name, static_cast<unsigned long long>(originalPcm.size()),
                static_cast<unsigned long long>(reported),
                static_cast<unsigned long long>(pcm.size()),
                accepted ? 1u : 0u, voice.IsValid() ? 1u : 0u,
                backend.LastError().c_str());
            Report(!pcm.empty() && pcm.size() < originalPcm.size(),
                "정상 prefix 뒤쪽 PCM이 실제로 사라졌다");
            runtime.Stop(voice);
            runtime.UnloadClip(key);
        }
        runtime.Shutdown();
        std::printf("실패 %d건\n", g_failures);
        return g_failures == 0 ? kExitPass : kExitFail;
    }
    void RunVoiceTable()
    {
        std::printf("[6] 보이스 표(wave)\n");

        wave::VoiceTable table(3);
        wave::PlayRequest request{};
        request.clip = wave::ClipKey("probe_tone");
        request.ownerId = 7u;

        Report(!table.IsAlive(wave::VoiceHandle{}), "값 초기화 핸들은 무효다");

        const wave::VoiceHandle first = table.Acquire(request, 100u);
        Report(first.IsValid(), "잡으면 유효한 핸들이 나온다");
        Report(table.IsAlive(first), "잡은 보이스는 살아 있다");
        Report(1u == table.AliveCount(), "생존 수가 1이다");

        Report(table.Release(first), "반납이 성공한다");
        Report(!table.IsAlive(first), "반납 뒤 핸들이 거부된다");
        Report(!table.Release(first), "같은 핸들을 두 번 반납하면 실패한다");
        Report(0u == table.AliveCount(), "생존 수가 0이다");

        const wave::VoiceHandle slotA = table.Acquire(request, 101u);
        const wave::VoiceHandle slotB = table.Acquire(request, 102u);
        const wave::VoiceHandle slotC = table.Acquire(request, 103u);
        Report(slotA.IsValid() && slotB.IsValid() && slotC.IsValid(), "용량만큼 잡힌다");
        const wave::VoiceHandle overflow = table.Acquire(request, 104u);
        Report(!overflow.IsValid(), "자리가 없으면 무효 핸들을 준다");
        Report(3u == table.AliveCount(), "생존 수가 용량을 넘지 않는다");

        const wave::VoiceHandle stale = slotA;
        table.Release(slotA);
        const wave::VoiceHandle reused = table.Acquire(request, 105u);
        Report(reused.index == stale.index, "반납된 슬롯이 다시 쓰인다");
        Report(reused.generation != stale.generation, "다시 쓸 때 세대가 바뀐다");
        Report(!table.IsAlive(stale), "옛 핸들이 새 보이스를 가리키지 않는다");
        Report(nullptr == table.Find(stale), "낡은 핸들 조회는 nullptr 이다");

        const wave::VoiceRecord* const record = table.Find(reused);
        Report(nullptr != record && 7u == record->ownerId, "요청의 소유자가 기록된다");

        std::size_t visited = 0;
        table.ForEachAlive([&visited](wave::VoiceHandle, wave::VoiceRecord&) { ++visited; });
        Report(visited == table.AliveCount(), "순회가 생존 수와 일치한다");

        (void)slotB;
        (void)slotC;
    }

    [[nodiscard]] bool NearlyEqual(float left, float right) noexcept
    {
        return std::abs(left - right) < 1e-4f;
    }

    // ── 검사 7: wave + Null 백엔드 ────────────────────────────────────────
    //
    // 장치 없이 도는 결정적 경로다. 여기서 재는 것은 소리가 아니라 **계약**이다 —
    // 시작 전/적재 전 재생 거부, 손잡이 수명, 값 전달, 용량, 소유자별 정지, 종료.
    void RunWaveNullBackend()
    {
        std::printf("[7] wave + Null 백엔드\n");

        wave::NullAudioBackend backend;
        wave::AudioRuntime runtime(backend, 2);

        wave::PlayRequest request{};
        request.clip = wave::ClipKey("probe_tone");
        request.bus = wave::BusId{ 1u };
        request.ownerId = 42u;

        Report(!runtime.Play(request).IsValid(), "시작 전 재생은 무효 핸들이다");
        Report(runtime.Start(wave::DeviceSettings{}), "Null 백엔드가 시작한다");
        const std::size_t attemptsBefore = backend.StartVoiceAttempts();
        Report(!runtime.Play(request).IsValid(), "적재되지 않은 클립은 무효 핸들이다");
        // ★ 위 단정만으로는 런타임이 막았는지 백엔드가 막았는지 알 수 없다 — 실제로
        //   런타임 가드를 변이로 걷었을 때 Null 이 대신 막아 통과했다. 요청이 백엔드에
        //   닿지 않았음을 함께 재야 그 자리가 못 박힌다.
        Report(attemptsBefore == backend.StartVoiceAttempts(),
            "없는 클립은 백엔드에 요청조차 가지 않는다");

        Report(runtime.LoadClip(request.clip, "probe_tone.wav"), "클립이 적재된다");
        Report(1u == runtime.ListClipKeys().size(), "적재된 클립이 목록에 오른다");

        const wave::VoiceHandle voice = runtime.Play(request);
        Report(voice.IsValid(), "적재 뒤 재생이 손잡이를 준다");
        Report(runtime.IsAlive(voice), "손잡이가 살아 있다");
        Report(1u == backend.PlayingCount(), "백엔드가 실제로 하나를 울린다");

        // 값 전달과 이득 합성. 기본 이득 × 감쇠가 백엔드에 닿아야 한다.
        const wave::BackendVoiceId firstBackendVoice{ 1u };
        runtime.SetVoiceParameters(voice, 0.5f, 1.5f, 10);
        Report(NearlyEqual(backend.VoiceVolume(firstBackendVoice), 0.5f),
            "값 변경이 백엔드까지 간다");
        Report(NearlyEqual(backend.VoicePitch(firstBackendVoice), 1.5f),
            "피치가 백엔드까지 간다");
        runtime.SetVoiceGain(voice, 0.5f);
        Report(NearlyEqual(backend.VoiceVolume(firstBackendVoice), 0.25f),
            "감쇠가 기본 이득과 곱해진다");

        const wave::VoiceHandle second = runtime.Play(request);
        const wave::VoiceHandle third = runtime.Play(request);
        Report(second.IsValid(), "용량 안에서는 더 잡힌다");
        Report(!third.IsValid(), "용량을 넘으면 무효 핸들이다");

        runtime.SetPaused(voice, true);
        Report(runtime.IsAlive(voice), "일시정지해도 손잡이는 살아 있다");
        runtime.Update(0.016f);
        Report(runtime.IsAlive(voice), "틱이 일시정지한 보이스를 회수하지 않는다");
        runtime.SetPaused(voice, false);

        runtime.StopByOwner(42u);
        Report(!runtime.IsAlive(voice) && !runtime.IsAlive(second),
            "소유자별 정지가 그 소유자의 것을 전부 끈다");
        Report(0u == backend.PlayingCount(), "백엔드에도 남지 않는다");

        wave::ListenerState listener{};
        listener.position = math::vector3{ 1.0f, 2.0f, 3.0f };
        runtime.SetListener(listener);
        Report(NearlyEqual(backend.Listener().position.x, 1.0f) &&
            NearlyEqual(backend.Listener().position.z, 3.0f), "리스너가 백엔드로 간다");

        runtime.Shutdown();
        Report(!backend.IsRunning(), "종료가 백엔드를 멈춘다");
    }

    class RejectingAudioBackend final : public wave::AudioBackend
    {
    public:
        bool Start(const wave::DeviceSettings& settings) override
        {
            ++starts;
            (void)m_partial.Start(settings);
            return !rejectStart;
        }
        void Stop() override { ++stops; m_partial.Stop(); }
        bool IsRunning() const override { return m_partial.IsRunning(); }
        bool LoadClip(const wave::ClipKey& key, const file::path& source) override
        {
            return m_partial.LoadClip(key, source);
        }
        void UnloadClip(const wave::ClipKey& key) override { m_partial.UnloadClip(key); }
        bool HasClip(const wave::ClipKey& key) const override { return m_partial.HasClip(key); }
        wave::BackendVoiceId StartVoice(const wave::PlayRequest& request) override
        {
            if (voiceStarts++ >= failVoiceAfter)
            {
                return {};
            }
            return m_partial.StartVoice(request);
        }
        void StopVoice(wave::BackendVoiceId voice) override { m_partial.StopVoice(voice); }
        void SetVoicePaused(wave::BackendVoiceId voice, bool paused) override
        {
            m_partial.SetVoicePaused(voice, paused);
        }
        bool IsVoicePlaying(wave::BackendVoiceId voice) const override
        {
            return m_partial.IsVoicePlaying(voice);
        }
        void SetVoiceVolume(wave::BackendVoiceId voice, float gain) override
        {
            m_partial.SetVoiceVolume(voice, gain);
        }
        void SetVoicePitch(wave::BackendVoiceId voice, float pitch) override
        {
            m_partial.SetVoicePitch(voice, pitch);
        }
        void SetVoiceTransform(wave::BackendVoiceId voice,
            const math::vector3& position, const math::vector3& velocity) override
        {
            m_partial.SetVoiceTransform(voice, position, velocity);
        }
        void SetBusVolume(wave::BusId bus, float gain) override
        {
            m_partial.SetBusVolume(bus, gain);
        }
        void SetListener(const wave::ListenerState& listener) override
        {
            m_partial.SetListener(listener);
        }
        void Update() override { m_partial.Update(); }

        int starts{ 0 };
        int stops{ 0 };
        bool rejectStart{ true };
        std::size_t voiceStarts{};
        std::size_t failVoiceAfter{ std::numeric_limits<std::size_t>::max() };
        std::size_t PlayingCount() const noexcept { return m_partial.PlayingCount(); }

    private:
        wave::NullAudioBackend m_partial;
    };

    void RunBackendFailureRetirement()
    {
        wave::PlayRequest request;
        request.clip = wave::ClipKey("failing-loop");
        request.loop = true;
        request.allowVirtualization = true;
        RejectingAudioBackend rejecting;
        rejecting.rejectStart = false;
        rejecting.failVoiceAfter = 0u;
        wave::AudioRuntime initial(rejecting, 4u);
        Report(initial.Start({}) && initial.LoadClip(request.clip, {}),
            "Failure-injection backend starts with known logical clip");
        const auto failed = initial.Play(request);
        Report(!failed.IsValid() && initial.AliveVoiceCount() == 0u && rejecting.PlayingCount() == 0u
            && initial.Metrics().backendFailures == 1u && !initial.LastError().empty(),
            "Backend start failure rejects loop instead of returning immortal virtual handle");
        initial.Shutdown();
        RejectingAudioBackend rehydrate;
        rehydrate.rejectStart = false;
        rehydrate.failVoiceAfter = 1u;
        wave::AudioRuntime resumed(rehydrate, 4u);
        Report(resumed.Start({}) && resumed.LoadClip(request.clip, {}),
            "Rehydrate-failure backend starts");
        const auto voice = resumed.Play(request);
        resumed.SetPhysicalVoiceLimit(0u);
        Report(voice.IsValid() && resumed.GetVoiceState(voice) == wave::VoiceState::Virtual,
            "Loop virtualizes after successful initial backend start");
        resumed.SetPhysicalVoiceLimit(1u);
        resumed.Update(0.1f);
        Report(!resumed.IsAlive(voice) && resumed.AliveVoiceCount() == 0u
            && resumed.Metrics().backendFailures == 1u && !resumed.LastError().empty(),
            "Actual rehydrate failure retires logical voice and records error");
        for (unsigned index = 0u; index < 100u; ++index)
        {
            resumed.Update(0.01f);
        }
        Report(rehydrate.voiceStarts == 2u && rehydrate.PlayingCount() == 0u,
            "Retired failed loop never retries backend start on later ticks");
        resumed.Shutdown();
    }

    void RunWaveHost()
    {
        std::printf("[11] Host 소유 수명과 Null degrade\n");
        auto device = std::make_unique<RejectingAudioBackend>();
        RejectingAudioBackend* const observedDevice = device.get();
        wave::AudioHost host(std::move(device), 2);
        Report(nullptr == host.Service() && wave::AudioHostMode::Stopped == host.Mode(),
            "시작 전에는 서비스가 없다");

        wave::PlayRequest request{};
        request.clip = wave::ClipKey("host_probe");
        request.ownerId = 77u;
        wave::VoiceHandle previous{};
        wave::VoiceHandle firstFallback{};
        bool cyclesPassed = true;
        bool staleRejected = true;
        for (int cycle = 0; cycle < 100; ++cycle)
        {
            cyclesPassed &= host.Start(wave::DeviceSettings{});
            cyclesPassed &= wave::AudioHostMode::Null == host.Mode();
            wave::AudioService* const service = host.Service();
            if (nullptr == service) { cyclesPassed = false; break; }
            staleRejected &= !service->IsAlive(previous);
            cyclesPassed &= service->LoadClip(request.clip, "host_probe.wav");
            const wave::VoiceHandle voice = service->Play(request);
            if (0 == cycle)
            {
                firstFallback = voice;
            }
            cyclesPassed &= voice.IsValid() && service->IsAlive(voice);
            staleRejected &= !service->IsAlive(previous);
            host.Update(0.016f);
            host.Shutdown();
            cyclesPassed &= nullptr == host.Service() &&
                wave::AudioHostMode::Stopped == host.Mode();
            previous = voice;
        }
        Report(cyclesPassed, "장치 실패 뒤 Null 재생·틱·종료 100회가 끝난다");
        Report(staleRejected, "Host 재시작 뒤 이전 보이스 핸들이 거부된다");
        Report(100 == observedDevice->starts && 100 == observedDevice->stops &&
            !observedDevice->IsRunning(), "부분 초기화한 장치를 매번 정리한다");

        observedDevice->rejectStart = false;
        const bool recovered = host.Start(wave::DeviceSettings{});
        wave::AudioService* const recoveredService = host.Service();
        bool recoveryPassed = recovered && wave::AudioHostMode::Device == host.Mode() &&
            nullptr != recoveredService;
        if (recoveredService)
        {
            recoveryPassed &= recoveredService->LoadClip(request.clip, "host_probe.wav");
            const wave::VoiceHandle deviceVoice = recoveredService->Play(request);
            recoveryPassed &= deviceVoice.IsValid() && recoveredService->IsAlive(deviceVoice);
            recoveryPassed &= !recoveredService->IsAlive(firstFallback);
        }
        host.Shutdown();
        recoveryPassed &= 101 == observedDevice->starts && 101 == observedDevice->stops;
        Report(recoveryPassed, "Null 뒤 장치 복구에서도 옛 핸들이 새 보이스를 가리키지 않는다");

        wave::NullAudioBackend reusedNull;
        bool slotsReclaimed = true;
        for (int cycle = 0; cycle < 100; ++cycle)
        {
            slotsReclaimed &= reusedNull.Start(wave::DeviceSettings{});
            slotsReclaimed &= reusedNull.LoadClip(request.clip, "host_probe.wav");
            slotsReclaimed &= 1u == reusedNull.StartVoice(request).value;
            reusedNull.Stop();
        }
        Report(slotsReclaimed, "Null 백엔드의 보이스 저장소가 재시작마다 회수된다");
    }

    // ── 검사 8: wave + miniaudio ──────────────────────────────────────────
    //
    // 실제 장치로 도는 경로다. 이 프로세스는 이미 FMOD 장치 개방을 확인했다.
    // 여기서 miniaudio만 실패하면 건너뛰기가 아니라 이행을 막는 게이트 실패다.
    void RunWaveMiniaudio(const Fixture& fixture)
    {
        std::printf("[8] wave + miniaudio\n");

        wave::MiniaudioBackend backend;
        wave::AudioRuntime runtime(backend, 8);

        if (!runtime.Start(wave::DeviceSettings{}))
        {
            std::printf("  miniaudio 장치를 열지 못했다: %s\n",
                backend.LastError().c_str());
            Report(false, "isolated miniaudio backend starts");
            return;
        }
        Report(backend.IsRunning(), "miniaudio 장치가 열린다");
        Report(backend.ActualSettings().sampleRate > 0u, "열린 장치의 sampleRate 가 보고된다");

        const file::path sounds = fixture.assets / "Sounds";
        const wave::ClipKey tone("probe_tone");
        const wave::ClipKey blip("probe_blip");

        Report(runtime.LoadClip(tone, sounds / "probe_tone.wav"), "생성한 WAV 가 적재된다");
        Report(!runtime.LoadClip(wave::ClipKey("probe_broken"), sounds / "probe_broken.wav"),
            "손상 파일은 적재가 거부된다");
        Report(!backend.LastError().empty(), "거부 사유가 문장으로 남는다");

        const wave::ClipKey mp3("probe_mp3");
        const wave::ClipKey flac("probe_flac");
        Report(runtime.LoadClip(mp3, fixture.root / "Formats" / "silent.mp3"),
            "생성한 MP3가 디코드되어 적재된다");
        Report(runtime.LoadClip(flac, fixture.root / "Formats" / "silent.flac"),
            "생성한 FLAC이 디코드되어 적재된다");
        Report(!runtime.LoadClip(wave::ClipKey("truncated_mp3"),
            fixture.root / "Formats" / "truncated.mp3"), "절단된 MP3는 거부된다");
        Report(!runtime.LoadClip(wave::ClipKey("truncated_flac"),
            fixture.root / "Formats" / "truncated.flac"), "절단된 FLAC은 거부된다");

        wave::PlayRequest request{};
        request.clip = tone;
        request.bus = wave::BusId{ 1u };
        request.ownerId = 7u;

        const wave::VoiceHandle voice = runtime.Play(request);
        Report(voice.IsValid(), "재생이 손잡이를 준다");
        runtime.Update(0.016f);
        Report(runtime.IsAlive(voice), "틱 뒤에도 살아 있다");
        runtime.Stop(voice);
        Report(!runtime.IsAlive(voice), "정지 뒤 손잡이가 죽는다");

        request.clip = mp3;
        const wave::VoiceHandle mp3Voice = runtime.Play(request);
        Report(mp3Voice.IsValid(), "MP3가 재생된다");
        runtime.Stop(mp3Voice);
        request.clip = flac;
        const wave::VoiceHandle flacVoice = runtime.Play(request);
        Report(flacVoice.IsValid(), "FLAC이 재생된다");
        runtime.Stop(flacVoice);
        request.clip = tone;

        // ★ 자연 종료 회수. 옛 배선에는 이 경로가 아예 없어 끝난 소리의 자리가
        //   백엔드 열거로만 드러났다. 짧은 클립을 끝까지 두고 틱이 슬롯을 돌려주는지 본다.
        wave::PlayRequest shortRequest = request;
        shortRequest.clip = blip;
        Report(runtime.LoadClip(blip, sounds / "probe_blip.wav"), "짧은 클립이 적재된다");
        const wave::VoiceHandle shortVoice = runtime.Play(shortRequest);
        Report(shortVoice.IsValid(), "짧은 클립이 재생된다");
        std::this_thread::sleep_for(std::chrono::milliseconds(400));
        runtime.Update(0.016f);
        Report(!runtime.IsAlive(shortVoice), "끝난 보이스를 틱이 회수한다");
        Report(0u == runtime.AliveVoiceCount(), "회수 뒤 생존 보이스가 0이다");

        for (const char* name : { "loop.wav", "loop.mp3", "loop.flac" })
        {
            const wave::ClipKey loopKey(name);
            Report(runtime.LoadClip(loopKey, fixture.root / "Formats" / name),
                "짧은 loop fixture가 적재된다");
            request.clip = loopKey;
            request.loop = true;
            request.volume = 0.0f; // keep the device test quiet
            const wave::VoiceHandle loopVoice = runtime.Play(request);
            std::this_thread::sleep_for(std::chrono::milliseconds(250));
            runtime.Update(0.016f);
            std::printf("  %s: ", name);
            Report(loopVoice.IsValid() && runtime.IsAlive(loopVoice),
                "원본 길이를 넘겨도 looping voice가 살아 있다");
            runtime.Stop(loopVoice);
        }

        runtime.Shutdown();
        Report(!backend.IsRunning(), "종료가 장치를 닫는다");
    }

    // ── 검사 9: 클립 폴더 훑기(wave) ─────────────────────────────────────
    //
    // 옛 배선의 [1] 과 **같은 물음**이다 — 폴더에 넣은 것이 클립 표에 오르는가.
    // 다른 것은 답이 오는 방식이다. 옛 배선은 1초 주기 폴링이라 6초를 기다리며
    // 물어봐야 했고, 여기서는 호출 하나가 결과를 들고 돌아온다.
    //
    // ★ 훑기의 판정은 "몇 개 실렸나" 하나가 아니다. 무엇이 왜 빠졌는지가 같이
    //   나와야 한다 — 옛 배선은 넷을 전부 같은 침묵으로 떨궜다(폴더 부재 · 권한
    //   오류 · 미지원 확장자 · 적재 실패).
    void RunWaveClipDirectory(const Fixture& fixture)
    {
        std::printf("[9] 클립 폴더 훑기(wave)\n");

        wave::MiniaudioBackend backend;
        wave::AudioRuntime runtime(backend, 8);
        if (!runtime.Start(wave::DeviceSettings{}))
        {
            std::printf("  miniaudio 장치를 열지 못했다: %s\n",
                backend.LastError().c_str());
            Report(false, "클립 폴더 검사에 필요한 miniaudio 장치가 열린다");
            return;
        }

        const wave::ClipScanReport report =
            wave::LoadClipsFromDirectory(runtime, fixture.assets / "Sounds");

        const std::vector<wave::ClipKey> keys = runtime.ListClipKeys();
        Report(HasClipKey(keys, "probe_tone"), "probe_tone 이 클립 표에 있다");
        Report(HasClipKey(keys, "probe_blip"), "probe_blip 이 클립 표에 있다");

        Report(2u == report.accepted, "성한 클립 둘만 실린다");
        Report(1u == report.rejected, "손상 파일은 실리지 않는다");
        Report(1u == report.unsupported, "미지원 확장자는 적재를 시도하지 않는다");
        Report(1u == report.collided, "동명 충돌이 세어진다");
        Report(!report.rejectedFiles.empty() && !report.collidedKeys.empty(),
            "빠진 것이 이름으로 남는다");
        Report(!HasClipKey(keys, "probe_broken"), "손상 파일은 표에 오르지 않는다");
        Report(!HasClipKey(keys, "probe_ignored"), "미지원 확장자는 표에 오르지 않는다");

        // ★ 두 번 훑어도 같은 결과여야 한다. 옛 폴링 배선은 파일 수가 바뀔
        //   때마다 전체를 다시 훑으면서 이미 있는 것을 조용히 건너뛰었다 —
        //   건너뛴 것과 새로 실은 것이 구분되지 않았다.
        const wave::ClipScanReport again =
            wave::LoadClipsFromDirectory(runtime, fixture.assets / "Sounds");
        Report(0u == again.accepted, "두 번째 훑기는 아무것도 새로 싣지 않는다");
        Report(3u == again.collided, "이미 있는 것은 충돌로 세어진다");
        Report(keys.size() == runtime.ListClipKeys().size(), "두 번 훑어도 표 크기가 같다");

        // ★ 없는 폴더는 오류가 아니라 빈 결과다.
        const wave::ClipScanReport absent =
            wave::LoadClipsFromDirectory(runtime, fixture.assets / "NoSuchFolder");
        Report(0u == absent.Examined(), "없는 폴더는 빈 결과를 준다");

        runtime.Shutdown();
    }

    // ── 검사 10: wave 종료가 끝난다 ──────────────────────────────────────
    //
    // [5] 와 **같은 물음**이고, 옛 배선에서 붉은 그 자리다. 클립이 하나도 없을 때
    // 종료가 끝나는가.
    //
    // ★ 이 실행은 `SoundManager` 를 **한 번도 만들지 않는다.** 만들면 FMOD 싱글톤의
    //   적재 스레드가 서서, 여기서 재려는 것과 다른 이유로 프로세스가 멈출 수 있다.
    //   그러면 초록도 붉음도 믿을 수 없다.
    int RunWaveShutdown(const Fixture& fixture, bool withClips)
    {
        std::printf("[10] 종료 — wave::AudioRuntime::Shutdown() (클립 %s)\n",
            withClips ? "있음" : "없음");
        std::fflush(stdout);

        wave::MiniaudioBackend backend;
        wave::AudioRuntime runtime(backend, 8);
        if (!runtime.Start(wave::DeviceSettings{}))
        {
            std::printf("오디오 장치를 열지 못했다 — 검사 불가: %s\n",
                backend.LastError().c_str());
            return kExitNoDevice;
        }

        const wave::ClipScanReport report =
            wave::LoadClipsFromDirectory(runtime, fixture.assets / "Sounds");
        std::printf("  적재 %zu · 거부 %zu · 미지원 %zu · 충돌 %zu\n",
            report.accepted, report.rejected, report.unsupported, report.collided);

        // ★ 울리는 채로 끝낸다. "정지한 뒤에만 끝난다" 는 종료가 아니다.
        if (report.accepted > 0u)
        {
            wave::PlayRequest request{};
            request.clip = wave::ClipKey("probe_tone");
            request.ownerId = 1u;
            (void)runtime.Play(request);
        }

        runtime.Shutdown();
        std::printf("  [ ok ] Shutdown() 이 끝났다\n");
        return kExitPass;
    }

#include "audio/phase22_playback_contracts.inl"
#include "audio/phase22_render_contracts.inl"
}

int main(int argc, char** argv)
{
    if (argc != 3)
    {
        std::fprintf(stderr, "usage: audio_voice_contract_probe MODE OWNED_WORK_ROOT\n");
        return 2;
    }
    const std::string mode(argv[1]);
    Fixture fixture{};
    if (!PrepareFixture(argv[2], true, fixture))
    {
        std::fprintf(stderr, "fixture failed\n");
        return 2;
    }
    if (mode == "fixtures")
    {
        std::printf("FIXTURES_OK\n");
        return 0;
    }
    if (mode == "core")
    {
        RunVoiceTable();
        RunWaveNullBackend();
        RunWaveHost();
        RunBackendFailureRetirement();
        RunPlaybackContracts();
        RunGraphContracts();
        RunPolicyContracts();
    }
    else if (mode == "decode")
    {
        RunOfflineFixtures(fixture);
    }
    else if (mode == "render")
    {
        RunRenderContracts(fixture);
        RunBlendContracts(fixture);
        RunSpatialHandedness(fixture);
    }
    else if (mode == "software-device")
    {
        wave::MiniaudioBackend diag;
        if (!diag.Start({}))
        {
            std::printf("DEVICE_UNAVAILABLE %s\n", diag.LastError().c_str());
            return 3;
        }
        const auto info = diag.DeviceDiagnostics();
        std::printf("DEVICE_DIAGNOSTICS backend=%s name=%s physical_device=not-tested\n",
            info.backend.c_str(), info.deviceName.c_str());
        diag.Stop();
        RunWaveMiniaudio(fixture);
        RunWaveClipDirectory(fixture);
        RunWaveMalformed(fixture);
        RunWaveTailAudit(fixture);
        RunSoftwareDeviceRecovery(fixture);
        Report(RunWaveShutdown(fixture, true) == 0, "active shutdown completes");
    }
    else
    {
        return 2;
    }
    std::printf("SUMMARY checks=%d passed=%d failed=%d\n", g_checks, g_checks - g_failures, g_failures);
    return g_failures ? 1 : 0;
}
