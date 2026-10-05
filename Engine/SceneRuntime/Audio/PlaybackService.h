#pragma once

#include "AudioService.h"
#include "SoundGraph.h"

#include <deque>
#include <mutex>
#include <optional>

namespace wave
{
    struct PlaybackHandle final
    {
        std::uint32_t index{};
        std::uint32_t generation{};
        [[nodiscard]] constexpr bool IsValid() const noexcept { return generation != 0u; }
        [[nodiscard]] constexpr std::uint64_t Value() const noexcept
        {
            return (static_cast<std::uint64_t>(generation) << 32u) | index;
        }
        [[nodiscard]] static constexpr PlaybackHandle FromValue(std::uint64_t value) noexcept
        {
            return { static_cast<std::uint32_t>(value), static_cast<std::uint32_t>(value >> 32u) };
        }
        bool operator==(const PlaybackHandle&) const = default;
    };

    struct PlaybackScope final
    {
        std::uint32_t index{};
        std::uint32_t generation{};
        [[nodiscard]] constexpr bool IsValid() const noexcept { return generation != 0u; }
        bool operator==(const PlaybackScope&) const = default;
    };

    enum class ScopeKind : std::uint8_t { World, Session, EditorPreview };
    enum class OwnerDestroyedPolicy : std::uint8_t { Stop, DetachAndFinish };
    enum class PlaybackState : std::uint8_t { Stopped, Playing, Paused };
    enum class PlaybackEndReason : std::uint8_t { Finished, Stopped, ScopeEnded, OwnerDestroyed, Replaced };

    struct PlaybackRequest final
    {
        SoundSource source;
        PlayRequest settings;
        ParameterMap parameters;
        OwnerDestroyedPolicy ownerPolicy{ OwnerDestroyedPolicy::Stop };
        std::uint64_t randomSeed{};
        // Presets own the defaults. Set true for a component with explicitly
        // authored settings; false applies the preset's defaults in full.
        bool overridePresetSettings{ false };
        std::optional<bool> spatialOverride;
        std::optional<bool> loopOverride;
    };

    struct PlaybackCompletion final
    {
        PlaybackHandle playback;
        PlaybackScope scope;
        std::uint64_t ownerId{};
        PlaybackEndReason reason{ PlaybackEndReason::Finished };
    };

    enum class PlaybackCommandKind : std::uint8_t { Play, Stop, Pause, Resume, SetGainPitch, SetParameter };
    struct PlaybackCommand final
    {
        PlaybackCommandKind kind{ PlaybackCommandKind::Stop };
        PlaybackHandle playback;
        float gain{ 1.0f };
        float pitch{ 1.0f };
        std::string parameter;
        ParameterValue value{ 0.0f };
        PlaybackScope scope;
        PlaybackRequest request;
        std::uint64_t ticket{};
    };

    struct PlaybackStarted final
    {
        std::uint64_t ticket{};
        PlaybackHandle playback;
        std::string error;
    };

    // All methods except Enqueue run on the host/game thread. This service
    // never runs inside the device callback and never retains Entity/CLR data.
    class PlaybackService final
    {
    public:
        explicit PlaybackService(AudioService& audio, std::size_t capacity = 256u);
        ~PlaybackService();
        PlaybackService(const PlaybackService&) = delete;
        PlaybackService& operator=(const PlaybackService&) = delete;

        [[nodiscard]] PlaybackScope CreateScope(ScopeKind kind);
        void EndScope(PlaybackScope scope);
        [[nodiscard]] bool IsScopeAlive(PlaybackScope scope) const noexcept;
        [[nodiscard]] PlaybackHandle Play(PlaybackScope scope, const PlaybackRequest& request);
        [[nodiscard]] PlaybackHandle Play2D(PlaybackScope scope, PlaybackRequest request);
        [[nodiscard]] PlaybackHandle PlayAt(PlaybackScope scope, PlaybackRequest request,
            const math::vector3& position);
        [[nodiscard]] PlaybackHandle PlayAttached(PlaybackScope scope, PlaybackRequest request,
            std::uint64_t ownerId, const math::vector3& position);
        void Stop(PlaybackHandle playback);
        void SetPaused(PlaybackHandle playback, bool paused);
        void SetGainPitch(PlaybackHandle playback, float gain, float pitch);
        void SetSettings(PlaybackHandle playback, const PlayRequest& settings);
        void SetOwnerPolicy(PlaybackHandle playback, OwnerDestroyedPolicy policy);
        [[nodiscard]] bool SetParameter(PlaybackHandle playback, const std::string& name, ParameterValue value);
        void SetTransform(PlaybackHandle playback, const math::vector3& position, const math::vector3& velocity);
        void OwnerDestroyed(PlaybackScope scope, std::uint64_t ownerId);
        [[nodiscard]] PlaybackState State(PlaybackHandle playback) const noexcept;
        [[nodiscard]] bool IsAlive(PlaybackHandle playback) const noexcept;
        [[nodiscard]] std::size_t AliveCount() const noexcept;
        [[nodiscard]] std::size_t ChildVoiceCount(PlaybackHandle playback) const noexcept;
        [[nodiscard]] bool RegisterGraph(const ClipKey& id, std::shared_ptr<const SoundGraphProgram> graph);
        [[nodiscard]] bool RegisterPreset(const ClipKey& id, const SoundPreset& preset);
        void UnregisterAsset(const ClipKey& id);
        [[nodiscard]] bool HasSource(const SoundSource& source) const;
        [[nodiscard]] bool Enqueue(PlaybackCommand command);
        void Update();
        void Shutdown();
        [[nodiscard]] std::vector<PlaybackCompletion> TakeCompletions();
        [[nodiscard]] bool TryTakeCompletion(PlaybackCompletion& completion);
        [[nodiscard]] std::vector<PlaybackStarted> TakeStarted();
        [[nodiscard]] std::uint64_t DroppedCompletions() const noexcept { return m_droppedCompletions; }
        [[nodiscard]] const std::string& LastError() const noexcept { return m_lastError; }
        [[nodiscard]] AudioService& Audio() noexcept { return m_audio; }

    private:
        struct ScopeRecord final
        {
            ScopeKind kind{};
            std::uint32_t generation{ 1u };
            bool alive{};
        };
        struct Child final
        {
            VoiceHandle voice;
            GraphVoice graph;
        };
        struct Instance final
        {
            PlaybackScope scope;
            PlaybackRequest request;
            std::shared_ptr<const SoundGraphProgram> graph;
            ParameterMap parameters;
            std::vector<Child> children;
            std::uint64_t randomState{};
            float gain{ 1.0f };
            float pitch{ 1.0f };
            std::uint32_t generation{ 1u };
            PlaybackState state{ PlaybackState::Stopped };
        };
        [[nodiscard]] Instance* Find(PlaybackHandle playback) noexcept;
        [[nodiscard]] const Instance* Find(PlaybackHandle playback) const noexcept;
        void Finish(PlaybackHandle playback, PlaybackEndReason reason);
        bool ApplySettings(Instance& instance);
        AudioService& m_audio;
        std::vector<ScopeRecord> m_scopes;
        std::vector<Instance> m_instances;
        std::unordered_map<ClipKey, std::shared_ptr<const SoundGraphProgram>> m_graphs;
        std::unordered_map<ClipKey, SoundPreset> m_presets;
        std::vector<PlaybackCompletion> m_completions;
        std::vector<PlaybackStarted> m_started;
        std::uint64_t m_droppedCompletions{};
        std::string m_lastError;
        // Only the bounded worker-to-game command mailbox is synchronized.
        // No device callback or ordinary owner-thread playback takes this lock.
        std::mutex m_commandMutex;
        std::deque<PlaybackCommand> m_commands;
        std::size_t m_pendingResults{};
        bool m_acceptCommands{ true };
        bool m_stopped{ false };
        static constexpr std::size_t kCommandCapacity = 1024u;
    };
}
