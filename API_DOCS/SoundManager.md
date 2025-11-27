# SoundManager

**Header:** `ScriptBinder/SoundManager.h`

**Inheritance:** `: public DLLCore::Singleton<SoundManager>`

> 자동 생성된 문서입니다. 실제 사용 시 구현 파일을 함께 참고하세요.

## Public Methods
- `bool initialize(int maxChannels);`
- `void update();`
- `void shutdown();`
- `void Initialize();`
- `void SoundLoaderThread();`
- `void LoadSounds();`
- `void setMasterVolume(float volume);`
- `void setBusVolume(ChannelType bus, float linear);`
- `void setBusVolumeDb(ChannelType bus, float db);`
- `void setBusVolumePercent(ChannelType bus, int percent);`
- `void unloadSound(const std::string& name);`
- `std::vector<std::string> getAllClipKeys() const;`
- `void setGroupMaxVoices(ChannelType bus, int maxVoices);`
- `void setGroupStealPolicy(ChannelType bus, StealPolicy p);`
- `void setGroupPreemptSameClip(ChannelType bus, bool on);`
- `void configureVoicePool(const std::string& clipKey, ChannelType bus, int capacity);`
- `void clearVoicePool(const std::string& clipKey, ChannelType bus);`
- `void stopByOwnerTag(void* ownerTag);`
- `void stopByOwnerTag(void* ownerTag, ChannelType bus);`
- `bool getListenerPosition(FMOD_VECTOR& out) const;`

## Public Properties
- `const FMOD_VECTOR& up);`
- `bool is3D = false, bool loop = false);`
- `void* ownerTag = nullptr);`
- `void* ownerTag = nullptr);`
