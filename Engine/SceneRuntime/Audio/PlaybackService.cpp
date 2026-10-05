#include "PlaybackService.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace wave
{
    namespace
    {
        void AdvanceGeneration(std::uint32_t& generation)
        {
            // Exhausted slots are retired. Wrapping must never resurrect an
            // ancient handle, even after a very long editor session.
            generation = generation == std::numeric_limits<std::uint32_t>::max() ? 0u : generation + 1u;
        }

        bool ValidSettings(const PlayRequest& settings)
        {
            return std::isfinite(settings.volume) && settings.volume >= 0.0f
                && std::isfinite(settings.pitch) && settings.pitch > 0.0f;
        }
    }

    PlaybackService::PlaybackService(AudioService& audio, std::size_t capacity)
        : m_audio(audio), m_instances(std::min<std::size_t>(capacity, 65536u))
    {
        m_completions.reserve(m_instances.size());
    }

    PlaybackService::~PlaybackService()
    {
        Shutdown();
    }

    PlaybackScope PlaybackService::CreateScope(ScopeKind kind)
    {
        if (m_stopped)
        {
            m_lastError = "Audio playback service has shut down";
            return {};
        }
        for (std::size_t index = 0u; index < m_scopes.size(); ++index)
        {
            auto& scope = m_scopes[index];
            if (!scope.alive && scope.generation != 0u)
            {
                scope.kind = kind;
                scope.alive = true;
                return { static_cast<std::uint32_t>(index), scope.generation };
            }
        }
        if (m_scopes.size() >= 4096u)
        {
            m_lastError = "Audio playback scope capacity exhausted";
            return {};
        }
        m_scopes.push_back({ kind, 1u, true });
        return { static_cast<std::uint32_t>(m_scopes.size() - 1u), 1u };
    }

    bool PlaybackService::IsScopeAlive(PlaybackScope scope) const noexcept
    {
        return scope.IsValid() && scope.index < m_scopes.size() && m_scopes[scope.index].alive
            && m_scopes[scope.index].generation == scope.generation;
    }

    void PlaybackService::EndScope(PlaybackScope scope)
    {
        if (!IsScopeAlive(scope))
        {
            return;
        }
        for (std::size_t index = 0u; index < m_instances.size(); ++index)
        {
            auto& instance = m_instances[index];
            if (instance.state != PlaybackState::Stopped && instance.scope == scope)
            {
                Finish({ static_cast<std::uint32_t>(index), instance.generation }, PlaybackEndReason::ScopeEnded);
            }
        }
        auto& record = m_scopes[scope.index];
        record.alive = false;
        AdvanceGeneration(record.generation);
    }

    PlaybackHandle PlaybackService::Play(PlaybackScope scope, const PlaybackRequest& request)
    {
        m_lastError.clear();
        if (!IsScopeAlive(scope))
        {
            m_lastError = "Audio playback scope is stale or has ended";
            return {};
        }
        std::size_t index = 0u;
        while (index < m_instances.size()
            && (m_instances[index].state != PlaybackState::Stopped || m_instances[index].generation == 0u))
        {
            ++index;
        }
        if (index == m_instances.size())
        {
            m_lastError = "Audio playback instance capacity exhausted";
            return {};
        }
        PlaybackRequest resolved = request;
        if (resolved.source.kind == SoundSourceKind::Preset)
        {
            const auto found = m_presets.find(resolved.source.asset);
            if (found == m_presets.end())
            {
                m_lastError = "SoundPreset is not loaded: " + resolved.source.asset.Text();
                return {};
            }
            const auto& preset = found->second;
            resolved.source = preset.source;
            if (!request.overridePresetSettings)
            {
                resolved.settings = preset.defaults;
                // Ownership and requested transform belong to the Play call,
                // not to a shared preset asset.
                resolved.settings.ownerId = request.settings.ownerId;
                resolved.settings.position = request.settings.position;
                resolved.settings.velocity = request.settings.velocity;
            }
            resolved.parameters = preset.parameters;
            for (const auto& [name, value] : request.parameters)
            {
                resolved.parameters[name] = value;
            }
        }
        if (request.loopOverride.has_value())
        {
            resolved.settings.loop = *request.loopOverride;
            resolved.settings.persistent = *request.loopOverride;
        }
        if (request.spatialOverride.has_value())
        {
            resolved.settings.spatialBlend = *request.spatialOverride ? 1.0f : 0.0f;
        }
        if (!ValidSettings(resolved.settings) || !HasSource(resolved.source))
        {
            m_lastError = "Audio source is unavailable or playback settings are invalid: " + resolved.source.asset.Text();
            return {};
        }
        Instance& instance = m_instances[index];
        instance.scope = scope;
        instance.request = resolved;
        instance.randomState = request.randomSeed != 0u ? request.randomSeed
            : ((static_cast<std::uint64_t>(instance.generation) << 32u) ^ (index + 1u));
        instance.gain = 1.0f;
        instance.pitch = 1.0f;
        instance.graph.reset();
        instance.children.clear();
        instance.parameters.clear();
        std::vector<GraphVoice> voices;
        if (resolved.source.kind == SoundSourceKind::Graph)
        {
            instance.graph = m_graphs.at(resolved.source.asset);
            auto random = instance.randomState;
            if (!ResolveGraphParameters(*instance.graph, resolved.parameters, instance.parameters, m_lastError)
                || !EvaluateSoundGraph(*instance.graph, instance.parameters, random, voices, m_lastError))
            {
                instance.graph.reset();
                return {};
            }
        }
        else
        {
            if (!resolved.parameters.empty())
            {
                m_lastError = "A direct AudioClip does not declare typed graph parameters";
                return {};
            }
            voices.push_back({ resolved.source.asset, 1.0f, 1.0f, 1u });
        }
        for (const auto& voice : voices)
        {
            auto settings = resolved.settings;
            settings.clip = voice.clip;
            settings.volume *= voice.gain;
            settings.pitch *= voice.pitch;
            const auto handle = m_audio.Play(settings);
            if (!handle.IsValid())
            {
                for (const auto& child : instance.children)
                {
                    m_audio.Stop(child.voice);
                }
                instance.children.clear();
                instance.graph.reset();
                m_lastError = "Audio graph child could not start; the whole play request was cancelled";
                return {};
            }
            instance.children.push_back({ handle, voice });
        }
        if (std::any_of(instance.children.begin(), instance.children.end(), [this](const Child& child)
            { return !m_audio.IsAlive(child.voice); }))
        {
            for (const auto& child : instance.children)
            {
                m_audio.Stop(child.voice);
            }
            instance.children.clear();
            instance.graph.reset();
            m_lastError = "Audio policy displaced a child inside one graph request; playback cancelled";
            return {};
        }
        instance.state = PlaybackState::Playing;
        return { static_cast<std::uint32_t>(index), instance.generation };
    }

    PlaybackHandle PlaybackService::Play2D(PlaybackScope scope, PlaybackRequest request)
    {
        request.spatialOverride = false;
        return Play(scope, request);
    }

    PlaybackHandle PlaybackService::PlayAt(PlaybackScope scope, PlaybackRequest request, const math::vector3& position)
    {
        request.settings.position = position;
        request.spatialOverride = true;
        return Play(scope, request);
    }

    PlaybackHandle PlaybackService::PlayAttached(PlaybackScope scope, PlaybackRequest request,
        std::uint64_t ownerId, const math::vector3& position)
    {
        request.settings.ownerId = ownerId;
        return PlayAt(scope, std::move(request), position);
    }

    PlaybackService::Instance* PlaybackService::Find(PlaybackHandle playback) noexcept
    {
        return const_cast<Instance*>(static_cast<const PlaybackService*>(this)->Find(playback));
    }

    const PlaybackService::Instance* PlaybackService::Find(PlaybackHandle playback) const noexcept
    {
        if (!playback.IsValid() || playback.index >= m_instances.size())
        {
            return nullptr;
        }
        const auto& instance = m_instances[playback.index];
        return instance.generation == playback.generation && instance.state != PlaybackState::Stopped ? &instance : nullptr;
    }

    void PlaybackService::Finish(PlaybackHandle playback, PlaybackEndReason reason)
    {
        auto* instance = Find(playback);
        if (instance == nullptr)
        {
            return;
        }
        for (const auto& child : instance->children)
        {
            m_audio.Stop(child.voice);
        }
        // Completion storage is bounded even when a host does not consume it.
        // Hosts should drain once per tick before dispatching to managed code.
        if (m_completions.size() >= std::max<std::size_t>(m_instances.size() * 4u, 1u))
        {
            m_completions.erase(m_completions.begin());
            ++m_droppedCompletions;
        }
        m_completions.push_back({ playback, instance->scope, instance->request.settings.ownerId, reason });
        instance->children.clear();
        instance->graph.reset();
        instance->parameters.clear();
        instance->request = {};
        instance->state = PlaybackState::Stopped;
        AdvanceGeneration(instance->generation);
    }

    void PlaybackService::Stop(PlaybackHandle playback)
    {
        Finish(playback, PlaybackEndReason::Stopped);
    }

    void PlaybackService::SetPaused(PlaybackHandle playback, bool paused)
    {
        if (auto* instance = Find(playback))
        {
            instance->state = paused ? PlaybackState::Paused : PlaybackState::Playing;
            for (const auto& child : instance->children)
            {
                m_audio.SetPaused(child.voice, paused);
            }
        }
    }

    bool PlaybackService::ApplySettings(Instance& instance)
    {
        bool applied = true;
        for (const auto& child : instance.children)
        {
            if (!m_audio.IsAlive(child.voice))
            {
                continue;
            }
            auto settings = instance.request.settings;
            settings.clip = child.graph.clip;
            settings.volume *= instance.gain * child.graph.gain;
            settings.pitch *= instance.pitch * child.graph.pitch;
            m_audio.SetVoiceSettings(child.voice, settings);
            if (!m_audio.LastError().empty())
            {
                m_lastError = m_audio.LastError();
                applied = false;
            }
        }
        return applied;
    }

    void PlaybackService::SetGainPitch(PlaybackHandle playback, float gain, float pitch)
    {
        if (!std::isfinite(gain) || gain < 0.0f || !std::isfinite(pitch) || pitch <= 0.0f)
        {
            m_lastError = "Audio gain/pitch must be finite; gain must be nonnegative and pitch positive";
            return;
        }
        if (auto* instance = Find(playback))
        {
            m_lastError.clear();
            const float oldGain = instance->gain;
            const float oldPitch = instance->pitch;
            instance->gain = gain;
            instance->pitch = pitch;
            if (!ApplySettings(*instance))
            {
                const auto failure = m_lastError;
                instance->gain = oldGain;
                instance->pitch = oldPitch;
                (void)ApplySettings(*instance);
                m_lastError = failure;
            }
        }
    }

    void PlaybackService::SetSettings(PlaybackHandle playback, const PlayRequest& settings)
    {
        if (!ValidSettings(settings))
        {
            m_lastError = "Audio settings require finite nonnegative volume and positive pitch";
            return;
        }
        if (auto* instance = Find(playback))
        {
            m_lastError.clear();
            const auto previous = instance->request.settings;
            instance->request.settings = settings;
            if (!ApplySettings(*instance))
            {
                const auto failure = m_lastError;
                instance->request.settings = previous;
                (void)ApplySettings(*instance);
                m_lastError = failure;
            }
        }
    }

    void PlaybackService::SetOwnerPolicy(PlaybackHandle playback, OwnerDestroyedPolicy policy)
    {
        if (auto* instance = Find(playback))
        {
            instance->request.ownerPolicy = policy;
        }
    }

    bool PlaybackService::SetParameter(PlaybackHandle playback, const std::string& name, ParameterValue value)
    {
        auto* instance = Find(playback);
        if (instance == nullptr || !instance->graph)
        {
            return false;
        }
        auto parameters = instance->parameters;
        parameters[name] = std::move(value);
        auto random = instance->randomState;
        std::vector<GraphVoice> selected;
        if (!EvaluateSoundGraph(*instance->graph, parameters, random, selected, m_lastError))
        {
            return false;
        }
        // Stable branch keys keep unchanged clips at their existing playheads.
        // Switch changes stop only removed branches; random choices retain the
        // per-play seed instead of rerolling whenever a gain changes.
        for (const auto& child : instance->children)
        {
            const bool retained = std::any_of(selected.begin(), selected.end(), [&child](const GraphVoice& voice)
            {
                return voice.branchKey == child.graph.branchKey && voice.clip == child.graph.clip;
            });
            if (!retained)
            {
                m_audio.Stop(child.voice);
            }
        }
        std::vector<Child> children;
        children.reserve(selected.size());
        for (const auto& voice : selected)
        {
            const auto found = std::find_if(instance->children.begin(), instance->children.end(),
                [&voice](const Child& child)
                {
                    return child.graph.branchKey == voice.branchKey && child.graph.clip == voice.clip;
                });
            if (found != instance->children.end())
            {
                children.push_back({ found->voice, voice });
                continue;
            }
            auto settings = instance->request.settings;
            settings.clip = voice.clip;
            settings.volume *= instance->gain * voice.gain;
            settings.pitch *= instance->pitch * voice.pitch;
            const auto handle = m_audio.Play(settings);
            if (!handle.IsValid())
            {
                for (const auto& child : children)
                {
                    m_audio.Stop(child.voice);
                }
                Finish(playback, PlaybackEndReason::Stopped);
                m_lastError = "Audio parameter branch could not start; playback stopped";
                return false;
            }
            m_audio.SetPaused(handle, instance->state == PlaybackState::Paused);
            children.push_back({ handle, voice });
        }
        if (std::any_of(children.begin(), children.end(), [this](const Child& child)
            { return !m_audio.IsAlive(child.voice); }))
        {
            for (const auto& child : children)
            {
                m_audio.Stop(child.voice);
            }
            Finish(playback, PlaybackEndReason::Stopped);
            m_lastError = "Audio policy displaced a child during graph parameter update; playback cancelled";
            return false;
        }
        instance->children = std::move(children);
        instance->parameters = std::move(parameters);
        if (!ApplySettings(*instance))
        {
            Finish(playback, PlaybackEndReason::Stopped);
            return false;
        }
        return true;
    }

    void PlaybackService::SetTransform(PlaybackHandle playback, const math::vector3& position,
        const math::vector3& velocity)
    {
        if (auto* instance = Find(playback))
        {
            instance->request.settings.position = position;
            instance->request.settings.velocity = velocity;
            for (const auto& child : instance->children)
            {
                m_audio.SetVoiceTransform(child.voice, position, velocity);
            }
        }
    }

    void PlaybackService::OwnerDestroyed(PlaybackScope scope, std::uint64_t ownerId)
    {
        if (ownerId == 0u || !IsScopeAlive(scope))
        {
            return;
        }
        for (std::size_t index = 0u; index < m_instances.size(); ++index)
        {
            auto& instance = m_instances[index];
            if (instance.state == PlaybackState::Stopped || instance.scope != scope
                || instance.request.settings.ownerId != ownerId)
            {
                continue;
            }
            if (instance.request.ownerPolicy == OwnerDestroyedPolicy::DetachAndFinish)
            {
                instance.request.settings.ownerId = 0u;
                // DetachAndFinish must actually finish even for an emitter that
                // had looping enabled. The scope still owns the detached tail.
                instance.request.settings.loop = false;
                ApplySettings(instance);
            }
            else
            {
                Finish({ static_cast<std::uint32_t>(index), instance.generation }, PlaybackEndReason::OwnerDestroyed);
            }
        }
    }

    PlaybackState PlaybackService::State(PlaybackHandle playback) const noexcept
    {
        const auto* instance = Find(playback);
        return instance == nullptr ? PlaybackState::Stopped : instance->state;
    }

    bool PlaybackService::IsAlive(PlaybackHandle playback) const noexcept
    {
        return Find(playback) != nullptr;
    }

    std::size_t PlaybackService::AliveCount() const noexcept
    {
        return static_cast<std::size_t>(std::count_if(m_instances.begin(), m_instances.end(), [](const Instance& instance)
        {
            return instance.state != PlaybackState::Stopped;
        }));
    }

    std::size_t PlaybackService::ChildVoiceCount(PlaybackHandle playback) const noexcept
    {
        const auto* instance = Find(playback);
        return instance == nullptr ? 0u : instance->children.size();
    }

    bool PlaybackService::RegisterGraph(const ClipKey& id, std::shared_ptr<const SoundGraphProgram> graph)
    {
        if (!id.IsGuid() || !graph)
        {
            m_lastError = "SoundGraph registration requires a GUID and a compiled program";
            return false;
        }
        auto validated = CompileSoundGraph(graph->definition, [this](const ClipKey& clip)
        {
            return HasSource({ SoundSourceKind::Clip, clip });
        }, m_lastError);
        if (!validated)
        {
            return false;
        }
        m_graphs[id] = std::move(validated);
        return true;
    }

    bool PlaybackService::RegisterPreset(const ClipKey& id, const SoundPreset& preset)
    {
        if (!id.IsGuid() || preset.source.kind == SoundSourceKind::Preset || !ValidSettings(preset.defaults)
            || !HasSource(preset.source))
        {
            m_lastError = "SoundPreset requires a GUID and a loaded clip or graph source";
            return false;
        }
        if (preset.source.kind == SoundSourceKind::Graph)
        {
            ParameterMap parameters;
            if (!ResolveGraphParameters(*m_graphs.at(preset.source.asset), preset.parameters, parameters, m_lastError))
            {
                return false;
            }
        }
        else if (!preset.parameters.empty())
        {
            m_lastError = "A clip preset cannot declare graph parameters";
            return false;
        }
        m_presets[id] = preset;
        return true;
    }

    void PlaybackService::UnregisterAsset(const ClipKey& id)
    {
        m_graphs.erase(id);
        m_presets.erase(id);
    }

    bool PlaybackService::HasSource(const SoundSource& source) const
    {
        if (!source.asset.IsGuid())
        {
            return false;
        }
        switch (source.kind)
        {
        case SoundSourceKind::Clip:
        {
            const auto clips = m_audio.ListClipKeys();
            return std::find(clips.begin(), clips.end(), source.asset) != clips.end();
        }
        case SoundSourceKind::Graph:
            return m_graphs.find(source.asset) != m_graphs.end();
        case SoundSourceKind::Preset:
            return m_presets.find(source.asset) != m_presets.end();
        default:
            return false;
        }
    }

    bool PlaybackService::Enqueue(PlaybackCommand command)
    {
        std::lock_guard lock(m_commandMutex);
        if (!m_acceptCommands || m_commands.size() >= kCommandCapacity
            || (command.kind == PlaybackCommandKind::Play && m_pendingResults >= kCommandCapacity))
        {
            return false;
        }
        if (command.kind == PlaybackCommandKind::Play)
        {
            ++m_pendingResults;
        }
        m_commands.push_back(std::move(command));
        return true;
    }

    void PlaybackService::Update()
    {
        std::deque<PlaybackCommand> commands;
        {
            std::lock_guard lock(m_commandMutex);
            commands.swap(m_commands);
        }
        for (auto& command : commands)
        {
            switch (command.kind)
            {
            case PlaybackCommandKind::Play:
            {
                const auto playback = Play(command.scope, command.request);
                m_started.push_back({ command.ticket, playback, m_lastError });
                break;
            }
            case PlaybackCommandKind::Stop:
                Stop(command.playback);
                break;
            case PlaybackCommandKind::Pause:
                SetPaused(command.playback, true);
                break;
            case PlaybackCommandKind::Resume:
                SetPaused(command.playback, false);
                break;
            case PlaybackCommandKind::SetGainPitch:
                SetGainPitch(command.playback, command.gain, command.pitch);
                break;
            case PlaybackCommandKind::SetParameter:
                (void)SetParameter(command.playback, command.parameter, std::move(command.value));
                break;
            }
        }
        for (std::size_t index = 0u; index < m_instances.size(); ++index)
        {
            auto& instance = m_instances[index];
            if (instance.state == PlaybackState::Stopped)
            {
                continue;
            }
            const bool alive = std::any_of(instance.children.begin(), instance.children.end(), [this](const Child& child)
            {
                return m_audio.IsAlive(child.voice);
            });
            if (!alive)
            {
                Finish({ static_cast<std::uint32_t>(index), instance.generation }, PlaybackEndReason::Finished);
            }
        }
    }

    void PlaybackService::Shutdown()
    {
        m_stopped = true;
        {
            std::lock_guard lock(m_commandMutex);
            m_acceptCommands = false;
            for (const auto& command : m_commands)
            {
                if (command.kind == PlaybackCommandKind::Play)
                {
                    m_started.push_back({ command.ticket, {}, "Audio playback service shut down before queued play" });
                }
            }
            m_commands.clear();
        }
        for (std::size_t index = 0u; index < m_scopes.size(); ++index)
        {
            if (m_scopes[index].alive)
            {
                EndScope({ static_cast<std::uint32_t>(index), m_scopes[index].generation });
            }
        }
        m_graphs.clear();
        m_presets.clear();
    }

    std::vector<PlaybackStarted> PlaybackService::TakeStarted()
    {
        std::lock_guard lock(m_commandMutex);
        std::vector<PlaybackStarted> result;
        result.swap(m_started);
        m_pendingResults -= result.size();
        return result;
    }

    bool PlaybackService::TryTakeCompletion(PlaybackCompletion& completion)
    {
        if (m_completions.empty())
        {
            return false;
        }
        completion = m_completions.front();
        m_completions.erase(m_completions.begin());
        return true;
    }

    std::vector<PlaybackCompletion> PlaybackService::TakeCompletions()
    {
        std::vector<PlaybackCompletion> result;
        result.swap(m_completions);
        return result;
    }
}
