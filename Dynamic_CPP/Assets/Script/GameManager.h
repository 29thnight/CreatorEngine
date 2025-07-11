#pragma once
#include "Core.Minimal.h"
#include "ModuleBehavior.h"

class Entity;
class ActionMap;
class GameManager : public ModuleBehavior
{
public:
	MODULE_BEHAVIOR_BODY(GameManager)
	virtual void Awake() override;
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
	virtual void OnDisable() override;
	virtual void OnDestroy() override  {}

public:
	void Inputblabla();
	void LoadScene(const std::string& sceneName);

public:
	void PushEntity(Entity* entity);
	const std::vector<Entity*>& GetEntities();

private:
	std::vector<Entity*> m_entities;
	ActionMap* playerMap{ nullptr };

	std::vector<Entity*> m_resourcePool;
	std::vector<Entity*> m_weaponPiecePool;
private:
	void CheatMiningResource();
};
