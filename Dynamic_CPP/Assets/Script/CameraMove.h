#pragma once
#include "Core.Minimal.h"
#include "ModuleBehavior.h"
#include "CameraMove.generated.h"

class GameManager;
class CameraMove : public ModuleBehavior
{
public:
   ReflectCameraMove
	[[ScriptReflectionField]]
	MODULE_BEHAVIOR_BODY(CameraMove)
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

private:
	GameObject* target = nullptr;
	GameManager* GM = nullptr;
	[[Property]]
	float followSpeed{ 1.0f };
	[[Property]]
	Mathf::Vector3 offset{ 0.f, 20.f, -11.5f };
	[[Property]]
	float detectRange{ 1.f };


	[[Method]]
	void OnCameraControll();
	[[Method]]
	void OffCameraCOntroll();
	[[Method]]
	void CameraMoveFun(Mathf::Vector2 dir);
	[[Property]]
	float cameraMoveSpeed = 0.05f;


	float followTimer{ 0.f };
	Mathf::Vector3 targetPosition{ 0.f, 0.f, 0.f };
};
