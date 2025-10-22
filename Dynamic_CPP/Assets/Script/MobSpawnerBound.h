#pragma once
#include "Core.Minimal.h"
#include "ModuleBehavior.h"

class Entity;
class MobSpawner;
class MobSpawnerBound : public ModuleBehavior
{
public:
	MODULE_BEHAVIOR_BODY(MobSpawnerBound)
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

private:
	//�ڽ����� Host  , Target 2���������ְ�  host�� host tag, target�� target �±״޾��ֱ�
	GameObject* m_hostObj = nullptr;
	GameObject* m_targetObj = nullptr;
	std::vector<std::weak_ptr<Entity>> m_hosts;
	std::vector<MobSpawner*> m_targets;
};
