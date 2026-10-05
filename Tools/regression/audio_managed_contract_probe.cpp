// Test body for extracted production native audio bindings. The Python runner
// injects exact ABI structs/functions from ClrHost.cpp and stub scene objects.
int main()
{
    static_assert(sizeof(AudioAssetId) == 16u);
    static_assert(sizeof(AudioPlaySettings) == 60u);
    static_assert(sizeof(AudioParameter) == 32u);
    static_assert(sizeof(AudioPlaybackCompletion) == 32u);
    static_assert(offsetof(AudioPlaySettings, ownerPolicy) == 44u);
    static_assert(offsetof(AudioParameter, text) == 24u);
    static_assert(offsetof(AudioPlaySettings, concurrencyGroup) == 48u);
    static_assert(offsetof(AudioPlaySettings, allowVirtualization) == 56u);
    FakeAudio audio; wave::PlaybackService playback(audio);
    Scene scene; Entity entity; entity.scene = &scene; scene.entity = &entity;
    SoundComponent sound; sound.owner = &entity; sound.id = 11u; entity.sound = &sound;
    scene.Sounds().Bind(scene, &playback); scene.Sounds().Activate(); sound.OnAddedToScene();
    manager.playback = &playback; manager.scene = &scene;
    manager.session = playback.CreateScope(wave::ScopeKind::Session);
    ScriptObjectRegistry::Get().entity = &entity;
    assert(Api_Audio_Configure(1u, 12, 2, 0) == 1);
    assert(Api_Audio_Configure(6u, 12, 2, 0) == 0);
    assert(Api_Audio_Configure(2u, 0, 0, 1) == 1);
    assert(Api_Audio_Configure(0u, 12, 2, 1) == 0);
    assert(Api_Audio_Configure(65536u, 12, 2, 1) == 0);
    assert(Api_Audio_Configure(1u, 1025, 2, 0) == 0);
    assert(Api_Audio_SetReverbPreset(1) == 1 && Api_Audio_SetReverbPreset(3) == 0);
    AudioAssetId asset{}; const auto guid = Uuid::Parse(audio.key.Text());
    std::memcpy(&asset, guid.data.data(), sizeof(asset));
    assert(AudioKey(asset) == audio.key);
    const auto world = Api_Audio_GetScope(0);
    assert(world != 0u && Api_Audio_GetScope(1) != 0u && world != Api_Audio_GetScope(1));
    const auto handle = Api_Audio_Play(world, asset, 0, 0, {}, {}, nullptr, nullptr, 0);
    assert(handle != 0u && Api_Audio_State(handle) == 1);
    assert(audio.voices.at(audio.serial).spatialBlend == 0.0f);
    Api_Audio_Control(handle, 1, 0.0f, 0.0f); assert(Api_Audio_State(handle) == 2);
    Api_Audio_Control(handle, 2, 0.0f, 0.0f); assert(Api_Audio_State(handle) == 1);
    Api_Audio_Control(handle, 3, 0.4f, 1.2f);
    assert(audio.voices.at(audio.serial).volume == 0.4f);
    Api_Audio_Control(handle, 0, 0.0f, 0.0f); assert(Api_Audio_State(handle) == 0);
    AudioPlaybackCompletion completion{};
    assert(Api_Audio_TakeCompletion(&completion) == 1 && completion.playback == handle && completion.scope == world);
    assert(Api_Audio_TakeCompletion(&completion) == 0);

    wave::SoundGraphDefinition definition;
    definition.parameters = { {"enabled",wave::ParameterType::Boolean,false},
        {"variant",wave::ParameterType::Integer,std::int32_t{0}},
        {"gain",wave::ParameterType::Float,1.0f},
        {"surface",wave::ParameterType::String,std::string("default")} };
    wave::SoundNode clip; clip.id=1; clip.clip=audio.key;
    wave::SoundNode gain; gain.id=2; gain.kind=wave::SoundNodeKind::GainPitch; gain.inputs={1}; gain.gainParameter="gain";
    wave::SoundNode output; output.id=3; output.kind=wave::SoundNodeKind::Output; output.inputs={2};
    definition.nodes={clip,gain,output}; definition.output=3;
    std::string error; const auto graph=wave::CompileSoundGraph(definition,[&](const auto& key){return key==audio.key;},error);
    assert(graph);
    const auto graphGuid=Uuid::Parse("dd4949ac-22b2-42f1-ab2e-1775b5776611");
    const auto graphKey=wave::ClipKey::FromGuid(graphGuid); assert(playback.RegisterGraph(graphKey,graph));
    AudioAssetId graphAsset{}; std::memcpy(&graphAsset,graphGuid.data.data(),sizeof(graphAsset));
    AudioParameter parameters[]={{"enabled",0,1,0,nullptr},{"variant",1,2,0,nullptr},
        {"gain",2,0,0.2f,nullptr},{"surface",3,0,0,"wood"}};
    const auto graphHandle=Api_Audio_Play(world,graphAsset,2,0,{}, {}, nullptr,parameters,4);
    assert(graphHandle != 0u && audio.voices.at(audio.serial).volume==0.2f);
    assert(Api_Audio_SetParameter(graphHandle,"gain",2,0,0.6f,nullptr)==1);
    assert(audio.voices.at(audio.serial).volume==0.6f);
    assert(Api_Audio_SetParameter(graphHandle,"gain",0,1,0,nullptr)==0);
    assert(Api_Audio_Play(world,graphAsset,2,0,{}, {}, nullptr,parameters,65)==0u);
    const auto bad=AudioParameter{"gain",2,0,0.3f,nullptr};
    const AudioParameter duplicate[]={bad,bad};
    assert(Api_Audio_Play(world,graphAsset,2,0,{}, {}, nullptr,duplicate,2)==0u);
    AudioListenerComponent listener; listener.owner=&entity; listener.id=91; listener.OnAddedToScene();
    audio.rejectDistant=true;
    entity.transform.SetPending({100,0,0});
    const auto attached=Api_Audio_Play(world,asset,0,2,{}, {0,1}, nullptr,nullptr,0);
    assert(attached != 0u && audio.voices.at(audio.serial).position.x==100 && audio.listener.position.x==100); entity.destroyed=true; scene.Sounds().Update(0.1f);
    assert(Api_Audio_State(attached)==0);
    assert(Api_Audio_Play(world,asset,0,2,{}, {0,1}, nullptr,nullptr,0)==0u);
    scene.Sounds().EndWorld(); assert(Api_Audio_Play(world,asset,0,0,{}, {}, nullptr,nullptr,0)==0u);
    std::cout << "PASS: extracted native managed audio ABI: scoped identity, initial bool/int/float/string, live typed parameters, completions, stale scopes/owners\n";
}
