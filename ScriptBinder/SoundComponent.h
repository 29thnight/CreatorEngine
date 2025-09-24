#pragma once
#include "Core.Minimal.h"
#include "Component.h"
#include "SoundDefinition.h"
#include "IRegistableEvent.h"
#include "SoundComponent.generated.h"

namespace FMOD
{
	class Channel;
}

class SoundComponent : public Component, public RegistableEvent<SoundComponent>
{
public:
   ReflectSoundComponent
	[[Serializable(Inheritance:Component)]]
	GENERATED_BODY(SoundComponent)

	void Start() override;
	void Update(float tick) override;
	void OnDestroy() override;

	[[Method]]
	void Play();
	[[Method]]
	void Stop();
	[[Method]]
	void Pause(bool pause);
	[[Method]]
	bool IsPlaying();
	[[Method]]
	void PlayOneShot();

	void EditorSet();

	FMOD::Channel* Get2DChannel() const { return channel2D; }
	FMOD::Channel* Get3DChannel() const { return channel3D; }

public:
	[[Property]]
	std::string clipKey; // SoundManager::sounds Ű
	[[Property]]
	ChannelType bus = ChannelType::SFX;
	[[Property]]
	float volume = 1.f;
	[[Property]]
	float pitch = 1.f;
	[[Property]]
	int priority = 128;
	[[Property]]
	bool loop = false;
	[[Property]]
	bool playOnStart = false;

public:
	[[Property]]
	bool  spatial = false;          //false = 2D, true = ����(2D + 3D)
	[[Property]]
	float spatialBlend = 1.0f;      // 0=2D, 1=3D, �߰��� ���ä�� crossfade
	[[Property]]
	float minDistance = 1.0f;
	[[Property]]
	float maxDistance = 50.0f;

public:
	[[Property]]
	bool   useReverbSend = false;
	[[Property]]
	float  reverbLevel = 0.0f;    // -80dB~+10dB ���� ���� (FMOD send)
	[[Property]]
	int    reverbIndex = 0;       // 0~3 (FMOD ǥ�� ������ ���� �ε���)
	[[Property]]
	Rolloff rolloff = Rolloff::Inverse;

	// 3D �Ӽ�(���� ��ǥ���� �޾� ����)
	Mathf::Vector3 position{ 0,0,0 };
	[[Property]]
	Mathf::Vector3 velocity{ 0,0,0 };
	[[Property]]
	std::vector<CurvePoint> localRolloffCurve;

private:
	float SampleLocalRolloff(float d) const;

	FMOD_VECTOR _pos{};
	FMOD_VECTOR _velocity{};

	FMOD::Channel* channel2D = nullptr;
	FMOD::Channel* channel3D = nullptr;
};
