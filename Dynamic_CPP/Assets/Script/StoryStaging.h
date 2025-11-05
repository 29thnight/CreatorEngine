#pragma once
#include "Core.Minimal.h"
#include "ModuleBehavior.h"
#include "StoryStaging.generated.h"

class StoryStaging : public ModuleBehavior
{
public:
   ReflectStoryStaging
	[[ScriptReflectionField]]
	MODULE_BEHAVIOR_BODY(StoryStaging)
	virtual void Awake() override {}
	virtual void Start() override;
	virtual void FixedUpdate(float fixedTick) override {}
	virtual void OnTriggerEnter(const Collision& collision) override;
	virtual void OnTriggerStay(const Collision& collision) override {}
	virtual void OnTriggerExit(const Collision& collision) override;
	virtual void OnCollisionEnter(const Collision& collision) override {}
	virtual void OnCollisionStay(const Collision& collision) override {}
	virtual void OnCollisionExit(const Collision& collision) override {}
	virtual void Update(float tick) override;
	virtual void LateUpdate(float tick) override {}
	virtual void OnDisable() override  {}
	virtual void OnDestroy() override  {}

	[[Property]]
	int stagingID = 0;

private:
	std::vector<GameObject*> players;
	void StartAction();
	Mathf::Vector3 stagingPositions[2];
	class LetterboxController* m_letterboxController = nullptr;
	bool m_actionEnd = false;
	Mathf::Vector3 stagingForward[2];
	int currentStagingIndex = 0;
};
