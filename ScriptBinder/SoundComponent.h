#pragma once
#include "Core.Minimal.h"
#include "Component.h"
#include "SoundDefinition.h"

namespace FMOD
{
	class Channel;
}

class SoundComponent : public meta::identity<SoundComponent, Component>
{
public:
   static consteval auto describe()
   {
       return meta::describe<SoundComponent>(
           meta::base<Component>(),
           meta::member<&SoundComponent::clipKey>(),
           meta::member<&SoundComponent::bus>(),
           meta::member<&SoundComponent::volume>(),
           meta::member<&SoundComponent::pitch>(),
           meta::member<&SoundComponent::priority>(),
           meta::member<&SoundComponent::spatialBlend>(),
           meta::member<&SoundComponent::minDistance>(),
           meta::member<&SoundComponent::maxDistance>(),
           meta::member<&SoundComponent::reverbLevel>(),
           meta::member<&SoundComponent::reverbIndex>(),
           meta::member<&SoundComponent::rolloff>(),
           meta::member<&SoundComponent::velocity>(),
           meta::member<&SoundComponent::localRolloffCurve>(),
           meta::member<&SoundComponent::loop>(),
           meta::member<&SoundComponent::playOnStart>(),
           meta::member<&SoundComponent::spatial>(),
           meta::member<&SoundComponent::useReverbSend>(),
           meta::method<&SoundComponent::Play>(),
           meta::method<&SoundComponent::Stop>(),
           meta::method<&SoundComponent::Pause>("pause"),
           meta::method<&SoundComponent::IsPlaying>(),
           meta::method<&SoundComponent::PlayOneShot>());
   }
	SoundComponent() = default;

	void Start() override;
	void Update(float tick) override;
	void LateUpdate(float tick) override;
	void OnDestroy() override;

	void Play();
	void Stop();
	void Pause(bool pause);
	bool IsPlaying();
	void PlayOneShot();

	void EditorSet();

	FMOD::Channel* Get2DChannel() const { return channel2D; }
	FMOD::Channel* Get3DChannel() const { return channel3D; }

public:
	std::string clipKey; // SoundManager::sounds 키
	ChannelType bus = ChannelType::SFX;
	float volume = 1.f;
	float pitch = 1.f;
	int priority = 128;


public:
	float spatialBlend = 1.0f;      // 0=2D, 1=3D, 중간은 듀얼채널 crossfade
	float minDistance = 1.0f;
	float maxDistance = 50.0f;

public:
	float  reverbLevel = 0.0f;    // -80dB~+10dB 범위 권장 (FMOD send)
	int    reverbIndex = 0;       // 0~3 (FMOD 표준 리버브 버스 인덱스)
	Rolloff rolloff = Rolloff::Inverse;

	// 3D 속성(엔진 좌표에서 받아 세팅)
	Mathf::Vector3 position{ 0,0,0 };
	Mathf::Vector3 velocity{ 0,0,0 };
	std::vector<CurvePoint> localRolloffCurve;

private:
	float SampleLocalRolloff(float d) const;

	FMOD_VECTOR _pos{};
	FMOD_VECTOR _velocity{};

	FMOD::Channel* channel2D = nullptr;
	FMOD::Channel* channel3D = nullptr;

public:
	bool loop = false;
	bool playOnStart = false;
	bool spatial = false;          //false = 2D, true = 블렌드(2D + 3D)
	bool useReverbSend = false;


};
