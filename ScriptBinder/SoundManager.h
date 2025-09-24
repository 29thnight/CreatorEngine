#pragma once
#include "Core.Minimal.h"
#include "SoundDefinition.h"
#include <shared_mutex>

struct ChannelPair
{
    FMOD::Channel* ch2D{ nullptr };
    FMOD::Channel* ch3D{ nullptr };
};

class SoundComponent;
class SoundManager : public DLLCore::Singleton<SoundManager>
{
private:
    friend class DLLCore::Singleton<SoundManager>;
    SoundManager();
    ~SoundManager();

public:
    bool initialize(int maxChannels);
    void update();
    void shutdown();
    void Initialize();
    void SoundLoaderThread();
    void LoadSounds();

    // ===== 리스너 & 볼륨 =====
    void setListenerAttributes(const FMOD_VECTOR& pos,
        const FMOD_VECTOR& vel,
        const FMOD_VECTOR& forward,
        const FMOD_VECTOR& up);
    void setMasterVolume(float volume);
    void setBusVolume(ChannelType bus, float linear);
    void setBusVolumeDb(ChannelType bus, float db);
    void setBusVolumePercent(ChannelType bus, int percent);

    // ===== 리소스 =====
    bool loadSound(const std::string& name, const std::string& filePath,
        bool is3D = false, bool loop = false);
    void unloadSound(const std::string& name);

    // 등록된 클립 키 열람(검색용)
    std::vector<std::string> getAllClipKeys() const;

    // ===== 버스별 동시 재생 제한/스틸 정책 =====
    void setGroupMaxVoices(ChannelType bus, int maxVoices);
    void setGroupStealPolicy(ChannelType bus, StealPolicy p);
    void setGroupPreemptSameClip(ChannelType bus, bool on);

    // ===== SpatialBlend 듀얼채널 재생 =====
    ChannelPair playFromSourceBlended(const SoundComponent& src,
        void* ownerTag = nullptr);

    // ===== 원샷 풀링(per-clip 라운드로빈) =====
    void configureVoicePool(const std::string& clipKey, ChannelType bus, int capacity);
    ChannelPair playOneShotPooled(const std::string& clipKey, ChannelType bus,
        float volume, float pitch, int priority,
        float spatialBlend,
        const FMOD_VECTOR* pos = nullptr,
        const FMOD_VECTOR* vel = nullptr,
        void* ownerTag = nullptr);
    void clearVoicePool(const std::string& clipKey, ChannelType bus);

    // ===== 오너 태그로 정지(루프/잔존 채널 확실히 정지) =====
    void stopByOwnerTag(void* ownerTag);                  // 모든 버스
    void stopByOwnerTag(void* ownerTag, ChannelType bus); // 특정 버스

    bool getListenerPosition(FMOD_VECTOR& out) const;

private:
    // 내부 헬퍼
    static void computeEqualPowerGains(float blend01, float& w2D, float& w3D);
    ChannelPair playBlendedInternal(FMOD::Sound* sound, ChannelType bus,
        float volume, float pitch, int priority,
        float blend01, bool loop,
        const FMOD_VECTOR* pos, const FMOD_VECTOR* vel,
        void* ownerTag);
    FMOD::Channel* findStealCandidate(ChannelType bus, FMOD::Sound* newSound);
    int            countPlaying(ChannelType bus) const;
    void           pruneStopped(ChannelType bus);

    struct PooledVoice { FMOD::Channel* ch2D{ nullptr }; FMOD::Channel* ch3D{ nullptr }; };
    struct VoicePool {
        ChannelType bus{};
        int capacity{ 8 };
        int cursor{ 0 };
        std::vector<PooledVoice> slots;
    };
    static std::string poolKey(const std::string& clipKey, ChannelType bus);

private:
    // FMOD Core
    FMOD::System* system{ nullptr };
    FMOD::ChannelGroup* master{ nullptr };
    std::vector<FMOD::ChannelGroup*> groups; // size=(int)ChannelType::MaxChannel

    // 상태/설정
    int    _inputMaxChannels{ 256 };
    int    _softMaxChannels{ 0 };
    int    _sampleRate{ 0 };
    uint32_t _currSoundCount{ 0 };

    std::array<GroupConfig, (int)ChannelType::MaxChannel> groupCfg{};
    mutable std::shared_mutex _soundsMutex;
    std::unordered_map<std::string, FMOD::Sound*> sounds;
    std::unordered_map<std::string, VoicePool>    voicePools;

    // 로더 스레드
    std::thread      _soundLoaderThread;
    std::atomic_bool _isInitialized{ false };
    std::atomic_bool _isSoundLoaderThreadRunning{ true };
};

// 전역 핸들처럼 쓰고 있던 패턴을 유지하고 싶다면:
inline static auto Sound = SoundManager::GetInstance();