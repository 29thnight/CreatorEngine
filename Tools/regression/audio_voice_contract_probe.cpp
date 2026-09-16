// 오디오 보이스 계약 probe — 에디터 없이 도는 단독 실행 파일
//
// ★ 왜 만들었나. PHASE 22 사전 정찰(docs/analysis/Phase22AudioPreflight.md)에서
//   오디오 회귀 게이트가 **0개**임을 확인했다. 배선을 다시 긋기 전에 "무엇이
//   지금 성립하는가" 를 재는 자가 없으면, 재배선 뒤에도 소리가 안 나는 상태가
//   그대로 초록으로 통과한다.
//
// ★★ 음원은 저장소에 커밋하지 않는다. 이 probe 가 매 실행마다 sine PCM WAV 를
//   **생성**해 `Build/`(git 무시) 아래 임시 자산 루트에 쓴다. 바이트는 결정적이다.
//
// ★★★ 이 probe 는 지금 `SoundManager` 의 표면을 직접 쓴다. 배선이 `wave::`
//   계약으로 바뀌면 **단정 문장은 그대로 두고 호출만 갈아끼운다** — 단정이
//   "클립이 적재된다 · 재생하면 살고 정지하면 죽는다 · 없는 키는 무효 · 종료가
//   끝난다" 라서 백엔드나 API 이름에 매여 있지 않다.
//
// 빌드·실행은 `Tools/regression/verify-audio-voice-contract.ps1` 이 한다.
// **run-all.ps1 에 배선하지 않는다** — 실장치를 요구하는 로컬 검증 전용이다.
//
// 종료 코드: 0 통과 · 1 실패 · 3 오디오 장치 없음(검사 불가, 초록이 아니다)

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

#include "Audio/AudioRuntime.h"
#include "Audio/MiniaudioBackend.h"
#include "Audio/NullAudioBackend.h"
#include "Audio/VoiceTable.h"
#include "PathFinder.h"
#include "SoundManager.h"

namespace
{
    namespace file = std::filesystem;

    constexpr int kExitPass = 0;
    constexpr int kExitFail = 1;
    constexpr int kExitNoDevice = 3;

    constexpr std::uint32_t kSampleRate = 48000u;
    constexpr int kMaxChannels = 64;

    int g_failures = 0;

    void Report(bool condition, const char* what)
    {
        std::printf("  [%s] %s\n", condition ? " ok " : "FAIL", what);
        if (!condition) ++g_failures;
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

    // 16-bit PCM 모노 sine. 바이트가 결정적이라 같은 인자면 같은 파일이 나온다.
    //
    // ★ amplitude 기본값은 0 이다 — 즉 **무음**을 쓴다. 이 게이트가 재는 것은 보이스
    //   수명(살아 있다/죽는다)이지 소리 내용이 아니고, 검사가 사람 귀에 삐 소리를
    //   내면 반복 실행이 괴롭다. 파형이 필요한 검사(AU6 의 dry/wet capture 같은 것)가
    //   생기면 그때 진폭을 준다. 진폭이 0이어도 프레임 수와 헤더는 그대로라 디코드·
    //   길이·루프 판정에는 영향이 없다(채널 volume 이 아니라 내용이 0이므로
    //   `vol0virtualvol` 가상화 경로도 타지 않는다).
    bool WriteSineWav(const file::path& target, float seconds, float frequency,
        float amplitude = 0.0f)
    {
        const std::uint32_t frameCount =
            static_cast<std::uint32_t>(static_cast<float>(kSampleRate) * seconds);
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

        const double step = 6.283185307179586 * static_cast<double>(frequency) /
            static_cast<double>(kSampleRate);
        for (std::uint32_t frame = 0; frame < frameCount; ++frame)
        {
            const double sample = std::sin(step * static_cast<double>(frame)) *
                static_cast<double>(amplitude);
            const std::int16_t quantized = static_cast<std::int16_t>(sample * 32767.0);
            WriteLittleEndian(bytes, static_cast<std::uint32_t>(
                static_cast<std::uint16_t>(quantized)), 2);
        }

        std::ofstream stream(target, std::ios::binary | std::ios::trunc);
        if (!stream.is_open()) return false;
        stream.write(reinterpret_cast<const char*>(bytes.data()),
            static_cast<std::streamsize>(bytes.size()));
        return stream.good();
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
        if (error) return false;
        file::create_directories(out.root / "Runtime" / "Data", error);
        if (error) return false;
        if (!withClips) return true;

        file::create_directories(out.assets / "Sounds", error);
        if (error) return false;

        // `SoundManager::LoadSounds` 는 파일명 stem 을 클립 키로 쓴다.
        if (!WriteSineWav(out.assets / "Sounds" / "probe_tone.wav", 0.60f, 440.0f)) return false;
        if (!WriteSineWav(out.assets / "Sounds" / "probe_blip.wav", 0.12f, 880.0f)) return false;

        // ★ 손상 파일. "적재 성공, 재생 실패" 로 뒤늦게 드러나는 것을 막는 canary 다.
        //   확장자만 .wav 이고 내용은 RIFF 가 아니다.
        {
            std::ofstream broken(out.assets / "Sounds" / "probe_broken.wav",
                std::ios::binary | std::ios::trunc);
            if (!broken.is_open()) return false;
            broken << "이것은 WAV 가 아니다. not a riff header at all.";
            if (!broken.good()) return false;
        }
        return true;
    }

    bool InitializePaths(const Fixture& fixture)
    {
        EnginePaths paths{};
        paths.executableRoot = fixture.root;
        paths.projectRoot = fixture.root;
        paths.runtimeContentRoot = fixture.root / "Runtime";
        paths.runtimeDataRoot = fixture.root / "Runtime" / "Data";
        paths.assetsRoot = fixture.assets;
        paths.enableAssetAuthoring = false;
        return PathFinder::Initialize(paths);
    }

    void PumpAudio(int milliseconds)
    {
        const int stepMilliseconds = 10;
        for (int elapsed = 0; elapsed < milliseconds; elapsed += stepMilliseconds)
        {
            Sound->update();
            std::this_thread::sleep_for(std::chrono::milliseconds(stepMilliseconds));
        }
    }

    bool HasClip(const std::vector<std::string>& keys, const char* wanted)
    {
        return std::find(keys.begin(), keys.end(), std::string(wanted)) != keys.end();
    }

    bool ChannelPairIsPlaying(const ChannelPair& pair)
    {
        bool playing = false;
        if (nullptr != pair.ch2D && FMOD_OK == pair.ch2D->isPlaying(&playing) && playing) return true;
        playing = false;
        if (nullptr != pair.ch3D && FMOD_OK == pair.ch3D->isPlaying(&playing) && playing) return true;
        return false;
    }

    // ── 검사 1: 폴더에 넣은 클립이 적재된다 ───────────────────────────────
    //
    // 지금 구현은 1초 주기 폴링이라 즉시 보이지 않는다. 기다리는 한계를 넉넉히
    // 두되, 무한정 기다리지 않는다 — 안 오면 "적재되지 않는다" 가 판정이다.
    bool RunClipDiscovery()
    {
        std::printf("[1] 클립 적재\n");
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(6);
        std::vector<std::string> keys;
        while (std::chrono::steady_clock::now() < deadline)
        {
            keys = Sound->getAllClipKeys();
            if (HasClip(keys, "probe_tone") && HasClip(keys, "probe_blip")) break;
            PumpAudio(100);
        }

        Report(HasClip(keys, "probe_tone"), "probe_tone 이 클립 표에 있다");
        Report(HasClip(keys, "probe_blip"), "probe_blip 이 클립 표에 있다");
        return HasClip(keys, "probe_tone");
    }

    // ── 검사 2: 재생하면 살고, 정지하면 죽는다 ────────────────────────────
    void RunVoiceLifetime()
    {
        std::printf("[2] 보이스 수명\n");
        int ownerTag = 0;

        ChannelPair voice = Sound->playOneShotPooled("probe_tone", ChannelType::SFX,
            1.0f, 1.0f, 128, 0.0f, nullptr, nullptr, &ownerTag);
        Report(nullptr != voice.ch2D || nullptr != voice.ch3D, "재생이 채널을 돌려준다");

        PumpAudio(60);
        Report(ChannelPairIsPlaying(voice), "재생 직후 보이스가 살아 있다");

        Sound->stopByOwnerTag(&ownerTag);
        PumpAudio(60);
        Report(!ChannelPairIsPlaying(voice), "정지 뒤 보이스가 죽는다");
    }

    // ── 검사 3: 없는 키는 무효 핸들 ───────────────────────────────────────
    void RunMissingClip()
    {
        std::printf("[3] 없는 클립\n");
        int ownerTag = 0;
        ChannelPair voice = Sound->playOneShotPooled("probe_absent", ChannelType::SFX,
            1.0f, 1.0f, 128, 0.0f, nullptr, nullptr, &ownerTag);
        Report(nullptr == voice.ch2D && nullptr == voice.ch3D,
            "없는 클립은 채널을 만들지 않는다");
    }

    // ── 검사 4: 리스너를 세울 수 있다 ─────────────────────────────────────
    //
    // 정찰에서 `setListenerAttributes` 호출부가 0이라 3D 감쇠가 항상 원점 기준임을
    // 확인했다. 계약으로는 "세운 값이 되읽힌다" 가 성립해야 한다.
    void RunListenerRoundTrip()
    {
        std::printf("[4] 리스너 왕복\n");
        const FMOD_VECTOR position{ 3.0f, -2.0f, 7.0f };
        const FMOD_VECTOR velocity{ 0.0f, 0.0f, 0.0f };
        const FMOD_VECTOR forward{ 0.0f, 0.0f, 1.0f };
        const FMOD_VECTOR up{ 0.0f, 1.0f, 0.0f };
        Sound->setListenerAttributes(position, velocity, forward, up);
        PumpAudio(30);

        FMOD_VECTOR readBack{};
        const bool read = Sound->getListenerPosition(readBack);
        Report(read, "리스너 위치를 읽을 수 있다");
        const bool same = read &&
            std::abs(readBack.x - position.x) < 1e-3f &&
            std::abs(readBack.y - position.y) < 1e-3f &&
            std::abs(readBack.z - position.z) < 1e-3f;
        Report(same, "세운 리스너 위치가 그대로 되읽힌다");
    }

    // ── 검사 6: 보이스 표(wave) ───────────────────────────────────────────
    //
    // 새 배선의 핵심은 "엔진이 자기 보이스 표를 든다" 는 것이다. 표가 지켜야 하는
    // 규약은 넷이다 — 잡으면 살고, 반납하면 죽고, 슬롯을 다시 쓸 때 세대가 바뀌어
    // 옛 핸들이 새 보이스를 가리키지 못하고, 자리가 없으면 무효 핸들을 준다.
    //
    // ★ 이 검사는 장치도 클립도 쓰지 않는다. 순수 값 계약이다.
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

    // ── 검사 8: wave + miniaudio ──────────────────────────────────────────
    //
    // 실제 장치로 도는 경로다. 장치가 없으면 이 검사만 건너뛴다(그 자체가 degrade
    // 계약이다 — Null 로 내려가면 상위는 분기하지 않는다).
    void RunWaveMiniaudio(const Fixture& fixture)
    {
        std::printf("[8] wave + miniaudio\n");

        wave::MiniaudioBackend backend;
        wave::AudioRuntime runtime(backend, 8);

        if (!runtime.Start(wave::DeviceSettings{}))
        {
            std::printf("  (장치를 열지 못했다: %s — 이 검사만 건너뛴다)\n",
                backend.LastError().c_str());
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

        runtime.Shutdown();
        Report(!backend.IsRunning(), "종료가 장치를 닫는다");
    }
}

int main(int argc, char** argv)
{
    const std::string mode = (argc > 1) ? std::string(argv[1]) : std::string("run");
    const file::path repositoryRoot = (argc > 2)
        ? file::path(argv[2])
        : file::current_path();

    // 종료 canary 는 클립 없는 자산 루트로 돈다 — 제품의 실제 상태다.
    const bool withClips = (mode != "shutdown-empty");

    Fixture fixture{};
    if (!PrepareFixture(repositoryRoot, withClips, fixture))
    {
        std::printf("fixture 를 만들지 못했다: %s\n", fixture.assets.string().c_str());
        return kExitFail;
    }
    if (!InitializePaths(fixture))
    {
        std::printf("PathFinder 초기화 실패\n");
        return kExitFail;
    }

    Sound->initialize(kMaxChannels);

    // ★ 장치가 없으면 FMOD system 이 초기화되지 않아 모든 호출이 실패한다.
    //   그 상태를 "검사 실패" 로 내면 거짓 붉음이고, "통과" 로 내면 거짓 초록이다.
    //   별도 종료 코드로 구분한다.
    FMOD_VECTOR probeListener{};
    if (!Sound->getListenerPosition(probeListener))
    {
        std::printf("오디오 장치를 열지 못했다 — 검사 불가\n");
        return kExitNoDevice;
    }

    if (mode == "shutdown" || mode == "shutdown-empty")
    {
        // ── 검사 5: 종료가 끝난다 (현재 붉음) ──────────────────────────────
        //
        // `SoundManager::Destroy()` 는 제품에서 한 번도 호출되지 않는다. 소멸자는
        // `_isSoundLoaderThreadRunning` 이 내려가기를 기다리는데, 그 값은 초기값
        // true 이고 내리는 유일한 경로가 `LoadSounds()` 의 끝이다. 클립이 한 번도
        // 적재되지 않으면 영원히 돈다.
        //
        // 이 실행은 **끝나지 않을 수 있다**. 제한 시간은 부모 스크립트가 건다.
        std::printf("[5] 종료 — SoundManager::Destroy() 호출 (클립 %s)\n",
            withClips ? "있음" : "없음");
        std::fflush(stdout);
        SoundManager::Destroy();
        std::printf("  [ ok ] Destroy() 가 끝났다\n");
        return kExitPass;
    }

    const bool discovered = RunClipDiscovery();
    if (discovered)
    {
        RunVoiceLifetime();
        RunMissingClip();
    }
    else
    {
        std::printf("  (클립이 적재되지 않아 [2][3] 을 건너뛴다 — 건너뜀은 통과가 아니다)\n");
        ++g_failures;
    }
    RunListenerRoundTrip();
    RunVoiceTable();
    RunWaveNullBackend();
    RunWaveMiniaudio(fixture);

    std::printf("실패 %d건\n", g_failures);
    // ★ 여기서 정상 반환한다. 싱글톤은 누수되므로 소멸자가 돌지 않는다 —
    //   종료 경로는 위 "shutdown" 모드가 따로 잰다.
    return (0 == g_failures) ? kExitPass : kExitFail;
}
