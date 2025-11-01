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
	Mathf::Vector3 minOffset{ 0.f,15.f ,-11.5f };
	Mathf::Vector3 maxOffset{ 0.f,23.f ,- 16.5f };
	Mathf::Vector3 preOffset = {0,0,0};
	float maxDistance = 25.f;
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

	Mathf::Vector3 targetPosition{ 0.f, 0.f, 0.f };

	[[Property]]
	bool OnCaculCamera = false;
	

private:
	//camera shake
	float shakeDuration = 0.f;
	float shakeMagnitude = 0.7f;
	float dampingSpeed = 1.0f;
	Mathf::Vector3 initialPosition{ 0.f, 0.f, 0.f };

	[[Method]]
	void ShakeCamera(float duration);
	[[Method]]
	void ShakeCamera1s();

public:
	void StopCameraMoveFlag() { cameraMoveStopFlag = true; }
private:
	bool cameraMoveStopFlag = false;
};
