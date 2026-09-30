#pragma once
#include <mathematics/vector3.hpp>
#include "Core.Minimal.h"
#include "Component.h"
#include "SoundDefinition.h"

namespace FMOD
{
	class Channel;
}

class [[reflgen::reflect]] SoundComponent : public meta::identity<SoundComponent, Component>
{
   public:
public:
	SoundComponent() = default;

	void OnBeginSimulation() override;
	void OnUninitializing() override;

	// íŠ¸ë™ C3: ê°€ìƒ Update/LateUpdate ì˜¤ë²„ë¼ì´ë“œë¥¼ ê±·ì–´ë‚´ê³  SoundSystem(ì¡°ë°€
	// ë²¡í„°, ì „ìš© í‹±)ìœ¼ë¡œ ì˜®ê²¼ë‹¤ â€” ë“±ë¡/í•´ì§€ëŠ” ì”¬ í¸ì…/ì´íƒˆ í›…ìœ¼ë¡œ í•œë‹¤(DDOL
	// ì•ˆì „, ê·¼ê±°ëŠ” SoundSystem.h ì£¼ì„ ì°¸ê³ ). ì•„ë˜ TickUpdate/TickLateUpdateëŠ”
	// SoundSystemì´ ë¶€ë¥´ëŠ” í‰ë²”í•œ ë©¤ë²„ í•¨ìˆ˜ë‹¤(ê°€ìƒ ì˜¤ë²„ë¼ì´ë“œê°€ ì•„ë‹ˆë‹¤ â€”
	// Component::Update/LateUpdateì™€ ì´ë¦„ì´ ê²¹ì¹˜ë©´ LifecycleRegistry::
	// MaskOfTypeì´ ë‹¤ì‹œ ì•”ë¬µ êµ¬ë…ìœ¼ë¡œ ì¡ëŠ”ë‹¤).
	void OnAddedToScene() override;
	void OnRemovingFromScene() override;
	void TickUpdate(float tick);
	void TickLateUpdate(float tick);

	[[reflgen::reflect]]
	void Play();

	[[reflgen::reflect]]
	void Stop();

	[[reflgen::reflect]]
	void Pause(bool pause);

	[[reflgen::reflect, creator::read_only_in_inspector]]
	bool IsPlaying();

	[[reflgen::reflect]]
	void PlayOneShot();

	void EditorSet();

	FMOD::Channel* Get2DChannel() const { return channel2D; }
	FMOD::Channel* Get3DChannel() const { return channel3D; }

public:
	std::string clipKey; // SoundManager::sounds Å°
	ChannelType bus = ChannelType::SFX;
	float volume = 1.f;
	float pitch = 1.f;
	int priority = 128;


public:
	float spatialBlend = 1.0f;      // 0=2D, 1=3D, Áß°£Àº µà¾óÃ¤³Î crossfade
	float minDistance = 1.0f;
	float maxDistance = 50.0f;

public:
	float  reverbLevel = 0.0f;    // -80dB~+10dB ¹üÀ§ ±ÇÀå (FMOD send)
	int    reverbIndex = 0;       // 0~3 (FMOD Ç¥ÁØ ¸®¹öºê ¹ö½º ÀÎµ¦½º)
	Rolloff rolloff = Rolloff::Inverse;

	// 3D ¼Ó¼º(¿£Áø ÁÂÇ¥¿¡¼­ ¹Ş¾Æ ¼¼ÆÃ)
	[[reflgen::ignore]]
	math::vector3 position{ 0,0,0 };

	math::vector3 velocity{ 0,0,0 };
	std::vector<CurvePoint> localRolloffCurve;

private:
	float SampleLocalRolloff(float d) const;

	[[reflgen::ignore]]
	FMOD_VECTOR _pos{};

	[[reflgen::ignore]]
	FMOD_VECTOR _velocity{};

	[[reflgen::ignore]]
	FMOD::Channel* channel2D = nullptr;

	[[reflgen::ignore]]
	FMOD::Channel* channel3D = nullptr;

public:
	bool loop = false;
	bool playOnStart = false;
	bool spatial = false;          //false = 2D, true = ºí·»µå(2D + 3D)
	bool useReverbSend = false;


};
