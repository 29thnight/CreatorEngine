    double PcmEnergy(const std::vector<float>& samples)
    {
        double result = 0.0;
        for (const float sample : samples)
        {
            result += static_cast<double>(sample) * sample;
        }
        return result;
    }

    void RunRenderContracts(const Fixture& fixture)
    {
        std::printf("[Phase22] Actual miniaudio decode/mix, explicit no-device render; physical output untested\n");
        wave::MiniaudioBackend backend(true);
        wave::DeviceSettings settings;
        settings.noDevice = true;
        settings.sampleRate = kSampleRate;
        settings.channels = 2u;
        wave::AudioRuntime audio(backend, 16u);
        Report(audio.Start(settings), "Explicit no-device miniaudio engine starts");
        if (!backend.IsRunning())
        {
            std::printf("backend error: %s\n", backend.LastError().c_str());
            return;
        }
        const auto actual = backend.ActualSettings();
        Report(actual.noDevice && actual.sampleRate == kSampleRate && actual.channels == 2u,
            "Offline diagnostics preserve explicit no-device settings");
        for (const char* filename : { "tail.wav", "tail.mp3", "tail.flac" })
        {
            Report(!audio.LoadClip(wave::ClipKey(filename), fixture.root / "Formats" / filename),
                "Full source decoder rejects tail-truncated WAV/MP3/FLAC");
        }
        for (const char* filename : { "sine.wav", "silent.mp3", "silent.flac" })
        {
            const wave::ClipKey key{ filename };
            Report(audio.LoadClip(key, fixture.root / "Formats" / filename),
                "Real WAV/MP3/FLAC decoder accepts valid deterministic fixture");
            wave::PlayRequest request;
            request.clip = key;
            request.loop = true;
            auto voice = audio.Play(request);
            std::vector<float> pcm(2u * 4096u);
            Report(voice.IsValid() && backend.Render(pcm.data(), 4096u)
                && std::all_of(pcm.begin(), pcm.end(), [](float sample) { return std::isfinite(sample); }),
                "Offline mixer produces finite stereo PCM from decoded voice");
            if (std::string(filename) == "sine.wav")
            {
                const double baseEnergy = PcmEnergy(pcm);
                Report(baseEnergy > 1.0, "Non-silent WAV remains audible in captured PCM");
                audio.SetPaused(voice, true);
                std::fill(pcm.begin(), pcm.end(), 1.0f);
                const bool pauseRendered = backend.Render(pcm.data(), 4096u);
                std::size_t lastPauseSample = 0u;
                for (std::size_t index = 0u; index < pcm.size(); ++index)
                {
                    if (std::abs(pcm[index]) > 0.000001f)
                    {
                        lastPauseSample = index + 1u;
                    }
                }
                std::printf("PAUSE_CACHE last_nonzero_frame=%zu\n", (lastPauseSample + 1u) / 2u);
                Report(pauseRendered && lastPauseSample <= 2u * 256u,
                    "Pause drains no more than bounded 256-frame node graph lookahead");
                const auto pausedAt = audio.GetPlayhead(voice);
                Report(backend.Render(pcm.data(), 4096u) && audio.GetPlayhead(voice) == pausedAt
                    && PcmEnergy(pcm) < 0.000001,
                    "Paused real decoder playhead freezes and next rendered block is silent");
                audio.SetPaused(voice, false);
                Report(audio.Seek(voice, 0u), "Real source supports explicit seek");
                audio.SetBusVolume(wave::Buses::SFX, 0.5f);
                Report(backend.Render(pcm.data(), 4096u) && PcmEnergy(pcm) > baseEnergy * 0.15
                    && PcmEnergy(pcm) < baseEnergy * 0.35, "Category gain halves amplitude in actual PCM");
                audio.SetBusVolume(wave::Buses::SFX, 1.0f);
                audio.SetBusVolume(wave::Buses::Master, 0.0f);
                Report(backend.Render(pcm.data(), 4096u) && PcmEnergy(pcm) < 0.000001,
                    "Master mute silences actual PCM");
                audio.SetBusVolume(wave::Buses::Master, 1.0f);
                audio.SetLooping(voice, false);
                Report(audio.Seek(voice, 0u) && backend.Render(pcm.data(), 4096u),
                    "Runtime looping change and seek reach backend");
                (void)backend.Render(pcm.data(), 4096u);
                audio.Update(0.0f);
                Report(!audio.IsAlive(voice), "Nonlooping real WAV completes naturally after frame budget");
            }
            else
            {
                Report(PcmEnergy(pcm) < 0.000001, "Compressed silence decodes to silent PCM");
                audio.Stop(voice);
            }
            audio.UnloadClip(key);
        }
        Report(!backend.Render(nullptr, 32u), "Offline render rejects null output buffer");
        const wave::ClipKey impulse{ "impulse" };
        Report(audio.LoadClip(impulse, fixture.root / "Formats" / "impulse.wav"), "Reverb impulse fixture loads");
        auto renderImpulse = [&](wave::ReverbPreset preset, bool send)
        {
            audio.SetReverbPreset(preset);
            wave::PlayRequest request;
            request.clip = impulse;
            request.useReverbSend = send;
            request.reverbSendDecibels = 0.0f;
            auto voice = audio.Play(request);
            std::vector<float> pcm(2u * 24000u);
            const bool rendered = backend.Render(pcm.data(), 24000u);
            audio.Stop(voice);
            if (!rendered)
            {
                return -1.0;
            }
            std::fill(pcm.begin(), pcm.begin() + 2u * 4800u, 0.0f);
            return PcmEnergy(pcm);
        };
        const auto dryTail = renderImpulse(wave::ReverbPreset::Off, false);
        const auto wetTail = renderImpulse(wave::ReverbPreset::Hall, true);
        std::printf("REVERB_PCM dry_tail=%.9f wet_tail=%.9f\n", dryTail, wetTail);
        Report(dryTail >= 0.0 && dryTail < 0.000001 && wetTail > 0.000001,
            "Hall send creates measured decay beyond dry impulse clip");
        const auto naturalClip = TestGuid(1u);
        Report(audio.LoadClip(naturalClip, fixture.root / "Formats" / "sine.wav"),
            "GUID source loads for actual per-play natural completion");
        wave::PlaybackService playback(audio, 4u);
        const auto scope = playback.CreateScope(wave::ScopeKind::World);
        wave::SoundPreset preset;
        preset.source = { wave::SoundSourceKind::Clip, naturalClip };
        preset.defaults.loop = true;
        preset.defaults.persistent = true;
        Report(playback.RegisterPreset(TestGuid(23u), preset), "Looping preset registers for one-shot override contract");
        wave::PlaybackRequest oneShot;
        oneShot.source = { wave::SoundSourceKind::Preset, TestGuid(23u) };
        oneShot.loopOverride = false;
        const auto shot = playback.Play(scope, oneShot);
        std::vector<float> completionPcm(2u * 12000u);
        Report(shot.IsValid() && backend.Render(completionPcm.data(), 12000u),
            "One-shot override renders a looping preset exactly once");
        audio.Update(0.0f);
        playback.Update();
        const auto completions = playback.TakeCompletions();
        Report(!playback.IsAlive(shot) && completions.size() == 1u
            && completions[0].reason == wave::PlaybackEndReason::Finished,
            "Looping preset one-shot completes with Finished rather than surviving virtually");
        playback.Shutdown();
        audio.Shutdown();
        Report(!backend.IsRunning() && audio.AliveVoiceCount() == 0u,
            "Offline engine shuts down after active decode/mix");
    }

    void RunBlendContracts(const Fixture& fixture)
    {
        const auto energyAt = [&](float blend, float distance)
        {
            wave::MiniaudioBackend backend;
            wave::AudioRuntime audio(backend, 2u);
            wave::DeviceSettings settings;
            settings.noDevice = true;
            if (!audio.Start(settings) || !audio.LoadClip(TestGuid(1u), fixture.root / "Formats" / "sine.wav"))
            {
                return -1.0;
            }
            wave::PlayRequest request;
            request.clip = TestGuid(1u);
            request.loop = true;
            request.spatialBlend = blend;
            request.volume = 0.15f;
            request.position = { 0.0f, 0.0f, distance };
            request.minimumDistance = 1.0f;
            request.maximumDistance = 10.0f;
            const auto voice = audio.Play(request);
            std::vector<float> pcm(2u * 4096u);
            if (!voice.IsValid() || !backend.Render(pcm.data(), 4096u))
            {
                return -1.0;
            }
            return PcmEnergy(pcm);
        };
        const auto near2D = energyAt(0.0f, 0.0f);
        const auto near3D = energyAt(1.0f, 0.0f);
        const auto middle = energyAt(0.5f, 0.0f);
        const auto far2D = energyAt(0.0f, 100.0f);
        const auto far3D = energyAt(1.0f, 100.0f);
        const auto farMiddle = energyAt(0.5f, 100.0f);
        std::printf("BLEND_PCM near2D=%.6f near3D=%.6f middle=%.6f far2D=%.6f far3D=%.6f farMiddle=%.6f\n",
            near2D, near3D, middle, far2D, far3D, farMiddle);
        const auto predictedMiddle = std::pow((std::sqrt(near2D) + std::sqrt(near3D)) / std::sqrt(2.0), 2.0);
        Report(near2D > 0.0 && near3D > 0.0 && middle > near2D && middle > near3D
            && std::abs(middle - predictedMiddle) < predictedMiddle * 0.15,
            "Correlated source pairs follow cosine/sine equal-power blend law in actual PCM");
        Report(far3D >= 0.0 && far3D < 0.000001 && std::abs(far2D - near2D) < near2D * 0.01
            && std::abs(farMiddle - near2D * 0.5) < near2D * 0.05,
            "Distance attenuation affects only 3D branch while 2D and mixed dry branch remain");
    }

    void RunSpatialHandedness(const Fixture& fixture)
    {
        const auto channelsAt = [&](const math::vector3& position, const math::vector3& forward)
        {
            wave::MiniaudioBackend backend;
            wave::AudioRuntime audio(backend, 2u);
            wave::DeviceSettings settings;
            settings.noDevice = true;
            std::array<double, 2u> energy{ -1.0, -1.0 };
            if (!audio.Start(settings) || !audio.LoadClip(TestGuid(1u), fixture.root / "Formats" / "sine.wav"))
            {
                return energy;
            }
            wave::ListenerState listener;
            listener.forward = forward;
            audio.SetListener(listener);
            wave::PlayRequest request;
            request.clip = TestGuid(1u);
            request.loop = true;
            request.spatialBlend = 1.0f;
            request.position = position;
            const auto voice = audio.Play(request);
            std::vector<float> pcm(2u * 4096u);
            if (!voice.IsValid() || !backend.Render(pcm.data(), 4096u))
            {
                return energy;
            }
            energy = {};
            for (std::size_t index = 0u; index < pcm.size(); ++index)
            {
                energy[index % 2u] += static_cast<double>(pcm[index]) * pcm[index];
            }
            return energy;
        };
        const auto right = channelsAt({ 5.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 1.0f });
        const auto left = channelsAt({ -5.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 1.0f });
        const auto rotated = channelsAt({ 0.0f, 0.0f, -5.0f }, { 1.0f, 0.0f, 0.0f });
        std::printf("SPATIAL_PCM plusX_L=%.6f plusX_R=%.6f minusX_L=%.6f minusX_R=%.6f rotated_L=%.6f rotated_R=%.6f\n",
            right[0], right[1], left[0], left[1], rotated[0], rotated[1]);
        Report(right[1] > 0.0 && right[1] > right[0] * 1.5,
            "Engine +X source pans right for +Z-forward listener");
        Report(left[0] > 0.0 && left[0] > left[1] * 1.5,
            "Engine -X source pans left for +Z-forward listener");
        Report(rotated[1] > 0.0 && rotated[1] > rotated[0] * 1.5,
            "Rotated +X-forward listener hears world -Z on its right");
    }

    void RunSoftwareDeviceRecovery(const Fixture& fixture)
    {
        wave::MiniaudioBackend backend;
        wave::AudioRuntime audio(backend, 2u);
        const wave::ClipKey key("recovery");
        Report(audio.Start({}) && audio.LoadClip(key, fixture.root / "Formats" / "loop.wav"),
            "Software-device recovery fixture starts");
        wave::PlayRequest request;
        request.clip = key;
        request.loop = true;
        const auto voice = audio.Play(request);
        bool recovered = voice.IsValid();
        for (unsigned index = 0u; index < 20u; ++index)
        {
            backend.RequestDeviceRestart();
            audio.Update(0.01f);
            const auto diagnostics = backend.DeviceDiagnostics();
            recovered = recovered && audio.IsAlive(voice) && !diagnostics.outputInterrupted;
        }
        const auto diagnostics = backend.DeviceDiagnostics();
        Report(recovered && diagnostics.restartAttempts == 20u && diagnostics.successfulRestarts == 20u,
            "Twenty software-device restarts preserve same logical looping handle and recover output");
        audio.Shutdown();
    }
