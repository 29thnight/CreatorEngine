#include <fmod.hpp>
#include <fmod_errors.h>
#include <iostream>
#include <unordered_map>
#include <string>
#include "Utility_Framework/Core.Definition.h"

enum class ChannelType
{
    BGM = 0,
    SFX,
    PLAYER,
    MONSTER,
    UI,
    MaxChannel
};

class SoundManager : public Singleton<SoundManager>
{
private:
    friend class Singleton;
public:
    bool initialize(int maxChannels);
    void update();
    void shutdown();
    bool loadSound(const std::string& name, const std::string& filePath, bool is3D = false, bool loop = false);
    void unloadSound(const std::string& name);
    void playSound(const std::string& name ,int channel, bool isPaused = false);
    void playSound(const std::string& name, ChannelType channel, bool isPaused = false);
    void stopSound(const int channel);
    void stopSound(const ChannelType channel);
    void stopAllSound();
    void setPaused(const int channel, bool paused);
    void setPaused(const ChannelType channel, bool paused);
    void setMasterVolume(float volume);
    void setVolume(const int channel, float volume);
    void setVolume(const ChannelType channel, float volume);
    void setPitch(const int channel, float pitch);
    void setPitch(const ChannelType channel, float pitch);
    void setMute(const int channel, bool mute);
    void setMute(const ChannelType channel, bool mute);
    void set3DAttributes(const int channel, const FMOD_VECTOR& position, const FMOD_VECTOR& velocity);
    void set3DAttributes(const ChannelType channel, const FMOD_VECTOR& position, const FMOD_VECTOR& velocity);


private:
    SoundManager();
    ~SoundManager();

	void Initialize();
	void SoundLoaderThread();
    void LoadSounds();

    FMOD::System* system;
    FMOD::ChannelGroup* _pMasterChannelGroup{ nullptr };
    std::vector<FMOD::ChannelGroup*>				_channelGroups;
    //std::vector<FadeSound>							_fadeSounds;
	int                                             _inputMaxChannels{ 0 };
    int												_maxChannels{ 0 };
    int												_pSamplateInt{ 0 };
	uint32 _currSoundCount{ 0 };

	std::thread _soundLoaderThread;
	std::atomic_bool _isInitialized{ false };
	std::atomic_bool _isSoundLoaderThreadRunning{ true };

    std::unordered_map<std::string, FMOD::Sound*> sounds;
    std::unordered_map<int, FMOD::Channel*> channels;
};

inline static auto Sound = SoundManager::GetInstance();