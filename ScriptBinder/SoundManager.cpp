#include "SoundManager.h"
#include "PathFinder.h"
#include "SoundComponent.h"
#include "Core.Minimal.h"

namespace fs = std::filesystem;

// ===== ctor/dtor =====
SoundManager::SoundManager() {}
SoundManager::~SoundManager() {
    while (_isSoundLoaderThreadRunning) std::this_thread::sleep_for(std::chrono::milliseconds(10));
    shutdown();
}

// ===== 레거시 표면 =====
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
    if (system) system->update();
}

void SoundManager::shutdown()
{
    {
        std::unique_lock wlock(_soundsMutex);
        for (auto& [k, s] : sounds) if (s) s->release();
        sounds.clear();
    }

    for (auto* g : groups) if (g) g->release();
    groups.clear();

    // master는 System 소유
    master = nullptr;

    if (system) { system->close(); system->release(); system = nullptr; }
}

void SoundManager::Initialize()
{
    FMOD_RESULT r = FMOD::System_Create(&system);
    if (r != FMOD_OK) Debug->LogError(std::string("FMOD create failed: ") + FMOD_ErrorString(r));

    r = system->init(_inputMaxChannels, FMOD_INIT_NORMAL, nullptr);
    if (r != FMOD_OK) Debug->LogError(std::string("FMOD init failed: ") + FMOD_ErrorString(r));

    int avail = 0; system->getSoftwareChannels(&avail);
    _softMaxChannels = std::min(_inputMaxChannels, avail);

    // 스트림 안정성
    system->setStreamBufferSize(128 * 1024, FMOD_TIMEUNIT_RAWBYTES);

    // vol0virtual 보정
    FMOD_ADVANCEDSETTINGS adv{}; adv.cbSize = sizeof(adv);
    adv.vol0virtualvol = 0.0f;
    system->setAdvancedSettings(&adv);

    system->set3DSettings(1.0f, 1.0f, 1.0f);

    groups.assign((int)ChannelType::MaxChannel, nullptr);
    system->getMasterChannelGroup(&master);
    system->getSoftwareFormat(&_sampleRate, 0, 0);

    system->createChannelGroup("BGM", &groups[(int)ChannelType::BGM]);
    system->createChannelGroup("SFX", &groups[(int)ChannelType::SFX]);
    system->createChannelGroup("Player", &groups[(int)ChannelType::PLAYER]);
    system->createChannelGroup("Monster", &groups[(int)ChannelType::MONSTER]);
    system->createChannelGroup("UI", &groups[(int)ChannelType::UI]);

    for (auto* g : groups) {
        if (g && master) {
            FMOD_RESULT rr = master->addGroup(g);
            if (rr != FMOD_OK) Debug->LogError(std::string("addGroup failed: ") + FMOD_ErrorString(rr));
        }
    }

    // 기본 볼륨
    setMasterVolume(1.0f);
    setBusVolumeDb(ChannelType::BGM, -5.0f);
    setBusVolumeDb(ChannelType::SFX, -3.0f);
    setBusVolumeDb(ChannelType::PLAYER, -3.0f);
    setBusVolumeDb(ChannelType::MONSTER, -3.0f);
    setBusVolumeDb(ChannelType::UI, -8.0f);

    // 기본 정책
    groupCfg[(int)ChannelType::SFX] = { 12, StealPolicy::Quietest, true };

    _isInitialized = true;
}

void SoundManager::SoundLoaderThread()
{
    while (true) {
        uint32_t cnt = 0;
        try {
            fs::path root = PathFinder::Relative("Sounds\\");
            for (auto& e : fs::recursive_directory_iterator(root)) {
                if (!e.is_directory()) {
                    auto ext = e.path().extension().string();
                    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
                    if (ext == ".mp3" || ext == ".wav" || ext == ".ogg") cnt++;
                }
            }
        }
        catch (...) {}

        if (_currSoundCount != cnt) {
            LoadSounds();
            _currSoundCount = cnt;
        }
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
}

void SoundManager::LoadSounds()
{
    _isSoundLoaderThreadRunning = true;
    try {
        fs::path root = PathFinder::Relative("Sounds\\");
        for (auto& e : fs::recursive_directory_iterator(root)) {
            if (e.is_directory()) continue;
            auto ext = e.path().extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
            if (ext == ".mp3" || ext == ".wav" || ext == ".ogg") {
                std::string key = e.path().filename().string();
                key = key.substr(0, key.find_last_of('.'));
                bool loop = (e.path().parent_path() == PathFinder::Relative("Sounds\\BGM"));
                loadSound(key, e.path().string(), /*is3D=*/false, loop);
            }
        }
    }
    catch (...) {}
    _isSoundLoaderThreadRunning = false;
}

// ===== 리스너 & 볼륨 =====
void SoundManager::setListenerAttributes(const FMOD_VECTOR& pos,
    const FMOD_VECTOR& vel,
    const FMOD_VECTOR& forward,
    const FMOD_VECTOR& up)
{
    if (system) system->set3DListenerAttributes(0, &pos, &vel, &forward, &up);
}
void SoundManager::setMasterVolume(float v) { if (master) master->setVolume(v); }
void SoundManager::setBusVolume(ChannelType bus, float lin) { if (groups[(int)bus]) groups[(int)bus]->setVolume(lin); }
void SoundManager::setBusVolumeDb(ChannelType bus, float db) { setBusVolume(bus, dbToLinear(db)); }
void SoundManager::setBusVolumePercent(ChannelType bus, int percent) { setBusVolumeDb(bus, sliderToDb(percent)); }

// ===== 리소스 =====
bool SoundManager::loadSound(const std::string& name, const std::string& filePath, bool is3D, bool loop)
{
    {
        std::shared_lock rlock(_soundsMutex);
        if (sounds.count(name)) return true;
    }

    FMOD_MODE mode = FMOD_DEFAULT | (is3D ? FMOD_3D : FMOD_2D);
    const bool isBGM = filePath.find("\\Sounds\\BGM\\") != std::string::npos
        || filePath.find("/Sounds/BGM/") != std::string::npos;
    if (isBGM) mode |= FMOD_CREATESTREAM;
    if (loop)  mode |= FMOD_LOOP_NORMAL;

    FMOD::Sound* s = nullptr;
    FMOD_RESULT r = system->createSound(filePath.c_str(), mode, nullptr, &s);
    if (r != FMOD_OK) {
        Debug->LogError("Failed to load sound: " + filePath + " - " + std::string(FMOD_ErrorString(r)));
        return false;
    }
    if (isBGM) {
        float freq = 0; int pr = 128;
        if (s->getDefaults(&freq, &pr) == FMOD_OK) s->setDefaults(freq, 0);
    }

    {
        std::unique_lock wlock(_soundsMutex);
        sounds[name] = s;
    }
    return true;
}

void SoundManager::unloadSound(const std::string& name)
{
    std::unique_lock wlock(_soundsMutex);
    auto it = sounds.find(name);
    if (it != sounds.end()) { if (it->second) it->second->release(); sounds.erase(it); }
}

std::vector<std::string> SoundManager::getAllClipKeys() const
{
    std::shared_lock rlock(_soundsMutex);
    std::vector<std::string> out; out.reserve(sounds.size());
    for (auto& kv : sounds) out.push_back(kv.first);
    std::sort(out.begin(), out.end());
    return out;
}

// ===== 정책 =====
void SoundManager::setGroupMaxVoices(ChannelType bus, int maxVoices) { groupCfg[(int)bus].maxVoices = std::max(0, maxVoices); }
void SoundManager::setGroupStealPolicy(ChannelType bus, StealPolicy p) { groupCfg[(int)bus].policy = p; }
void SoundManager::setGroupPreemptSameClip(ChannelType bus, bool on) { groupCfg[(int)bus].preemptSameClip = on; }

// ===== SpatialBlend =====
void SoundManager::computeEqualPowerGains(float t, float& w2D, float& w3D)
{
    t = std::clamp(t, 0.0f, 1.0f);
    w2D = cosf(t * (3.14159265f * 0.5f));
    w3D = sinf(t * (3.14159265f * 0.5f));
}

ChannelPair
SoundManager::playBlendedInternal(FMOD::Sound* sound, ChannelType bus,
    float volume, float pitch, int priority,
    float blend01, bool loop,
    const FMOD_VECTOR* pos, const FMOD_VECTOR* vel,
    void* ownerTag)
{
    ChannelPair out{};
    if (!sound) return out;
    auto* group = groups[(int)bus];
    if (!group) return out;

    pruneStopped(bus);
    const auto& cfg = groupCfg[(int)bus];
    int playing = countPlaying(bus);
    const int limit = cfg.maxVoices;

    float w2D = 1.f, w3D = 0.f;
    computeEqualPowerGains(blend01, w2D, w3D);
    const bool want2D = (w2D > 1e-3f);
    const bool want3D = (w3D > 1e-3f);
    int need = (int)want2D + (int)want3D;

    // 부족하면 스틸 → 그래도 부족하면 1채널로 degrade (3D 우선)
    if (limit > 0 && playing + need > limit) {
        int deficit = (playing + need) - limit;
        while (deficit-- > 0) {
            if (FMOD::Channel* v = findStealCandidate(bus, sound)) v->stop();
            else break;
        }
        playing = countPlaying(bus);
        if (limit > 0 && playing + need > limit) {
            if (want3D) {
                FMOD::Channel* c = nullptr;
                if (system->playSound(sound, group, true, &c) == FMOD_OK && c) {
                    c->setMode(FMOD_DEFAULT | FMOD_3D | (loop ? FMOD_LOOP_NORMAL : FMOD_LOOP_OFF));
                    if (pos) c->set3DAttributes(pos, vel);
                    c->set3DMinMaxDistance(1.f, 50.f);
                    c->setPriority(priority);
                    c->setPitch(pitch);
                    c->setVolume(volume * w3D);
                    if (ownerTag) c->setUserData(ownerTag);
                    c->setPaused(false);
                    out.ch3D = c;
                }
                return out;
            }
            else {
                FMOD::Channel* c = nullptr;
                if (system->playSound(sound, group, true, &c) == FMOD_OK && c) {
                    c->setMode(FMOD_DEFAULT | FMOD_2D | (loop ? FMOD_LOOP_NORMAL : FMOD_LOOP_OFF));
                    c->setPriority(priority);
                    c->setPitch(pitch);
                    c->setVolume(volume * w2D);
                    if (ownerTag) c->setUserData(ownerTag);
                    c->setPaused(false);
                    out.ch2D = c;
                }
                return out;
            }
        }
    }

    auto create2D = [&](FMOD::Channel** ch) {
        FMOD::Channel* c = nullptr;
        if (system->playSound(sound, group, true, &c) == FMOD_OK && c) {
            c->setMode(FMOD_DEFAULT | FMOD_2D | (loop ? FMOD_LOOP_NORMAL : FMOD_LOOP_OFF));
            c->setPriority(priority);
            c->setPitch(pitch);
            c->setVolume(volume * w2D);
            if (ownerTag) c->setUserData(ownerTag);
            c->setPaused(false);
            *ch = c;
        }
        };
    auto create3D = [&](FMOD::Channel** ch) {
        FMOD::Channel* c = nullptr;
        if (system->playSound(sound, group, true, &c) == FMOD_OK && c) {
            c->setMode(FMOD_DEFAULT | FMOD_3D | (loop ? FMOD_LOOP_NORMAL : FMOD_LOOP_OFF));
            if (pos) c->set3DAttributes(pos, vel);
            c->set3DMinMaxDistance(1.f, 50.f);
            c->setPriority(priority);
            c->setPitch(pitch);
            c->setVolume(volume * w3D);
            if (ownerTag) c->setUserData(ownerTag);
            c->setPaused(false);
            *ch = c;
        }
        };

    if (want2D) create2D(&out.ch2D);
    if (want3D) create3D(&out.ch3D);
    return out;
}

ChannelPair
SoundManager::playFromSourceBlended(const SoundComponent& src, void* ownerTag)
{
    FMOD::Sound* snd = nullptr;
    {
        std::shared_lock rlock(_soundsMutex);
        auto it = sounds.find(src.clipKey);
        if (it == sounds.end()) { Debug->LogError("Sound not found: " + src.clipKey); return {}; }
        snd = it->second;
    }

    FMOD_VECTOR p{ src.position.x, src.position.y, src.position.z };
    FMOD_VECTOR v{ src.velocity.x, src.velocity.y, src.velocity.z };
    float blend = src.spatial ? std::clamp(src.spatialBlend, 0.f, 1.f) : 0.f;

    return playBlendedInternal(snd, src.bus,
        src.volume, src.pitch, src.priority,
        blend, src.loop,
        src.spatial ? &p : nullptr,
        src.spatial ? &v : nullptr,
        ownerTag);
}

// ===== 풀링 =====
std::string SoundManager::poolKey(const std::string& clipKey, ChannelType bus) {
    return clipKey + "#" + std::to_string((int)bus);
}

void SoundManager::configureVoicePool(const std::string& clipKey, ChannelType bus, int capacity)
{
    auto& pool = voicePools[poolKey(clipKey, bus)];
    pool.bus = bus;
    pool.capacity = std::max(1, capacity);
    pool.cursor = 0;
    pool.slots.clear();
    pool.slots.resize(pool.capacity);
}

ChannelPair
SoundManager::playOneShotPooled(const std::string& clipKey, ChannelType bus,
    float volume, float pitch, int priority,
    float spatialBlend,
    const FMOD_VECTOR* pos, const FMOD_VECTOR* vel,
    void* ownerTag)
{
    FMOD::Sound* snd = nullptr;
    {
        std::shared_lock rlock(_soundsMutex);
        auto it = sounds.find(clipKey);
        if (it == sounds.end()) { Debug->LogError("Sound not found: " + clipKey); return {}; }
        snd = it->second;
    }

    auto& pool = voicePools[poolKey(clipKey, bus)];
    if (pool.slots.empty()) { pool.bus = bus; pool.capacity = 8; pool.cursor = 0; pool.slots.resize(pool.capacity); }

    const int idx = pool.cursor++ % pool.capacity;
    auto& slot = pool.slots[idx];

    if (slot.ch2D) { bool p = false; if (slot.ch2D->isPlaying(&p) == FMOD_OK && p) slot.ch2D->stop(); slot.ch2D = nullptr; }
    if (slot.ch3D) { bool p = false; if (slot.ch3D->isPlaying(&p) == FMOD_OK && p) slot.ch3D->stop(); slot.ch3D = nullptr; }

    auto made = playBlendedInternal(snd, bus, volume, pitch, priority,
        std::clamp(spatialBlend, 0.f, 1.f),
        /*loop=*/false, pos, vel, ownerTag);
    slot.ch2D = made.ch2D;
    slot.ch3D = made.ch3D;
    return made;
}

void SoundManager::clearVoicePool(const std::string& clipKey, ChannelType bus)
{
    auto it = voicePools.find(poolKey(clipKey, bus));
    if (it == voicePools.end()) return;
    for (auto& s : it->second.slots) {
        if (s.ch2D) { s.ch2D->stop(); s.ch2D = nullptr; }
        if (s.ch3D) { s.ch3D->stop(); s.ch3D = nullptr; }
    }
    voicePools.erase(it);
}

void SoundManager::stopByOwnerTag(void* ownerTag)
{
    if (!ownerTag) return;
    for (int b = 0; b < (int)ChannelType::MaxChannel; ++b)
        stopByOwnerTag(ownerTag, (ChannelType)b);

}

void SoundManager::stopByOwnerTag(void* ownerTag, ChannelType bus)
{
    if (!ownerTag) return;
    FMOD::ChannelGroup* g = groups[(int)bus];
    if (!g) return;

    int n = 0; g->getNumChannels(&n);
    for (int i = 0; i < n; ++i) {
        FMOD::Channel* ch = nullptr;
        if (g->getChannel(i, &ch) == FMOD_OK && ch) {
            void* tag = nullptr;
            ch->getUserData(&tag);
            if (tag == ownerTag) ch->stop();
        }
    }
}

bool SoundManager::getListenerPosition(FMOD_VECTOR& out) const
{
    if (!system) return false;
    FMOD_VECTOR vel{}, fwd{}, up{};
    auto r = system->get3DListenerAttributes(0, &out, &vel, &fwd, &up);
    return r == FMOD_OK;
}

// ===== 내부 유틸 =====
int SoundManager::countPlaying(ChannelType bus) const
{
    int n = 0;
    if (auto* g = groups[(int)bus]) g->getNumChannels(&n);
    return n;
}

void SoundManager::pruneStopped(ChannelType bus)
{
    if (auto* g = groups[(int)bus]) {
        int n = 0; g->getNumChannels(&n);
        for (int i = 0; i < n; ++i) {
            FMOD::Channel* ch = nullptr;
            if (g->getChannel(i, &ch) == FMOD_OK && ch) {
                bool playing = false; ch->isPlaying(&playing);
                (void)playing; // 필요 시 stop()으로 적극 정리 가능
            }
        }
    }
}

FMOD::Channel* SoundManager::findStealCandidate(ChannelType bus, FMOD::Sound* newSound)
{
    auto& cfg = groupCfg[(int)bus];
    FMOD::Channel* cand = nullptr;

    if (auto* g = groups[(int)bus]) {
        int n = 0; g->getNumChannels(&n);

        if (cfg.preemptSameClip && newSound) {
            for (int i = 0; i < n; ++i) {
                FMOD::Channel* ch = nullptr;
                if (g->getChannel(i, &ch) == FMOD_OK && ch) {
                    FMOD::Sound* cur = nullptr;
                    if (ch->getCurrentSound(&cur) == FMOD_OK && cur == newSound) return ch;
                }
            }
        }

        switch (cfg.policy) {
        case StealPolicy::Oldest: {
            unsigned int maxPos = 0;
            for (int i = 0; i < n; ++i) {
                FMOD::Channel* ch = nullptr;
                if (g->getChannel(i, &ch) == FMOD_OK && ch) {
                    bool playing = false; ch->isPlaying(&playing);
                    if (!playing) return ch;
                    unsigned int pos = 0;
                    if (ch->getPosition(&pos, FMOD_TIMEUNIT_MS) == FMOD_OK) {
                        if (pos >= maxPos) { maxPos = pos; cand = ch; }
                    }
                }
            }
            break;
        }
        case StealPolicy::Quietest: {
            float minAud = FLT_MAX;
            for (int i = 0; i < n; ++i) {
                FMOD::Channel* ch = nullptr;
                if (g->getChannel(i, &ch) == FMOD_OK && ch) {
                    bool playing = false; ch->isPlaying(&playing);
                    if (!playing) return ch;
                    float aud = 0.f;
                    if (ch->getAudibility(&aud) == FMOD_OK) {
                        if (aud <= minAud) { minAud = aud; cand = ch; }
                    }
                }
            }
            break;
        }
        case StealPolicy::LowestPriority: {
            int worst = -1;
            for (int i = 0; i < n; ++i) {
                FMOD::Channel* ch = nullptr;
                if (g->getChannel(i, &ch) == FMOD_OK && ch) {
                    bool playing = false; ch->isPlaying(&playing);
                    if (!playing) return ch;
                    int p = 0;
                    if (ch->getPriority(&p) == FMOD_OK) {
                        if (p > worst) { worst = p; cand = ch; }
                    }
                }
            }
            break;
        }
        }
    }
    return cand;
}
