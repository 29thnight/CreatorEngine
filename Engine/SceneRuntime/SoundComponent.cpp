#include "SoundComponent.h"
#include "MathematicsInterop.h"
#include "SoundManager.h"
#include "SoundSystem.h"

static FMOD_VECTOR ToFVec(const math::vector3& vec)
{
    FMOD_VECTOR fvec{ 
        .x = vec.x, 
        .y = vec.y,
        .z = vec.z 
    };

    return fvec;
}

void SoundComponent::OnBeginSimulation()
{
    if (localRolloffCurve.empty()) {
        // ±âº» Ä¿ºê: (0m,1) ¡æ (maxDistance,0)
        CurvePoint base{}, end{};

        end.distance = std::max(0.1f, maxDistance);
        end.gain = 0.f;

        localRolloffCurve.push_back(base);
        localRolloffCurve.push_back(end);
    }

    if (playOnStart && !clipKey.empty()) 
        Play();
}

// íŠ¸ëž™ C3 â€” SoundSystem ë“±ë¡/í•´ì§€. Awake/OnDestroy(ì»´í¬ë„ŒíŠ¸ë‹¹ 1íšŒ ê²Œì´íŠ¸)ê°€
// ì•„ë‹ˆë¼ ì”¬ íŽ¸ìž…/ì´íƒˆ í›…ì„ ì“°ëŠ” ì´ìœ ëŠ” SoundSystem.h ìƒë‹¨ ì£¼ì„ ì°¸ì¡° â€” DDOL
// ì˜¤ë¸Œì íŠ¸ê°€ ì”¬ì„ ê±´ë„ ë•Œë„ ë§¤ë²ˆ ë‹¤ì‹œ ë¶ˆë ¤ì•¼ í•˜ê¸° ë•Œë¬¸ì´ë‹¤. ì‹¤ì œ íŒŒê´´ ê²½ë¡œ
// (Scene::FlushPendingDestroyÂ·PrefabUtility::ApplyComponentDiff)ë„ ì‹¤ íŒŒê´´
// (OnDestroy) ì§ì „ì— OnRemovingFromSceneì„ ë¨¼ì € ë¶€ë¥´ë¯€ë¡œ, ì´ ì‹œìŠ¤í…œì—ì„œ
// ë¹ ì§€ëŠ” ì‹œì ì´ í•­ìƒ ì‹¤ íŒŒê´´ë³´ë‹¤ ë¨¼ì €ë‹¤.
void SoundComponent::OnAddedToScene()
{
    SoundSystems->Register(this);
}

void SoundComponent::OnRemovingFromScene()
{
    SoundSystems->Unregister(this);
}

void SoundComponent::TickUpdate(float tick)
{
    auto owner = GetOwner();
    if (owner)
    {
        auto transform = owner->GetComponent<Transform>();

        position = transform->GetWorldPosition();

        if (channel3D) {
            _pos = ToFVec(position);
            _velocity = ToFVec(velocity);
            channel3D->set3DAttributes(&_pos, &_velocity);
        }
    }
}

void SoundComponent::TickLateUpdate(float tick)
{
    // ¦¡¦¡ ·ÎÄÃ Rolloff ¿À¹ö¶óÀÌµå Àû¿ë ¦¡¦¡
    if (spatial && rolloff == Rolloff::Custom)
    {
        FMOD_VECTOR lis{};
        if (Sound->getListenerPosition(lis))
        {
            float dx = position.x - lis.x;
            float dy = position.y - lis.y;
            float dz = position.z - lis.z;
            float d = std::sqrt(dx * dx + dy * dy + dz * dz);

            float w2D = 1.f, w3D = 0.f;
            // (µ¿ÀÏ: equal-power ºí·»µå)
            // ComputeEqualPower(...) ¸¦ µ¿ÀÏÇÏ°Ô »ç¿ë
            // t=spatialBlend
            w2D = cosf(spatialBlend * (3.14159265f * 0.5f));
            w3D = sinf(spatialBlend * (3.14159265f * 0.5f));

            float att = SampleLocalRolloff(d);
            if (channel2D) channel2D->setVolume(volume * w2D);
            if (channel3D) channel3D->setVolume(volume * w3D * att);
        }
    }
}

void SoundComponent::OnUninitializing()
{
    Stop();
}

void SoundComponent::Play()
{
    if (clipKey.empty()) return;

    // ÀÌÀü¿¡ ³»°¡ ¶ç¿î ·çÇÁ/ÀÜÁ¸ Ã¤³Î Á¤¸®(¹ö½º ÇÑÁ¤)
    Sound->stopByOwnerTag(this, bus);

    // SpatialBlend(0~1)¿¡ µû¶ó 2D/3D µà¾óÃ¤³Î »ý¼º, ownerTag=this
    auto pair = Sound->playFromSourceBlended(*this, this);
    channel2D = pair.ch2D;
    channel3D = pair.ch3D;

    // º¼·ý/ÇÇÄ¡/¿ì¼±¼øÀ§ º¸Á¤(ÀÌ¹Ì ³»ºÎ¿¡¼­ ¼¼ÆÃÇÏÁö¸¸ ¾ÈÀü¿ë)
    if (channel2D) { channel2D->setVolume(volume); channel2D->setPitch(pitch); channel2D->setPriority(priority); }
    if (channel3D) { channel3D->setVolume(volume); channel3D->setPitch(pitch); channel3D->setPriority(priority); }

    // ·ÎÄÃ ¿À¹ö¶óÀÌµå ½Ã: FMOD ±âº» °¨¼è¸¦ ¾àÈ­(»ç½Ç»ó ÆòÅºÈ­)
    if (rolloff == Rolloff::Custom && channel3D) {
        channel3D->set3DMinMaxDistance(0.01f, 1e6f); // ¸Å¿ì Å« max·Î ±âº» °¨¼è¸¦ °ÅÀÇ 1·Î
    }
}

void SoundComponent::Stop()
{
    // ³»°¡ ¼ÒÀ¯ÇÑ ¸ðµç Ã¤³Î(¹ö½º ºÒ¹®) Á¾·á
    Sound->stopByOwnerTag(this);
    channel2D = nullptr;
    channel3D = nullptr;
}

void SoundComponent::Pause(bool pause)
{
    if (channel2D) channel2D->setPaused(pause);
    if (channel3D) channel3D->setPaused(pause);
}

bool SoundComponent::IsPlaying()
{
    bool playing = false;
    if (channel2D) { if (channel2D->isPlaying(&playing) == FMOD_OK && playing) return true; }
    if (channel3D) { if (channel3D->isPlaying(&playing) == FMOD_OK && playing) return true; }
    return false;
}

void SoundComponent::PlayOneShot()
{
    if (clipKey.empty()) return;
    auto pos = ToFVec(position);
    auto vel = ToFVec(velocity);

    // Ç®¸µ ¾øÀÌ ¹Ù·Î ¿ø¼¦ (ÇÊ¿ä½Ã SoundManager::playOneShotPooled »ç¿ë)
    auto pair = Sound->playOneShotPooled(
        clipKey, bus, volume, pitch, priority,
        spatial ? spatialBlend : 0.0f,
        spatial ? &pos : nullptr,
        spatial ? &vel : nullptr,
        this
    );
    // ¿ø¼¦Àº Ã¤³Î ÀúÀå ¾È ÇØµµ µÇÁö¸¸, ÇÊ¿äÇÏ¸é ÂüÁ¶ º¸°ü:
    channel2D = pair.ch2D;
    channel3D = pair.ch3D;
}

void SoundComponent::EditorSet()
{
    auto owner = GetOwner();
    if(owner)
    {
        auto transform = owner->GetComponent<Transform>();

        position = transform->GetWorldPosition();

        if (channel3D) {
            _pos = ToFVec(position);
            _velocity = ToFVec(velocity);
            channel3D->set3DAttributes(&_pos, &_velocity);
        }
    }
}

float SoundComponent::SampleLocalRolloff(float d) const
{
    if (localRolloffCurve.empty()) return 1.f;
    if (d <= localRolloffCurve.front().distance) return localRolloffCurve.front().gain;
    if (d >= localRolloffCurve.back().distance)  return localRolloffCurve.back().gain;

    // ÀÌºÐ Å½»ö + º¸°£
    int lo = 0, hi = (int)localRolloffCurve.size() - 1;
    while (hi - lo > 1) {
        int mid = (lo + hi) >> 1;
        (localRolloffCurve[mid].distance <= d ? lo : hi) = mid;
    }
    const auto& a = localRolloffCurve[lo];
    const auto& b = localRolloffCurve[hi];
    float t = (d - a.distance) / std::max(1e-6f, (b.distance - a.distance));
    return std::clamp(a.gain + (b.gain - a.gain) * t, 0.f, 1.f);
}
