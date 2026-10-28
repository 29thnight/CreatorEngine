// Real consumer implementation, stubbed scene/entity/logger dependencies.
// This is a device-free domain probe, not a Windows/product integration test.
#include "SoundComponent.h"
#include "AudioListenerComponent.h"
#include "Scene.h"
#if defined(CE_CONTRACT_STANDALONE)
#include "../build/ModuleAdapters/Creator.Contracts.h"
#else
import Creator.Contracts;
#endif
#include "../../Engine/Utility_Framework/Foundation/StandardLibrary.h"

class FakeAudio final : public wave::AudioService
{
public:
    wave::ClipKey key{wave::ClipKey::FromGuid(uuid::parse("cc4949ac-22b2-42f1-ab2e-1775b5776611"))};
    std::unordered_map<std::uint32_t,wave::PlayRequest> voices;
    wave::ListenerState listener;
    std::uint32_t serial{};
    bool rejectDistant{};
    bool rejectSpatialEdits{};
    std::string lastError;
    wave::VoiceHandle Play(const wave::PlayRequest& request) override
    {
        if (request.clip != key) { return {}; }
        if (rejectDistant && request.spatialBlend > 0.0f &&
            std::abs(request.position.x-listener.position.x) > request.maximumDistance) { return {}; }
        const auto id=++serial; voices.emplace(id,request); return {id,1};
    }
    void Stop(wave::VoiceHandle h) override { voices.erase(h.index); }
    void SetPaused(wave::VoiceHandle,bool) override {}
    bool IsAlive(wave::VoiceHandle h) const override { return voices.contains(h.index); }
    void StopByOwner(std::uint64_t) override {}
    void SetVoiceTransform(wave::VoiceHandle h,const math::vector3& position,const math::vector3& velocity) override
    { if (voices.contains(h.index)) { voices[h.index].position=position; voices[h.index].velocity=velocity; } }
    void SetVoiceParameters(wave::VoiceHandle,float,float,int) override {}
    void SetVoiceSettings(wave::VoiceHandle h,const wave::PlayRequest& settings) override
    {
        lastError.clear();
        if (rejectSpatialEdits && settings.spatialBlend > 0.0f)
        {
            lastError="Spatial playback requires a mono clip";
            return;
        }
        if (voices.contains(h.index)) { voices[h.index]=settings; }
    }
    const std::string& LastError() const override { return lastError; }
    void SetVoiceGain(wave::VoiceHandle,float) override {}
    void SetListener(const wave::ListenerState& state) override { listener=state; }
    bool LoadClip(const wave::ClipKey&,const std::filesystem::path&) override { return true; }
    bool LoadCookedClip(const experiment::cooked::CookedAudioClipSource&) override { return true; }
    void UnloadClip(const wave::ClipKey&) override {}
    ce::dynamic_array<wave::ClipKey> ListClipKeys() const override { return {key}; }
    void Update(float) override {}
};

int main()
{
    FakeAudio audio; wave::PlaybackService playback(audio);
    Scene scene; Entity entity; entity.scene=&scene; scene.entity=&entity;
    SoundComponent sound; sound.owner=&entity; sound.id=11; entity.sound=&sound;
    scene.Sounds().Bind(scene,&playback,[&](std::string_view name) { return name=="legacy"?audio.key:wave::ClipKey{}; });
    scene.Sounds().Activate(); sound.OnAddedToScene();
    AudioListenerComponent startupListener; startupListener.owner=&entity; startupListener.id=91;
    startupListener.OnAddedToScene(); audio.rejectDistant=true;
    auto startup=sound.ReadSettings(); startup.clipKey="legacy"; startup.spatial=true;
    startup.playOnStart=true; startup.maxDistance=50.0f; sound.ApplySettings(startup);
    entity.transform.SetPending({100,0,0}); sound.OnBeginSimulation();
    ::ce::contracts::enforce_that(
        [&]() -> bool {
            return static_cast<bool>((playback.IsAlive(sound.CurrentPlayback()) && audio.listener.position.x == 100));
        },
        "playback.IsAlive(sound.CurrentPlayback()) && audio.listener.position.x==100");
    sound.Stop(); startupListener.OnRemovingFromScene(); audio.rejectDistant=false;
    auto settings=sound.ReadSettings(); settings.clipKey="legacy"; settings.loop=true; settings.spatial=false;
    sound.ApplySettings(settings);
    entity.transform.SetPending({10,0,0});
    const auto loop=sound.PlayInstance(); ::ce::contracts::enforce_that(
        [&]() -> bool {
            return static_cast<bool>((playback.IsAlive(loop)));
        },
        "playback.IsAlive(loop)");
    ::ce::contracts::enforce_that(
        [&]() -> bool {
            return static_cast<bool>((sound.ReadSettings().clipKey == audio.key.Text()));
        },
        "sound.ReadSettings().clipKey==audio.key.Text()");
    ::ce::contracts::enforce_that(
        [&]() -> bool {
            return static_cast<bool>((audio.voices.at(audio.serial).position.x == 10));
        },
        "audio.voices.at(audio.serial).position.x==10");
    ::ce::contracts::enforce_that(
        [&]() -> bool {
            return static_cast<bool>(
                (audio.voices.at(audio.serial).spatialBlend == 0 && audio.voices.at(audio.serial).loop));
        },
        "audio.voices.at(audio.serial).spatialBlend==0 && audio.voices.at(audio.serial).loop");
    const auto loopVoice=audio.serial;
    const auto one=sound.PlayInstance(true); const auto shotVoice=audio.serial;
    ::ce::contracts::enforce_that(
        [&]() -> bool {
            return static_cast<bool>((playback.AliveCount() == 2 && playback.IsAlive(loop)));
        },
        "playback.AliveCount()==2 && playback.IsAlive(loop)");
    ::ce::contracts::enforce_that(
        [&]() -> bool {
            return static_cast<bool>((!audio.voices.at(shotVoice).loop && audio.voices.at(loopVoice).loop));
        },
        "!audio.voices.at(shotVoice).loop && audio.voices.at(loopVoice).loop");
    sound.SetVolume(0.25f);
    ::ce::contracts::enforce_that(
        [&]() -> bool {
            return static_cast<bool>(
                (audio.voices.at(loopVoice).volume == 0.25f && audio.voices.at(shotVoice).volume == 0.25f));
        },
        "audio.voices.at(loopVoice).volume==0.25f && audio.voices.at(shotVoice).volume==0.25f");
    ::ce::contracts::enforce_that(
        [&]() -> bool {
            return static_cast<bool>((audio.voices.at(loopVoice).loop && !audio.voices.at(shotVoice).loop));
        },
        "audio.voices.at(loopVoice).loop && !audio.voices.at(shotVoice).loop");
    const auto diagnosticsBefore=Debug::messages.size();
    audio.rejectSpatialEdits=true;
    auto rejected=sound.ReadSettings(); rejected.spatial=true; rejected.spatialBlend=1.0f;
    sound.QueueSettings(rejected); scene.Sounds().Update(0.1f);
    ::ce::contracts::enforce_that(
        [&]() -> bool {
            return static_cast<bool>((playback.IsAlive(loop) && audio.voices.at(loopVoice).spatialBlend == 0.0f));
        },
        "playback.IsAlive(loop) && audio.voices.at(loopVoice).spatialBlend==0.0f");
    ::ce::contracts::enforce_that(
        [&]() -> bool {
            return static_cast<bool>((audio.voices.at(shotVoice).spatialBlend == 0.0f));
        },
        "audio.voices.at(shotVoice).spatialBlend==0.0f");
    ::ce::contracts::enforce_that(
        [&]() -> bool {
            return static_cast<bool>((Debug::messages.size() == diagnosticsBefore + 1));
        },
        "Debug::messages.size()==diagnosticsBefore+1");
    ::ce::contracts::enforce_that(
        [&]() -> bool {
            return static_cast<bool>((Debug::messages.back().find("[audio.settings.rejected]") != std::string::npos));
        },
        "Debug::messages.back().find(\"[audio.settings.rejected]\")!=std::string::npos");
    sound.QueueSettings(rejected); scene.Sounds().Update(0.1f);
    ::ce::contracts::enforce_that(
        [&]() -> bool {
            return static_cast<bool>((Debug::messages.size() == diagnosticsBefore + 1));
        },
        "Debug::messages.size()==diagnosticsBefore+1");
    rejected.spatial=false; sound.QueueSettings(rejected); scene.Sounds().Update(0.1f);
    audio.rejectSpatialEdits=false;
    sound.QueuePreview(SoundComponent::PreviewCommand::Play); scene.Sounds().Update(0.1f);
    ::ce::contracts::enforce_that(
        [&]() -> bool {
            return static_cast<bool>((playback.AliveCount() == 3));
        },
        "playback.AliveCount()==3");
    sound.QueuePreview(SoundComponent::PreviewCommand::Stop); scene.Sounds().Update(0.1f);
    ::ce::contracts::enforce_that(
        [&]() -> bool {
            return static_cast<bool>((playback.AliveCount() == 2 && playback.IsAlive(loop) && playback.IsAlive(one)));
        },
        "playback.AliveCount()==2 && playback.IsAlive(loop) && playback.IsAlive(one)");
    sound.QueuePreview(SoundComponent::PreviewCommand::Play); scene.Sounds().Update(0.1f);
    ::ce::contracts::enforce_that(
        [&]() -> bool {
            return static_cast<bool>((playback.AliveCount() == 3));
        },
        "playback.AliveCount()==3");
    std::this_thread::sleep_for(std::chrono::milliseconds(2100));
    scene.Sounds().Update(0.0f, false);
    ::ce::contracts::enforce_that(
        [&]() -> bool {
            return static_cast<bool>((playback.AliveCount() == 2 && playback.IsAlive(loop)));
        },
        "playback.AliveCount()==2 && playback.IsAlive(loop)");
    sound.Pause(true); ::ce::contracts::enforce_that(
        [&]() -> bool {
            return static_cast<bool>((playback.State(loop) == wave::PlaybackState::Paused));
        },
        "playback.State(loop)==wave::PlaybackState::Paused");
    sound.Pause(false); ::ce::contracts::enforce_that(
        [&]() -> bool {
            return static_cast<bool>((playback.State(loop) == wave::PlaybackState::Playing));
        },
        "playback.State(loop)==wave::PlaybackState::Playing");

    const auto presetKey=wave::ClipKey::FromGuid(uuid::parse("dd4949ac-22b2-42f1-ab2e-1775b5776611"));
    wave::SoundPreset preset; preset.source={wave::SoundSourceKind::Clip,audio.key};
    preset.defaults.volume=0.7f; preset.defaults.loop=true; preset.defaults.spatialBlend=0;
    ::ce::contracts::enforce_that(
        [&]() -> bool {
            return static_cast<bool>((playback.RegisterPreset(presetKey, preset)));
        },
        "playback.RegisterPreset(presetKey,preset)");
    settings=sound.ReadSettings(); settings.sourceKind=wave::SoundSourceKind::Preset;
    settings.soundPresetKey=presetKey.Text(); settings.overridePresetSettings=false;
    sound.ApplySettings(settings);
    ::ce::contracts::enforce_that(
        [&]() -> bool {
            return static_cast<bool>((!playback.IsAlive(loop) && playback.IsAlive(sound.CurrentPlayback())));
        },
        "!playback.IsAlive(loop) && playback.IsAlive(sound.CurrentPlayback())");
    ::ce::contracts::enforce_that(
        [&]() -> bool {
            return static_cast<bool>(
                (audio.voices.at(audio.serial).volume == 0.7f && audio.voices.at(audio.serial).spatialBlend == 0));
        },
        "audio.voices.at(audio.serial).volume==0.7f && audio.voices.at(audio.serial).spatialBlend==0");
    const auto presetShot=sound.PlayInstance(true);
    ::ce::contracts::enforce_that(
        [&]() -> bool {
            return static_cast<bool>((playback.IsAlive(presetShot) && !audio.voices.at(audio.serial).loop &&
                                      audio.voices.at(audio.serial).volume == 0.7f));
        },
        "playback.IsAlive(presetShot) && !audio.voices.at(audio.serial).loop && "
        "audio.voices.at(audio.serial).volume==0.7f");
    sound.Stop(); ::ce::contracts::enforce_that(
        [&]() -> bool {
            return static_cast<bool>((playback.AliveCount() == 0));
        },
        "playback.AliveCount()==0");

    settings=sound.ReadSettings(); settings.sourceKind=wave::SoundSourceKind::Clip;
    settings.ownerDestroyedPolicy=wave::OwnerDestroyedPolicy::Stop; settings.loop=false;
    sound.ApplySettings(settings); const auto detached=sound.PlayInstance();
    settings.ownerDestroyedPolicy=wave::OwnerDestroyedPolicy::DetachAndFinish;
    sound.ApplySettings(settings); ::ce::contracts::enforce_that(
        [&]() -> bool {
            return static_cast<bool>((sound.CurrentPlayback() == detached));
        },
        "sound.CurrentPlayback()==detached");
    sound.OnEndSimulation(); sound.OnRemovingFromScene(); sound.OnUninitializing();
    ::ce::contracts::enforce_that(
        [&]() -> bool {
            return static_cast<bool>((playback.IsAlive(detached)));
        },
        "playback.IsAlive(detached)"); scene.Sounds().EndWorld(); ::ce::contracts::enforce_that(
        [&]() -> bool {
            return static_cast<bool>((!playback.IsAlive(detached)));
        },
        "!playback.IsAlive(detached)");
    scene.Sounds().Activate();
    wave::PlaybackRequest attachedRequest; attachedRequest.source={wave::SoundSourceKind::Clip,audio.key};
    auto attached=playback.PlayAttached(scene.Sounds().WorldScope(),attachedRequest,entity.id,{});
    scene.Sounds().TrackAttached(entity,scene.Sounds().WorldScope(),attached);
    entity.transform.SetPending({2,3,4}); scene.Sounds().Update(0.1f);
    ::ce::contracts::enforce_that(
        [&]() -> bool {
            return static_cast<bool>((audio.voices.at(audio.serial).position.x == 2));
        },
        "audio.voices.at(audio.serial).position.x==2");
    entity.generation++; scene.Sounds().Update(0.1f); ::ce::contracts::enforce_that(
        [&]() -> bool {
            return static_cast<bool>((!playback.IsAlive(attached)));
        },
        "!playback.IsAlive(attached)");

    AudioListenerComponent listener; listener.owner=&entity; listener.id=99; listener.OnAddedToScene();
    entity.transform.SetPending({4,3,2});
    scene.Sounds().Update(0.1f); ::ce::contracts::enforce_that(
        [&]() -> bool {
            return static_cast<bool>((audio.listener.position.x == 4));
        },
        "audio.listener.position.x==4");
    listener.QueueSettings({false,{}}); scene.Sounds().Update(0.1f); ::ce::contracts::enforce_that(
        [&]() -> bool {
            return static_cast<bool>((audio.listener.position.x == 0));
        },
        "audio.listener.position.x==0");
    listener.OnRemovingFromScene(); scene.Sounds().EndWorld();
    ::ce::contracts::enforce_that(
        [&]() -> bool {
            return static_cast<bool>((playback.AliveCount() == 0));
        },
        "playback.AliveCount()==0");
    std::cout << "PASS: consumer real-source/stub-domain semantics: 2D, legacy GUID, live edits/rejection diagnostics, one-shot isolation, preview isolation/expiry, preset defaults, owner detach, scoped cleanup, stale entity, listener queue\n";
}
