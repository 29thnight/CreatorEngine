#include "SoundComponent.h"
#include "SoundManager.h"

static FMOD_VECTOR ToFVec(const Mathf::Vector3& vec)
{
    FMOD_VECTOR fvec{ 
        .x = vec.x, 
        .y = vec.y,
        .z = vec.z 
    };

    return fvec;
}

void SoundComponent::Start()
{
    if (localRolloffCurve.empty()) {
        // 기본 커브: (0m,1) → (maxDistance,0)
        CurvePoint base{}, end{};

        end.distance = std::max(0.1f, maxDistance);
        end.gain = 0.f;

        localRolloffCurve.push_back(base);
        localRolloffCurve.push_back(end);
    }

    if (playOnStart && !clipKey.empty()) 
        Play();
}

void SoundComponent::Update(float tick)
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

void SoundComponent::LateUpdate(float tick)
{
    // ── 로컬 Rolloff 오버라이드 적용 ──
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
            // (동일: equal-power 블렌드)
            // ComputeEqualPower(...) 를 동일하게 사용
            // t=spatialBlend
            w2D = cosf(spatialBlend * (3.14159265f * 0.5f));
            w3D = sinf(spatialBlend * (3.14159265f * 0.5f));

            float att = SampleLocalRolloff(d);
            if (channel2D) channel2D->setVolume(volume * w2D);
            if (channel3D) channel3D->setVolume(volume * w3D * att);
        }
    }
}

void SoundComponent::OnDestroy()
{
    Stop();
}

void SoundComponent::Play()
{
    if (clipKey.empty()) return;

    // 이전에 내가 띄운 루프/잔존 채널 정리(버스 한정)
    Sound->stopByOwnerTag(this, bus);

    // SpatialBlend(0~1)에 따라 2D/3D 듀얼채널 생성, ownerTag=this
    auto pair = Sound->playFromSourceBlended(*this, this);
    channel2D = pair.ch2D;
    channel3D = pair.ch3D;

    // 볼륨/피치/우선순위 보정(이미 내부에서 세팅하지만 안전용)
    if (channel2D) { channel2D->setVolume(volume); channel2D->setPitch(pitch); channel2D->setPriority(priority); }
    if (channel3D) { channel3D->setVolume(volume); channel3D->setPitch(pitch); channel3D->setPriority(priority); }

    // 로컬 오버라이드 시: FMOD 기본 감쇠를 약화(사실상 평탄화)
    if (rolloff == Rolloff::Custom && channel3D) {
        channel3D->set3DMinMaxDistance(0.01f, 1e6f); // 매우 큰 max로 기본 감쇠를 거의 1로
    }
}

void SoundComponent::Stop()
{
    // 내가 소유한 모든 채널(버스 불문) 종료
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

    // 풀링 없이 바로 원샷 (필요시 SoundManager::playOneShotPooled 사용)
    auto pair = Sound->playOneShotPooled(
        clipKey, bus, volume, pitch, priority,
        spatial ? spatialBlend : 0.0f,
        spatial ? &pos : nullptr,
        spatial ? &vel : nullptr,
        this
    );
    // 원샷은 채널 저장 안 해도 되지만, 필요하면 참조 보관:
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

    // 이분 탐색 + 보간
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
