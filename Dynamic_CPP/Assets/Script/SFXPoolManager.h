#pragma once
#include "Core.Minimal.h"
#include "ModuleBehavior.h"
#include "SoundComponent.h"

//실사용할려면 게임매니저의 GetSFXPool()로 요청
class SFXPoolManager : public ModuleBehavior
{
public:
	MODULE_BEHAVIOR_BODY(SFXPoolManager)
	virtual void Awake() override {}
	virtual void Start() override;
	virtual void FixedUpdate(float fixedTick) override {}
	virtual void OnTriggerEnter(const Collision& collision) override {}
	virtual void OnTriggerStay(const Collision& collision) override {}
	virtual void OnTriggerExit(const Collision& collision) override {}
	virtual void OnCollisionEnter(const Collision& collision) override {}
	virtual void OnCollisionStay(const Collision& collision) override {}
	virtual void OnCollisionExit(const Collision& collision) override {}
	virtual void Update(float tick) override;
	virtual void LateUpdate(float tick) override {}
	virtual void OnDisable() override  {}
	virtual void OnDestroy() override  {}

	std::vector<SoundComponent*> SFXSoundPool{};

	//자기소유 오브젝트들 검사해서 빈거 알아서줌 없으면 playX // 사운드클립 이름string 넣기 //한번만재생
	void PlayOneShot(std::string _ClipName);
};
