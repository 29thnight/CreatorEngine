#pragma once
#include <mathematics/vector3.hpp>
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

	void Play();
	void Stop();
	void Pause(bool pause);
	bool IsPlaying();
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
	math::vector3 position{ 0,0,0 };
	math::vector3 velocity{ 0,0,0 };
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
	bool spatial = false;          //false = 2D, true = ºí·»µå(2D + 3D)
	bool useReverbSend = false;


};
