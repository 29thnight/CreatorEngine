#include "SoundManager.h"
#include "Utility_Framework/PathFinder.h"
#include "Utility_Framework/Logger.h"

bool SoundManager::initialize(int maxChannels)
{
    _inputMaxChannels = maxChannels;
    Initialize();

    _soundLoaderThread = std::thread(&SoundManager::SoundLoaderThread, this);
    _soundLoaderThread.detach();

    return true;
}

void SoundManager::update()
{
    if (system)
    {
        system->update();
    }
}

void SoundManager::shutdown()
{
    for (auto& [key, sound] : sounds)
    {
        sound->release();
    }
    sounds.clear();

    for (auto& [key, channel] : channels)
    {
        channel->stop();
    }
    channels.clear();

    for (auto& channelGroup : _channelGroups)
    {
        channelGroup->release();
    }

    if (_pMasterChannelGroup)
    {
        _pMasterChannelGroup->release();
    }

    if (system)
    {
        system->close();
        system->release();
        system = nullptr;
    }
}

bool SoundManager::loadSound(const std::string& name, const std::string& filePath, bool is3D, bool loop)
{
    if (sounds.find(name) != sounds.end())
    {
        Log::Error("Sound already loaded: " + name);
        return false;
    }

    FMOD_MODE mode = FMOD_DEFAULT;
    if (is3D) mode |= FMOD_3D;
    if (loop) mode |= FMOD_LOOP_NORMAL;

    FMOD::Sound* sound = nullptr;
    FMOD_RESULT result = system->createSound(filePath.c_str(), mode, nullptr, &sound);
    if (result != FMOD_OK) 
    {
		Log::Error("Failed to load sound: " + filePath + " - " + FMOD_ErrorString(result));
        return false;
    }

    sounds[name] = sound;
    return true;
}

void SoundManager::unloadSound(const std::string& name)
{
    auto it = sounds.find(name);
    if (it != sounds.end()) 
    {
        it->second->release();
        sounds.erase(it);
    }
}

void SoundManager::playSound(const std::string& name, int channel, bool isPaused)
{
    auto it = sounds.find(name);
    if (it == sounds.end()) 
    {
		Log::Error("Sound not found: " + name);
        return;
    }

    FMOD::Channel* pChannel = nullptr;
    FMOD_RESULT result = system->playSound(it->second, _channelGroups[channel], isPaused, &pChannel);
    if (result != FMOD_OK) 
    {
		Log::Error("Failed to play sound: " + name + " - " + FMOD_ErrorString(result));
    }
    else 
    {
        channels[channel] = pChannel;
    }
}

void SoundManager::playSound(const std::string& name, ChannelType channel, bool isPaused)
{
    auto it = sounds.find(name);
    if (it == sounds.end())
    {
        Log::Error("Sound not found: " + name);
        return;
    }

    FMOD::Channel* pChannel = nullptr;
    FMOD_RESULT result = system->playSound(it->second, _channelGroups[(int)channel], isPaused, &pChannel);
    if (result != FMOD_OK)
    {
        Log::Error("Failed to play sound: " + name + " - " + FMOD_ErrorString(result));
    }
    else
    {
        channels[(int)channel] = pChannel;
    }
}

void SoundManager::stopSound(const int channel)
{
    auto it = channels.find(channel);
    if (it != channels.end() && it->second) 
    {
        it->second->stop();
        channels.erase(it);
    }
}

void SoundManager::stopSound(const ChannelType channel)
{
    auto it = channels.find((int)channel);
    if (it != channels.end() && it->second)
    {
        it->second->stop();
        channels.erase(it);
    }
}

void SoundManager::stopAllSound()
{
    for (auto& [key, channel] : channels) 
    {
        channel->stop();
    }
    channels.clear();
}

void SoundManager::setPaused(const int channel, bool paused)
{
    auto it = channels.find(channel);
    if (it != channels.end() && it->second) 
    {
        it->second->setPaused(paused);
    }
}

void SoundManager::setPaused(const ChannelType channel, bool paused)
{
    auto it = channels.find((int)channel);
    if (it != channels.end() && it->second)
    {
        it->second->setPaused(paused);
    }
}

void SoundManager::setMasterVolume(float volume)
{
    if (_pMasterChannelGroup) 
    {
        _pMasterChannelGroup->setVolume(volume);
    }
}

void SoundManager::setVolume(const int channel, float volume)
{
    auto it = channels.find(channel);
    if (it != channels.end() && it->second) 
    {
        it->second->setVolume(volume);
    }
}

void SoundManager::setVolume(const ChannelType channel, float volume)
{
    auto it = channels.find((int)channel);
    if (it != channels.end() && it->second)
    {
        it->second->setVolume(volume);
    }
}

void SoundManager::setPitch(const int channel, float pitch)
{
    auto it = channels.find(channel);
    if (it != channels.end() && it->second) 
    {
        it->second->setPitch(pitch);
    }
}

void SoundManager::setPitch(const ChannelType channel, float pitch)
{
    auto it = channels.find((int)channel);
    if (it != channels.end() && it->second)
    {
        it->second->setPitch(pitch);
    }
}

void SoundManager::setMute(const int channel, bool mute)
{
    auto it = channels.find(channel);
    if (it != channels.end() && it->second) 
    {
        it->second->setMute(mute);
    }
}

void SoundManager::setMute(const ChannelType channel, bool mute)
{
    auto it = channels.find((int)channel);
    if (it != channels.end() && it->second)
    {
        it->second->setMute(mute);
    }
}

void SoundManager::set3DAttributes(const int channel, const FMOD_VECTOR& position, const FMOD_VECTOR& velocity)
{
    auto it = channels.find(channel);
    if (it != channels.end() && it->second) 
    {
        it->second->set3DAttributes(&position, &velocity);
    }
}

void SoundManager::set3DAttributes(const ChannelType channel, const FMOD_VECTOR& position, const FMOD_VECTOR& velocity)
{
    auto it = channels.find((int)channel);
    if (it != channels.end() && it->second)
    {
        it->second->set3DAttributes(&position, &velocity);
    }
}

SoundManager::SoundManager()
{
}

SoundManager::~SoundManager()
{
    while (_isSoundLoaderThreadRunning)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    };

	shutdown();
}

void SoundManager::Initialize()
{
    int numberOfAvailableChannels{};

    FMOD_RESULT result = FMOD::System_Create(&system);
    if (result != FMOD_OK)
    {
		Log::Error("FMOD System creation failed: " + std::string(FMOD_ErrorString(result)));
    }
    system->getSoftwareChannels(&numberOfAvailableChannels); // 사용 가능한 채널 수를 가져온다.

    if (numberOfAvailableChannels < _inputMaxChannels) // 사용 가능한 채널 수가 설정한 채널 수보다 작다면
    {
        _inputMaxChannels = numberOfAvailableChannels; // 사용 가능한 채널 수로 설정한다.
    }

    result = system->init(_inputMaxChannels, FMOD_INIT_NORMAL, nullptr); // FMOD 시스템 초기화
    if (result != FMOD_OK)
    {
		Log::Error("FMOD System initialization failed: " + std::string(FMOD_ErrorString(result)));
    }

    _maxChannels = _inputMaxChannels;
    _channelGroups.resize(_maxChannels);
    system->getMasterChannelGroup(&_pMasterChannelGroup);
    system->getSoftwareFormat(&_pSamplateInt, 0, 0);
    for (int i = 0; i < _maxChannels; i++)
    {
        system->createChannelGroup(0, &_channelGroups[i]);
        _pMasterChannelGroup->addGroup(_channelGroups[i]);
    }
    channels.reserve(_maxChannels);

    setMasterVolume(30.f);
    _isInitialized = true;
}

void SoundManager::SoundLoaderThread()
{
    while (true)
    {
        uint32 soundCount = 0;

        try
        {
            file::path soundPath = PathFinder::Relative("Sounds\\");
            for (auto& dir : file::recursive_directory_iterator(soundPath))
            {
                if (dir.is_directory())
                    continue;

                if (dir.path().extension() == ".mp3" || 
                    dir.path().extension() == ".wav" || 
                    dir.path().extension() == ".ogg"
                    )
                {
                    soundCount++;
                }
            }
        }
        catch (const file::filesystem_error& e)
        {
            Log::Warning("Could not load sounds" + std::string(e.what()));
        }
        catch (const std::exception& e)
        {
            Log::Warning("Error" + std::string(e.what()));
        }

        if (_currSoundCount != soundCount)
        {
            LoadSounds();
            _currSoundCount = soundCount;
        }

		std::this_thread::sleep_for(std::chrono::seconds(1));
    }
}

void SoundManager::LoadSounds()
{
    _isSoundLoaderThreadRunning = true;
    file::path soundPath = PathFinder::Relative("Sounds\\");
    for (auto& dir : file::recursive_directory_iterator(soundPath))
    {
        if (dir.is_directory())
            continue;
        if (dir.path().extension() == ".mp3" || dir.path().extension() == ".wav" || dir.path().extension() == ".ogg")
        {
            std::string key = dir.path().filename().string();
            key = key.substr(0, key.find_last_of('.'));
            bool loop  = (dir.path().parent_path() == PathFinder::Relative("Sounds\\BGM"));
            loadSound(key, dir.path().string(),false, loop);
			Log::Info("Loaded Sound : " + key);
        }
    }
	_isSoundLoaderThreadRunning = false;
}
