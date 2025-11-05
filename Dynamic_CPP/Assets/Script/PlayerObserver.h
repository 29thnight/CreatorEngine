#pragma once
#include "Core.Minimal.h"
#include "ModuleBehavior.h"
#include "PlayerObserver.generated.h"

class PlayerObserver : public ModuleBehavior
{
public:
   ReflectPlayerObserver
	[[ScriptReflectionField]]
	MODULE_BEHAVIOR_BODY(PlayerObserver)
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

	void SetTarget(std::shared_ptr<GameObject> target) { m_target = target; }
	void SetPlayerIndex(int index) { m_playerIndex = index; }
	void Init();

	[[Property]]
	Mathf::Vector2 screenOffset = { 0.f, -70.f };
	[[Property]]
	float WaitBeforeFade{ 2.0f }; // 2√  ¥Î±‚
	[[Property]]
	bool m_isCinema{};

private:
	std::weak_ptr<GameObject> m_target;
	class RectTransformComponent* m_rect = nullptr;
	class ImageComponent* m_image = nullptr;
	class Camera* m_camera = nullptr;

	float m_lerpSpeed = 2.0f;
	float m_elapsedTime = 0.0f;
	int m_playerIndex = -1;
};
