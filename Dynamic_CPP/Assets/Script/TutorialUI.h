#pragma once
#include "Core.Minimal.h"
#include "ModuleBehavior.h"
#include "GameObject.h"
#include "TutorialUI.generated.h"

class TutorialUI : public ModuleBehavior
{
public:
   ReflectTutorialUI
	[[ScriptReflectionField]]
	MODULE_BEHAVIOR_BODY(TutorialUI)
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
	virtual void LateUpdate(float tick) override;
	virtual void OnDisable() override  {}
	virtual void OnDestroy() override  {}

	//0 집기 1던지기 2초원으로 3 공격
	void SetTarget(std::shared_ptr<GameObject> target) { m_target = target; }
	void Init();
	[[Property]]
	Mathf::Vector2 screenOffset = { 0.f, -50.f };

	void SetType(int type); //0 집기 1던지기 2초원으로 3 공격
private:
	std::weak_ptr<GameObject> m_target;
	class RectTransformComponent* m_rect = nullptr;
	class ImageComponent* m_image = nullptr;
	class Camera* m_camera = nullptr;

	int m_type{};
};
