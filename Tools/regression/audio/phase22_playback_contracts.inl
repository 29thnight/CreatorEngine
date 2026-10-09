    // Included inside the probe's anonymous namespace; production files are linked unchanged.
    wave::ClipKey TestGuid(unsigned value)
    {
        auto guid = Uuid::Parse("01234567-89ab-4cde-8fab-0123456789ab");
        guid.data[15] = static_cast<std::uint8_t>(value);
        return wave::ClipKey::FromGuid(guid);
    }

    wave::SoundGraphDefinition TwoClipGraph(wave::SoundNodeKind kind)
    {
        wave::SoundGraphDefinition result;
        wave::SoundNode first;
        first.id = 1u;
        first.clip = TestGuid(1u);
        wave::SoundNode second = first;
        second.id = 2u;
        second.clip = TestGuid(2u);
        wave::SoundNode combine;
        combine.id = 3u;
        combine.kind = kind;
        combine.inputs = { 1u, 2u };
        wave::SoundNode output;
        output.id = 4u;
        output.kind = wave::SoundNodeKind::Output;
        output.inputs = { 3u };
        result.nodes = { first, second, combine, output };
        result.output = 4u;
        return result;
    }

    std::shared_ptr<const wave::SoundGraphProgram> CompileTestGraph(
        const wave::SoundGraphDefinition& definition, std::string& error)
    {
        return wave::CompileSoundGraph(definition, [](const wave::ClipKey& key)
        {
            return key == TestGuid(1u) || key == TestGuid(2u);
        }, error);
    }

    void RunGraphContracts()
    {
        std::printf("[Phase22] Typed SoundGraph compiler and deterministic evaluation\n");
        std::string error;
        auto definition = TwoClipGraph(wave::SoundNodeKind::Layer);
        auto program = CompileTestGraph(definition, error);
        Report(program && program->maximumVoices == 2u, "Layer compiles its worst-case two-voice bound");
        if (!program)
        {
            return;
        }
        std::uint64_t state = 7u;
        std::vector<wave::GraphVoice> voices;
        Report(wave::EvaluateSoundGraph(*program, {}, state, voices, error)
            && voices.size() == 2u && voices[0].clip == TestGuid(1u)
            && voices[1].clip == TestGuid(2u) && voices[0].branchKey != voices[1].branchKey,
            "Layer emits ordered independent branch identities");
        const auto valid = definition;
        auto reject = [&](wave::SoundGraphDefinition bad, const char* label)
        {
            Report(!CompileTestGraph(bad, error) && !error.empty(), label);
        };
        definition.maximumVoices = 1u;
        reject(definition, "Layer expansion over maximumVoices fails closed");
        definition = valid;
        definition.nodes[2].inputs = { 3u };
        reject(definition, "Connected graph cycle rejected");
        definition = valid;
        auto detached = definition.nodes[2];
        detached.id = 20u;
        detached.inputs = { 20u };
        definition.nodes.push_back(detached);
        reject(definition, "Disconnected cycle rejected before publication");
        definition = valid;
        definition.nodes[0].clip = TestGuid(90u);
        reject(definition, "Missing clip dependency rejected");
        definition = valid;
        definition.nodes[0].clip = wave::ClipKey("legacy_stem");
        reject(definition, "Graph clip nodes require GUID identity");
        definition = valid;
        definition.nodes[0].id = 2u;
        reject(definition, "Duplicate node identity rejected");
        definition = valid;
        definition.nodes[2].inputs = { 99u };
        reject(definition, "Dangling audio input rejected");
        definition = valid;
        definition.nodes[3].inputs.clear();
        reject(definition, "Output without audio input rejected");
        definition = valid;
        definition.schemaVersion = 99u;
        reject(definition, "Unknown schema rejected");
        definition = valid;
        definition.maximumVoices = 257u;
        reject(definition, "Unbounded declared voices rejected");
        definition = TwoClipGraph(wave::SoundNodeKind::Random);
        definition.nodes[2].weights = { 0.0f, 0.0f };
        reject(definition, "All-zero random weights rejected");
        definition.nodes[2].weights = { -1.0f, 1.0f };
        reject(definition, "Negative random weight rejected");
        definition.nodes[2].weights = { std::numeric_limits<float>::quiet_NaN(), 1.0f };
        reject(definition, "Nonfinite random weight rejected");
        definition.nodes[2].weights = { 0.0f, 1.0f };
        program = CompileTestGraph(definition, error);
        Report(program && wave::EvaluateSoundGraph(*program, {}, state, voices, error)
            && voices.size() == 1u && voices[0].clip == TestGuid(2u),
            "Zero-weight branch is never chosen");
        definition.nodes[2].weights = { 1.0f, 3.0f };
        program = CompileTestGraph(definition, error);
        std::uint64_t firstState = 0xabcdefu;
        std::uint64_t secondState = firstState;
        bool deterministic = true;
        unsigned firstHits = 0u;
        for (unsigned index = 0u; index < 128u && program; ++index)
        {
            std::vector<wave::GraphVoice> again;
            deterministic = deterministic && wave::EvaluateSoundGraph(*program, {}, firstState, voices, error)
                && wave::EvaluateSoundGraph(*program, {}, secondState, again, error)
                && voices.size() == 1u && again.size() == 1u && voices[0].clip == again[0].clip
                && voices[0].branchKey == again[0].branchKey && firstState == secondState;
            firstHits += voices[0].clip == TestGuid(1u) ? 1u : 0u;
        }
        Report(program && deterministic && firstHits > 0u && firstHits < 128u,
            "Seeded random sequence is reproducible and visits both weighted branches");
        definition = TwoClipGraph(wave::SoundNodeKind::Switch);
        definition.parameters = { { "material", wave::ParameterType::String, std::string("stone") },
            { "gain", wave::ParameterType::Float, 0.5f },
            { "pitch", wave::ParameterType::Integer, std::int32_t{ 2 } },
            { "enabled", wave::ParameterType::Boolean, true } };
        definition.nodes[2].parameter = "material";
        definition.nodes[2].cases = { std::string("stone") };
        wave::SoundNode gain;
        gain.id = 5u;
        gain.kind = wave::SoundNodeKind::GainPitch;
        gain.inputs = { 3u };
        gain.gain = 0.5f;
        gain.pitch = 0.75f;
        gain.gainParameter = "gain";
        gain.pitchParameter = "pitch";
        definition.nodes[3].inputs = { 5u };
        definition.nodes.push_back(gain);
        program = CompileTestGraph(definition, error);
        Report(program && wave::EvaluateSoundGraph(*program, {}, state, voices, error)
            && voices.size() == 1u && voices[0].clip == TestGuid(1u)
            && std::abs(voices[0].gain - 0.25f) < 0.0001f
            && std::abs(voices[0].pitch - 1.5f) < 0.0001f,
            "Typed defaults drive Switch and multiplicative GainPitch");
        wave::ParameterMap supplied{ { "material", std::string("wood") } };
        Report(program && wave::EvaluateSoundGraph(*program, supplied, state, voices, error)
            && voices[0].clip == TestGuid(2u), "Unmatched Switch uses explicit default branch");
        supplied = { { "gain", std::int32_t{ 1 } } };
        Report(program && !wave::EvaluateSoundGraph(*program, supplied, state, voices, error)
            && voices.empty(), "Float parameter rejects integer override without coercion");
        supplied = { { "unknown", 1.0f } };
        Report(program && !wave::EvaluateSoundGraph(*program, supplied, state, voices, error),
            "Unknown parameter rejected");
        supplied = { { "gain", std::numeric_limits<float>::infinity() } };
        Report(program && !wave::EvaluateSoundGraph(*program, supplied, state, voices, error),
            "Nonfinite typed override rejected");
        supplied = { { "pitch", std::int32_t{ 0 } } };
        Report(program && !wave::EvaluateSoundGraph(*program, supplied, state, voices, error),
            "Evaluated zero pitch rejected");
        definition.parameters.push_back(definition.parameters.front());
        reject(definition, "Duplicate parameter declaration rejected");
    }

    void RunPlaybackContracts()
    {
        std::printf("[Phase22] Per-play lifetimes, scopes, ownership and bounded queues\n");
        Report(wave::VoiceTable::NextGeneration(0x7ffffffeu, 0u) == 0x7fffffffu
            && wave::VoiceTable::NextGeneration(0x7fffffffu, 0u) == 0u,
            "Final device generation retires instead of wrapping into stale handles");
        Report(wave::VoiceTable::NextGeneration(0xfffffffeu, 0x80000000u) == 0xffffffffu
            && wave::VoiceTable::NextGeneration(0xffffffffu, 0x80000000u) == 0u,
            "Final Null namespace generation retires without crossing namespace");
        wave::NullAudioBackend backend;
        wave::AudioRuntime audio(backend, 16u);
        Report(audio.Start({}), "Playback Null runtime starts");
        Report(audio.LoadClip(TestGuid(1u), {}) && audio.LoadClip(TestGuid(2u), {}),
            "GUID clips loaded into logical backend");
        wave::PlaybackService playback(audio, 8u);
        const auto world = playback.CreateScope(wave::ScopeKind::World);
        const auto session = playback.CreateScope(wave::ScopeKind::Session);
        const auto preview = playback.CreateScope(wave::ScopeKind::EditorPreview);
        wave::PlaybackRequest request;
        request.source = { wave::SoundSourceKind::Clip, TestGuid(1u) };
        request.settings.ownerId = 99u;
        request.settings.loop = true;
        auto first = playback.Play(world, request);
        auto second = playback.Play(world, request);
        Report(first.IsValid() && second.IsValid() && first != second && playback.AliveCount() == 2u,
            "Each Play allocates an independent logical instance");
        Report(wave::PlaybackHandle::FromValue(first.Value()) == first, "Playback handle wire value roundtrips");
        playback.SetPaused(first, true);
        Report(playback.State(first) == wave::PlaybackState::Paused
            && playback.State(second) == wave::PlaybackState::Playing, "Pause affects only the selected play");
        playback.Stop(first);
        const auto replacement = playback.Play(world, request);
        playback.Stop(first);
        playback.SetPaused(first, true);
        playback.SetGainPitch(first, 0.0f, 1.0f);
        Report(replacement.IsValid() && replacement != first && playback.IsAlive(replacement)
            && playback.State(replacement) == wave::PlaybackState::Playing,
            "Stale logical handle cannot control replacement instance");
        auto completed = playback.TakeCompletions();
        Report(completed.size() == 1u && completed[0].playback == first
            && completed[0].reason == wave::PlaybackEndReason::Stopped
            && playback.TakeCompletions().empty(), "Completion is delivered once then drained");
        const auto sessionPlay = playback.Play(session, request);
        const auto previewPlay = playback.Play(preview, request);
        playback.EndScope(world);
        Report(!playback.IsAlive(second) && !playback.IsAlive(replacement)
            && playback.IsAlive(sessionPlay) && playback.IsAlive(previewPlay),
            "World teardown preserves session and editor preview scopes");
        Report(!playback.Play(world, request).IsValid(), "Ended scope rejects new playback");
        const auto nextWorld = playback.CreateScope(wave::ScopeKind::World);
        playback.EndScope(world);
        Report(nextWorld != world && playback.IsScopeAlive(nextWorld),
            "Stale scope cannot end reused scope slot");
        auto stopOwner = playback.Play(nextWorld, request);
        request.ownerPolicy = wave::OwnerDestroyedPolicy::DetachAndFinish;
        auto detachOwner = playback.Play(nextWorld, request);
        playback.OwnerDestroyed(nextWorld, 99u);
        Report(!playback.IsAlive(stopOwner) && playback.IsAlive(detachOwner)
            && playback.IsAlive(sessionPlay), "Owner destruction stops or detaches only within its scope");
        playback.OwnerDestroyed(nextWorld, 99u);
        Report(playback.IsAlive(detachOwner), "Detached play no longer matches destroyed owner");
        request.settings.ownerId = 123u;
        const auto policyEdit = playback.Play(nextWorld, request);
        playback.SetOwnerPolicy(policyEdit, wave::OwnerDestroyedPolicy::Stop);
        playback.OwnerDestroyed(nextWorld, 123u);
        Report(!playback.IsAlive(policyEdit), "Live owner policy update changes subsequent owner-destruction behavior");
        playback.EndScope(nextWorld);
        Report(!playback.IsAlive(detachOwner), "Detached play still obeys original scope lifetime");
        playback.EndScope(session);
        playback.EndScope(preview);
        (void)playback.TakeCompletions();
        const auto graphScope = playback.CreateScope(wave::ScopeKind::World);
        std::string error;
        auto definition = TwoClipGraph(wave::SoundNodeKind::Layer);
        auto graph = CompileTestGraph(definition, error);
        Report(playback.RegisterGraph(TestGuid(20u), graph), "Compiled Layer graph registered");
        auto mutableProgram = std::make_shared<wave::SoundGraphProgram>(*graph);
        mutableProgram->nodeIndices.clear();
        mutableProgram->maximumVoices = 999999u;
        Report(playback.RegisterGraph(TestGuid(22u), mutableProgram),
            "Registration recompiles definition rather than trusting supplied indices/bounds");
        mutableProgram->definition.nodes[2].inputs.clear();
        wave::PlaybackRequest snapshotRequest;
        snapshotRequest.source = { wave::SoundSourceKind::Graph, TestGuid(22u) };
        const auto snapshotPlay = playback.Play(graphScope, snapshotRequest);
        Report(snapshotPlay.IsValid() && playback.ChildVoiceCount(snapshotPlay) == 2u,
            "Published graph remains immutable after caller mutates original program");
        playback.Stop(snapshotPlay);
        request = {};
        request.source = { wave::SoundSourceKind::Graph, TestGuid(20u) };
        request.settings.loop = true;
        const auto layer = playback.Play(graphScope, request);
        Report(layer.IsValid() && playback.ChildVoiceCount(layer) == 2u
            && playback.AliveCount() == 1u && audio.AliveVoiceCount() == 2u,
            "Layer graph owns two voices under one PlaybackHandle");
        playback.Stop(layer);
        Report(audio.AliveVoiceCount() == 0u, "Stopping graph stops every child voice");
        definition = TwoClipGraph(wave::SoundNodeKind::Switch);
        definition.parameters = { { "variant", wave::ParameterType::Integer, std::int32_t{ 1 } } };
        definition.nodes[2].parameter = "variant";
        definition.nodes[2].cases = { std::int32_t{ 1 }, std::int32_t{ 2 } };
        graph = CompileTestGraph(definition, error);
        Report(playback.RegisterGraph(TestGuid(21u), graph), "Typed Switch graph registered");
        request.source.asset = TestGuid(21u);
        const auto switchA = playback.Play(graphScope, request);
        const auto switchB = playback.Play(graphScope, request);
        const auto attempts = backend.StartVoiceAttempts();
        Report(playback.SetParameter(switchA, "variant", std::int32_t{ 1 })
            && backend.StartVoiceAttempts() == attempts, "Unchanged parameter retains existing branch/playhead");
        Report(!playback.SetParameter(switchA, "variant", 2.0f) && playback.IsAlive(switchA),
            "Wrong typed runtime parameter leaves playback alive");
        playback.SetPaused(switchA, true);
        Report(playback.SetParameter(switchA, "variant", std::int32_t{ 2 })
            && backend.StartVoiceAttempts() == attempts + 1u
            && playback.State(switchA) == wave::PlaybackState::Paused
            && playback.ChildVoiceCount(switchB) == 1u,
            "Parameter branch switch is per-play and preserves paused state");
        playback.UnregisterAsset(TestGuid(21u));
        Report(playback.IsAlive(switchA) && playback.IsAlive(switchB)
            && !playback.Play(graphScope, request).IsValid(),
            "Unregister prevents new plays while existing immutable program survives");
        playback.Stop(switchA);
        playback.Stop(switchB);
        auto preemptGraph = TwoClipGraph(wave::SoundNodeKind::Layer);
        preemptGraph.parameters = { { "variant", wave::ParameterType::Integer, std::int32_t{ 1 } } };
        wave::SoundNode preemptSwitch;
        preemptSwitch.id = 5u;
        preemptSwitch.kind = wave::SoundNodeKind::Switch;
        preemptSwitch.parameter = "variant";
        preemptSwitch.inputs = { 2u, 1u };
        preemptSwitch.cases = { std::int32_t{ 1 }, std::int32_t{ 2 } };
        preemptGraph.nodes[2].inputs = { 1u, 5u };
        preemptGraph.nodes.push_back(preemptSwitch);
        Report(playback.RegisterGraph(TestGuid(24u), CompileTestGraph(preemptGraph, error)),
            "Layer with parameter-controlled preempting branch compiles");
        request.source = { wave::SoundSourceKind::Graph, TestGuid(24u) };
        request.settings.preemptSameClip = true;
        const auto preemptPlay = playback.Play(graphScope, request);
        Report(preemptPlay.IsValid() && playback.ChildVoiceCount(preemptPlay) == 2u,
            "Non-conflicting initial layer admission owns both voices");
        Report(!playback.SetParameter(preemptPlay, "variant", std::int32_t{ 2 })
            && !playback.IsAlive(preemptPlay) && audio.AliveVoiceCount() == 0u,
            "Parameter branch that preempts sibling cancels whole graph instead of reporting partial success");
        playback.EndScope(graphScope);
        (void)playback.TakeCompletions();
        const auto queueScope = playback.CreateScope(wave::ScopeKind::World);
        request = {};
        request.source = { wave::SoundSourceKind::Clip, TestGuid(1u) };
        const auto queued = playback.Play(queueScope, request);
        std::atomic<unsigned> accepted{ 0u };
        std::vector<std::thread> producers;
        for (unsigned thread = 0u; thread < 4u; ++thread)
        {
            producers.emplace_back([&]()
            {
                for (unsigned index = 0u; index < 256u; ++index)
                {
                    wave::PlaybackCommand command;
                    command.kind = wave::PlaybackCommandKind::Pause;
                    command.playback = queued;
                    accepted += playback.Enqueue(command) ? 1u : 0u;
                }
            });
        }
        for (auto& producer : producers)
        {
            producer.join();
        }
        wave::PlaybackCommand stop;
        stop.playback = queued;
        Report(accepted == 1024u && !playback.Enqueue(stop), "Mailbox accepts 1024 concurrent commands and rejects overflow");
        playback.Update();
        Report(playback.State(queued) == wave::PlaybackState::Paused && playback.Enqueue(stop),
            "Owner update drains queue and frees bounded mailbox capacity");
        playback.Update();
        Report(!playback.IsAlive(queued), "Queued stop executes only when owner updates");
        wave::PlaybackCommand playCommand;
        playCommand.kind = wave::PlaybackCommandKind::Play;
        playCommand.scope = queueScope;
        playCommand.request = request;
        playCommand.ticket = 123u;
        const bool playQueued = playback.Enqueue(playCommand);
        Report(playQueued && playback.AliveCount() == 0u,
            "Queued Play carries values without touching backend on producer thread");
        playback.Update();
        auto started = playback.TakeStarted();
        Report(started.size() == 1u && started[0].ticket == 123u && started[0].playback.IsValid()
            && playback.IsAlive(started[0].playback) && playback.TakeStarted().empty(),
            "Owner update publishes queued play handle once with original ticket");
        if (!started.empty())
        {
            playback.Stop(started[0].playback);
        }
        playCommand.scope = world;
        playCommand.ticket = 124u;
        (void)playback.Enqueue(playCommand);
        playback.Update();
        started = playback.TakeStarted();
        Report(started.size() == 1u && started[0].ticket == 124u && !started[0].playback.IsValid()
            && !started[0].error.empty(), "Queued stale-scope Play returns correlated failure without backend voice");
        bool soak = true;
        auto stale = queued;
        for (unsigned index = 0u; index < 1000u; ++index)
        {
            const auto current = playback.Play(queueScope, request);
            playback.Stop(stale);
            soak = soak && current.IsValid() && playback.IsAlive(current);
            playback.Stop(current);
            stale = current;
        }
        completed = playback.TakeCompletions();
        Report(soak && playback.AliveCount() == 0u && audio.AliveVoiceCount() == 0u
            && completed.size() <= 32u, "1000-play bounded soak leaves no voices and bounded completion backlog");
        wave::PlaybackCompletion completion;
        Report(!playback.TryTakeCompletion(completion), "Single-completion pull is empty after full drain");
        playCommand.scope = queueScope;
        unsigned acceptedPlays = 0u;
        for (unsigned index = 0u; index < 1024u; ++index)
        {
            playCommand.ticket = 10000u + index;
            acceptedPlays += playback.Enqueue(playCommand) ? 1u : 0u;
        }
        playback.Update();
        Report(acceptedPlays == 1024u && !playback.Enqueue(playCommand),
            "Undrained queued-play results exert backpressure without losing accepted tickets");
        started = playback.TakeStarted();
        bool ticketsComplete = started.size() == 1024u;
        for (std::size_t index = 0u; index < started.size(); ++index)
        {
            ticketsComplete = ticketsComplete && started[index].ticket == 10000u + index;
            if (started[index].playback.IsValid())
            {
                playback.Stop(started[index].playback);
            }
        }
        Report(ticketsComplete, "All accepted queued Play tickets yield ordered success or explicit capacity failure");
        playCommand.ticket = 20000u;
        Report(playback.Enqueue(playCommand), "Draining result queue permits a new queued play");
        playback.Shutdown();
        started = playback.TakeStarted();
        Report(started.size() == 1u && started[0].ticket == 20000u && !started[0].playback.IsValid()
            && !started[0].error.empty(), "Shutdown returns cancellation result for accepted unprocessed Play ticket");
        Report(!playback.Enqueue(stop) && !playback.IsScopeAlive(queueScope),
            "Shutdown closes command producer and invalidates scopes");
        audio.Shutdown();
    }

    void RunPolicyContracts()
    {
        std::printf("[Phase22] Voice caps, steal, attenuation and virtualization\n");
        wave::NullAudioBackend backend;
        wave::AudioRuntime audio(backend, 8u);
        Report(audio.Start({}) && audio.LoadClip(TestGuid(1u), {}), "Policy runtime starts with GUID clip");
        wave::PlayRequest request;
        request.clip = TestGuid(1u);
        request.allowVirtualization = false;
        audio.ConfigureBus(wave::Buses::SFX, 1u, wave::StealPolicy::Reject);
        auto first = audio.Play(request);
        Report(first.IsValid() && !audio.Play(request).IsValid(), "Reject bus cap cannot allocate excess physical voice");
        audio.ConfigureBus(wave::Buses::SFX, 1u, wave::StealPolicy::Oldest);
        auto second = audio.Play(request);
        Report(second.IsValid() && !audio.IsAlive(first) && audio.Metrics().stolen >= 1u,
            "Oldest bus policy replaces prior one-shot");
        audio.Stop(second);
        audio.ConfigureBus(wave::Buses::SFX, 8u, wave::StealPolicy::Reject);
        audio.ConfigureConcurrencyGroup({ 7u }, 1u, wave::StealPolicy::Reject);
        request.concurrencyGroup = { 7u };
        first = audio.Play(request);
        request.bus = wave::Buses::UI;
        Report(first.IsValid() && !audio.Play(request).IsValid(), "Concurrency group limit spans buses");
        audio.Stop(first);
        request.concurrencyGroup = {};
        request.bus = wave::Buses::SFX;
        request.loop = true;
        request.allowVirtualization = true;
        audio.SetPhysicalVoiceLimit(1u);
        first = audio.Play(request);
        second = audio.Play(request);
        Report(first.IsValid() && second.IsValid() && audio.Metrics().active == 2u
            && audio.Metrics().physical == 1u && audio.Metrics().virtualized == 1u,
            "One physical slot retains second looping voice virtually");
        const auto virtualVoice = audio.GetVoiceState(first) == wave::VoiceState::Virtual ? first : second;
        const auto physicalVoice = virtualVoice == first ? second : first;
        audio.Update(0.1f);
        const auto progressed = audio.GetPlayhead(virtualVoice);
        Report(progressed > 0u, "Virtual looping playhead advances without backend voice");
        audio.SetPaused(virtualVoice, true);
        audio.Update(0.1f);
        Report(audio.GetPlayhead(virtualVoice) == progressed, "Paused virtual voice freezes playhead");
        audio.SetPaused(virtualVoice, false);
        audio.Stop(physicalVoice);
        audio.Update(0.01f);
        Report(audio.GetVoiceState(virtualVoice) == wave::VoiceState::Physical,
            "Virtual looping voice resumes when physical capacity becomes available");
        audio.Stop(virtualVoice);
        request.loop = false;
        request.allowVirtualization = false;
        request.priority = 200;
        audio.ConfigureBus(wave::Buses::SFX, 1u, wave::StealPolicy::LowestPriority);
        first = audio.Play(request);
        request.priority = 16;
        second = audio.Play(request);
        Report(second.IsValid() && !audio.IsAlive(first), "Lower numeric priority displaces lower-importance physical one-shot");
        request.priority = 220;
        Report(!audio.Play(request).IsValid() && audio.IsAlive(second),
            "Less important one-shot cannot displace higher-priority voice");
        audio.Stop(second);
        audio.ConfigureBus(wave::Buses::SFX, 1u, wave::StealPolicy::Quietest);
        request.priority = 128;
        request.volume = 0.8f;
        first = audio.Play(request);
        request.volume = 0.2f;
        Report(!audio.Play(request).IsValid() && audio.IsAlive(first),
            "Quietest policy drops quieter incoming one-shot");
        request.volume = 1.0f;
        second = audio.Play(request);
        Report(second.IsValid() && !audio.IsAlive(first), "Louder incoming one-shot displaces quietest physical voice");
        audio.Stop(second);
        audio.ConfigureBus(wave::Buses::SFX, 8u, wave::StealPolicy::Reject);
        audio.SetPhysicalVoiceLimit(8u);
        request.loop = true;
        request.allowVirtualization = true;
        request.preemptSameClip = true;
        first = audio.Play(request);
        second = audio.Play(request);
        Report(second.IsValid() && !audio.IsAlive(first) && audio.AliveVoiceCount() == 1u,
            "Same-clip preemption replaces old logical voice rather than only backend channel");
        audio.Stop(second);
        request.preemptSameClip = false;
        wave::ListenerState listener;
        request.minimumDistance = 1.0f;
        request.maximumDistance = 11.0f;
        request.position = { 6.0f, 0.0f, 0.0f };
        request.rolloff = wave::RolloffKind::Linear;
        Report(std::abs(wave::AudioRuntime::SampleAttenuation(request, listener) - 0.5f) < 0.0001f,
            "Linear attenuation samples authored min/max distances");
        request.rolloff = wave::RolloffKind::Inverse;
        Report(std::abs(wave::AudioRuntime::SampleAttenuation(request, listener) - 1.0f / 6.0f) < 0.0001f,
            "Inverse attenuation samples reference distance");
        request.rolloff = wave::RolloffKind::Custom;
        request.customRolloff = { { 0.0f, 1.0f }, { 10.0f, 0.0f } };
        Report(std::abs(wave::AudioRuntime::SampleAttenuation(request, listener) - 0.4f) < 0.0001f,
            "Custom attenuation interpolates authored curve");
        audio.SetBusVolume(wave::Buses::Master, 0.5f);
        audio.SetBusVolume(wave::Buses::SFX, 0.25f);
        Report(audio.GetBusVolume(wave::Buses::Master) == 0.5f
            && audio.GetBusVolume(wave::Buses::SFX) == 0.25f, "Master and category gains remain independently addressable");
        audio.Shutdown();
        Report(audio.AliveVoiceCount() == 0u && !backend.IsRunning(), "Policy shutdown releases logical and backend resources");
    }
