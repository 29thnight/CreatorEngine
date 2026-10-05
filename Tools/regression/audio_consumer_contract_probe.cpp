// Real consumer implementation, stubbed scene/entity/logger dependencies.
// This is a device-free domain probe, not a Windows/product integration test.
#include "SoundComponent.h"
#include "AudioListenerComponent.h"
#include "Scene.h"
#include <cassert>
#include <iostream>
#include <thread>
#include <unordered_map>

class FakeAudio final : public wave::AudioService
{
public:
    wave::ClipKey key{wave::ClipKey::FromGuid(Uuid::Parse("cc4949ac-22b2-42f1-ab2e-1775b5776611"))};
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
    std::vector<wave::ClipKey> ListClipKeys() const override { return {key}; }
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
    assert(playback.IsAlive(sound.CurrentPlayback()) && audio.listener.position.x==100);
    sound.Stop(); startupListener.OnRemovingFromScene(); audio.rejectDistant=false;
    auto settings=sound.ReadSettings(); settings.clipKey="legacy"; settings.loop=true; settings.spatial=false;
    sound.ApplySettings(settings);
    entity.transform.SetPending({10,0,0});
    const auto loop=sound.PlayInstance(); assert(playback.IsAlive(loop));
    assert(sound.ReadSettings().clipKey==audio.key.Text());
    assert(audio.voices.at(audio.serial).position.x==10);
    assert(audio.voices.at(audio.serial).spatialBlend==0 && audio.voices.at(audio.serial).loop);
    const auto loopVoice=audio.serial;
    const auto one=sound.PlayInstance(true); const auto shotVoice=audio.serial;
    assert(playback.AliveCount()==2 && playback.IsAlive(loop));
    assert(!audio.voices.at(shotVoice).loop && audio.voices.at(loopVoice).loop);
    sound.SetVolume(0.25f);
    assert(audio.voices.at(loopVoice).volume==0.25f && audio.voices.at(shotVoice).volume==0.25f);
    assert(audio.voices.at(loopVoice).loop && !audio.voices.at(shotVoice).loop);
    const auto diagnosticsBefore=Debug::messages.size();
    audio.rejectSpatialEdits=true;
    auto rejected=sound.ReadSettings(); rejected.spatial=true; rejected.spatialBlend=1.0f;
    sound.QueueSettings(rejected); scene.Sounds().Update(0.1f);
    assert(playback.IsAlive(loop) && audio.voices.at(loopVoice).spatialBlend==0.0f);
    assert(audio.voices.at(shotVoice).spatialBlend==0.0f);
    assert(Debug::messages.size()==diagnosticsBefore+1);
    assert(Debug::messages.back().find("[audio.settings.rejected]")!=std::string::npos);
    sound.QueueSettings(rejected); scene.Sounds().Update(0.1f);
    assert(Debug::messages.size()==diagnosticsBefore+1);
    rejected.spatial=false; sound.QueueSettings(rejected); scene.Sounds().Update(0.1f);
    audio.rejectSpatialEdits=false;
    sound.QueuePreview(SoundComponent::PreviewCommand::Play); scene.Sounds().Update(0.1f);
    assert(playback.AliveCount()==3);
    sound.QueuePreview(SoundComponent::PreviewCommand::Stop); scene.Sounds().Update(0.1f);
    assert(playback.AliveCount()==2 && playback.IsAlive(loop) && playback.IsAlive(one));
    sound.QueuePreview(SoundComponent::PreviewCommand::Play); scene.Sounds().Update(0.1f);
    assert(playback.AliveCount()==3);
    std::this_thread::sleep_for(std::chrono::milliseconds(2100));
    scene.Sounds().Update(0.0f, false);
    assert(playback.AliveCount()==2 && playback.IsAlive(loop));
    sound.Pause(true); assert(playback.State(loop)==wave::PlaybackState::Paused);
    sound.Pause(false); assert(playback.State(loop)==wave::PlaybackState::Playing);

    const auto presetKey=wave::ClipKey::FromGuid(Uuid::Parse("dd4949ac-22b2-42f1-ab2e-1775b5776611"));
    wave::SoundPreset preset; preset.source={wave::SoundSourceKind::Clip,audio.key};
    preset.defaults.volume=0.7f; preset.defaults.loop=true; preset.defaults.spatialBlend=0;
    assert(playback.RegisterPreset(presetKey,preset));
    settings=sound.ReadSettings(); settings.sourceKind=wave::SoundSourceKind::Preset;
    settings.soundPresetKey=presetKey.Text(); settings.overridePresetSettings=false;
    sound.ApplySettings(settings);
    assert(!playback.IsAlive(loop) && playback.IsAlive(sound.CurrentPlayback()));
    assert(audio.voices.at(audio.serial).volume==0.7f && audio.voices.at(audio.serial).spatialBlend==0);
    const auto presetShot=sound.PlayInstance(true);
    assert(playback.IsAlive(presetShot) && !audio.voices.at(audio.serial).loop && audio.voices.at(audio.serial).volume==0.7f);
    sound.Stop(); assert(playback.AliveCount()==0);

    settings=sound.ReadSettings(); settings.sourceKind=wave::SoundSourceKind::Clip;
    settings.ownerDestroyedPolicy=wave::OwnerDestroyedPolicy::Stop; settings.loop=false;
    sound.ApplySettings(settings); const auto detached=sound.PlayInstance();
    settings.ownerDestroyedPolicy=wave::OwnerDestroyedPolicy::DetachAndFinish;
    sound.ApplySettings(settings); assert(sound.CurrentPlayback()==detached);
    sound.OnEndSimulation(); sound.OnRemovingFromScene(); sound.OnUninitializing();
    assert(playback.IsAlive(detached)); scene.Sounds().EndWorld(); assert(!playback.IsAlive(detached));
    scene.Sounds().Activate();
    wave::PlaybackRequest attachedRequest; attachedRequest.source={wave::SoundSourceKind::Clip,audio.key};
    auto attached=playback.PlayAttached(scene.Sounds().WorldScope(),attachedRequest,entity.id,{});
    scene.Sounds().TrackAttached(entity,scene.Sounds().WorldScope(),attached);
    entity.transform.SetPending({2,3,4}); scene.Sounds().Update(0.1f);
    assert(audio.voices.at(audio.serial).position.x==2);
    entity.generation++; scene.Sounds().Update(0.1f); assert(!playback.IsAlive(attached));

    AudioListenerComponent listener; listener.owner=&entity; listener.id=99; listener.OnAddedToScene();
    entity.transform.SetPending({4,3,2});
    scene.Sounds().Update(0.1f); assert(audio.listener.position.x==4);
    listener.QueueSettings({false,{}}); scene.Sounds().Update(0.1f); assert(audio.listener.position.x==0);
    listener.OnRemovingFromScene(); scene.Sounds().EndWorld();
    assert(playback.AliveCount()==0);
    std::cout << "PASS: consumer real-source/stub-domain semantics: 2D, legacy GUID, live edits/rejection diagnostics, one-shot isolation, preview isolation/expiry, preset defaults, owner detach, scoped cleanup, stale entity, listener queue\n";
}
