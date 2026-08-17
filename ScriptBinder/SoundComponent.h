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
   static consteval auto reflect()
   {
       return meta::schema<Self>(
           meta::field<&Self::clipKey>,
           meta::field<&Self::bus>,
           meta::field<&Self::volume>,
           meta::field<&Self::pitch>,
           meta::field<&Self::priority>,
           meta::field<&Self::spatialBlend>,
           meta::field<&Self::minDistance>,
           meta::field<&Self::maxDistance>,
           meta::field<&Self::reverbLevel>,
           meta::field<&Self::reverbIndex>,
           meta::field<&Self::rolloff>,
           meta::field<&Self::velocity>,
           meta::field<&Self::localRolloffCurve>,
           meta::field<&Self::loop>,
           meta::field<&Self::playOnStart>,
           meta::field<&Self::spatial>,
           meta::field<&Self::useReverbSend>,
           meta::method<&Self::Play>,
           meta::method<&Self::Stop>,
           meta::method<&Self::Pause>.params("pause"),
           meta::method<&Self::IsPlaying>,
           meta::method<&Self::PlayOneShot>);
   }
public:
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
