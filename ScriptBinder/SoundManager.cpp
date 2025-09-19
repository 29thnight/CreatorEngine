#include "SoundManager.h"
#include "PathFinder.h"
#include "Core.Minimal.h"

static inline float dbToLinear(float db) { return powf(10.0f, db / 20.0f); }
static inline float linearToDb(float lin) { return 20.0f * log10f(std::max(lin, 1e-6f)); }

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

        FMOD::Channel* ch = channels[(int)ChannelType::BGM];
        if (ch) {
            bool playing = false, paused = false; float aud = 0.f;
            ch->isPlaying(&playing);
            ch->getPaused(&paused);
            ch->getAudibility(&aud); // 0이면 가상화/스틸 의심

            FMOD::Sound* snd = nullptr;
            ch->getCurrentSound(&snd);
            if (snd) {
                FMOD_OPENSTATE st = FMOD_OPENSTATE_READY;
                unsigned int percent = 0;
                bool starving = false, diskbusy = false;
                snd->getOpenState(&st, &percent, &starving, &diskbusy);

                unsigned int posMs = 0;
                ch->getPosition(&posMs, FMOD_TIMEUNIT_MS);
                constexpr float kAudEps = 1e-3f; // 0.001 이하를 사실상 0으로 간주
                if (starving || false == playing || aud <= kAudEps)
                {
                    // playing=false || aud0 → 스틸/가상화
                    // starving=true → 스트림 버퍼/IO 문제
                    __debugbreak();
                }
            }
        }
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

void SoundManager::setBusVolume(ChannelType bus, float linear /*0~1 권장*/) {
    _channelGroups[(int)bus]->setVolume(linear);
}

void SoundManager::setBusVolumeDb(ChannelType bus, float db) {
    _channelGroups[(int)bus]->setVolume(dbToLinear(db));
}

// UI 슬라이더(0~100) → dB 매핑(예: -60dB ~ 0dB)
static inline float sliderToDb(int percent) {
    const float minDb = -60.0f;
    if (percent <= 0) return -80.0f; // 사실상 mute
    return minDb + (0.0f - minDb) * (percent / 100.0f);
}

void SoundManager::setBusVolumePercent(ChannelType bus, int percent) {
    setBusVolumeDb(bus, sliderToDb(percent));
}

bool SoundManager::loadSound(const std::string& name, const std::string& filePath, bool is3D, bool loop)
{
    if (sounds.find(name) != sounds.end()) { Debug->LogError("Sound already loaded: " + name); return false; }

    FMOD_MODE mode = FMOD_DEFAULT;
    if (is3D) mode |= FMOD_3D; else mode |= FMOD_2D;

    // BGM 폴더면 스트리밍 + 루프 권장
    const bool isBGM = filePath.find("\\Sounds\\BGM\\") != std::string::npos
        || filePath.find("/Sounds/BGM/") != std::string::npos;

    if (isBGM) mode |= FMOD_CREATESTREAM;
    if (loop)  mode |= FMOD_LOOP_NORMAL;

    FMOD::Sound* sound = nullptr;
    FMOD_RESULT r = system->createSound(filePath.c_str(), mode, nullptr, &sound);
    if (r != FMOD_OK)
    {
        Debug->LogError("Failed to load sound: " + filePath + " - " + FMOD_ErrorString(r));
        return false;
    }

    if (isBGM) {
        float freq = 0.0f;
        int   prio = 128; // default

        FMOD_RESULT r = sound->getDefaults(&freq, &prio);  // 현재 주파수/우선순위 읽기
        if (r == FMOD_OK) {
            r = sound->setDefaults(freq, /*priority=*/0);  // 0 = 최상위 우선순위
        }
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
        Debug->LogError("Sound not found: " + name);
        return;
    }

    FMOD::Channel* pChannel = nullptr;
    FMOD_RESULT result = system->playSound(it->second, _channelGroups[channel], isPaused, &pChannel);
    if (result != FMOD_OK)
    {
        Debug->LogError("Failed to play sound: " + name + " - " + FMOD_ErrorString(result));
    }
    else
    {
        channels[channel] = pChannel;
    }
}

void SoundManager::playOneShot(const std::string& name, ChannelType ch, float volume, float pitch)
{
    auto it = sounds.find(name);
    if (it == sounds.end()) { Debug->LogError("Sound not found: " + name); return; }

    FMOD::Channel* c = nullptr;
    FMOD_RESULT r = system->playSound(it->second, _channelGroups[(int)ch], false, &c);
    if (r != FMOD_OK) {
        Debug->LogError("Failed to play oneshot: " + name + " - " + std::string(FMOD_ErrorString(r)));
        return;
    }
    c->setMode(FMOD_LOOP_OFF);   // 원샷 보장
    c->setVolume(volume);
    c->setPitch(pitch);
}

void SoundManager::playBGM(const std::string& name, bool paused)
{
    auto it = sounds.find(name);
    if (it == sounds.end()) { Debug->LogError("BGM not found: " + name); return; }

    FMOD::Channel* ch = nullptr;
    auto* group = _channelGroups[(int)ChannelType::BGM]; // 네가 쓰는 BGM 그룹 인덱스
    FMOD_RESULT r = system->playSound(it->second, group, paused, &ch);
    if (r != FMOD_OK) {
        Debug->LogError("Failed to play BGM: " + name + " - " + std::string(FMOD_ErrorString(r)));
        return;
    }

    ch->setMode(FMOD_LOOP_NORMAL);
    ch->setPriority(0);       // 최상위
    ch->setVolume(1.0f);        // 그룹 볼륨으로 전체 레벨 제어
    channels[(int)ChannelType::BGM] = ch;
}

void SoundManager::playSound(const std::string& name, ChannelType channel, bool isPaused)
{
    auto it = sounds.find(name);
    if (it == sounds.end())
    {
        Debug->LogError("Sound not found: " + name);
        return;
    }

    FMOD::Channel* pChannel = nullptr;
    FMOD_RESULT result = system->playSound(it->second, _channelGroups[(int)channel], isPaused, &pChannel);
    if (result != FMOD_OK)
    {
        Debug->LogError("Failed to play sound: " + name + " - " + FMOD_ErrorString(result));
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
        Debug->LogError("FMOD System creation failed: " + std::string(FMOD_ErrorString(result)));
    }

    result = system->init(_inputMaxChannels, FMOD_INIT_NORMAL, nullptr); // FMOD 시스템 초기화
    if (result != FMOD_OK)
    {
        Debug->LogError("FMOD System initialization failed: " + std::string(FMOD_ErrorString(result)));
    }

    system->getSoftwareChannels(&numberOfAvailableChannels); // 사용 가능한 채널 수를 가져온다.

    if (numberOfAvailableChannels < _inputMaxChannels) // 사용 가능한 채널 수가 설정한 채널 수보다 작다면
    {
        _inputMaxChannels = numberOfAvailableChannels; // 사용 가능한 채널 수로 설정한다.
    }

    // 스트림 버퍼(언더런 방지) 증가 권장
    system->setStreamBufferSize(128 * 1024, FMOD_TIMEUNIT_RAWBYTES);

    // 볼륨 0 가상화 임계치 내리기(필요시)
    FMOD_ADVANCEDSETTINGS adv{}; adv.cbSize = sizeof(adv);
    adv.vol0virtualvol = 0.0f; // 0 근처 볼륨도 진짜로 믹스하게
    system->setAdvancedSettings(&adv);

    _maxChannels = _inputMaxChannels;
    _channelGroups.resize(_maxChannels);
    system->getMasterChannelGroup(&_pMasterChannelGroup);
    system->getSoftwareFormat(&_pSamplateInt, 0, 0);
    setMasterVolume(2.f);
    //for (int i = 0; i < _maxChannels; i++)
    //{
    //    system->createChannelGroup(0, &_channelGroups[i]);
    //    _pMasterChannelGroup->addGroup(_channelGroups[i]);
    //}
    system->createChannelGroup("BGM", &_channelGroups[(int)ChannelType::BGM]);
    system->createChannelGroup("SFX", &_channelGroups[(int)ChannelType::SFX]);
    system->createChannelGroup("Player", &_channelGroups[(int)ChannelType::PLAYER]);
    system->createChannelGroup("Monster", &_channelGroups[(int)ChannelType::MONSTER]);
    system->createChannelGroup("UI", &_channelGroups[(int)ChannelType::UI]);

    for (int i = 0; i < _maxChannels; i++)
    {
        _pMasterChannelGroup->addGroup(_channelGroups[i]);
    }

    setBusVolumeDb(ChannelType::BGM, -5.0f);
    setBusVolumeDb(ChannelType::SFX, -3.0f);
    setBusVolumeDb(ChannelType::PLAYER, -3.0f);
    setBusVolumeDb(ChannelType::MONSTER, -3.0f);
    setBusVolumeDb(ChannelType::UI, -8.0f);

    channels.reserve(_maxChannels);

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
            Debug->LogWarning("Could not load sounds" + std::string(e.what()));
        }
        catch (const std::exception& e)
        {
            Debug->LogWarning("Error" + std::string(e.what()));
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
            bool loop = (dir.path().parent_path() == PathFinder::Relative("Sounds\\BGM"));
            loadSound(key, dir.path().string(), false, loop);
            Debug->Log("Loaded Sound : " + key);
        }
    }
    _isSoundLoaderThreadRunning = false;
}
