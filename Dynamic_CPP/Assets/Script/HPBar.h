#pragma once
#include "Core.Minimal.h"
#include "ModuleBehavior.h"
#include "HPBar.generated.h"
#include "GameObject.h"

class HPBar : public ModuleBehavior
{
public:
   ReflectHPBar
	[[ScriptReflectionField]]
	MODULE_BEHAVIOR_BODY(HPBar)
	virtual void Awake() override {}
	virtual void Start() override;
	virtual void FixedUpdate(float fixedTick) override {}
	virtual void OnTriggerEnter(const Collision& collision) override {}
	virtual void OnTriggerStay(const Collision& collision) override {}
	virtual void OnTriggerExit(const Collision& collision) override {}
	virtual void OnCollisionEnter(const Collision& collision) override {}
	virtual void OnCollisionStay(const Collision& collision) override {}
	virtual void OnCollisionExit(const Collision& collision) override {}
	virtual void Update(float tick) override {}
	virtual void LateUpdate(float tick) override;
	virtual void OnDisable() override  {}
	virtual void OnDestroy() override  {}

	void SetTarget(std::shared_ptr<GameObject> target) { m_target = target; }

	[[Property]]
	GameObject::Index targetIndex = GameObject::INVALID_INDEX;

	[[Property]]
	Mathf::Vector2 screenOffset = { 0.f, -50.f };

	void SetType(int type); //0 : p1 or mon(프리팹으로 구분), 1 : p2
	
	void SetMaxHP(int maxHP) 
	{ 
		m_maxHP = maxHP; 
		if (m_currentHP > m_maxHP)
			m_currentHP = m_maxHP;
	}

	void SetCurHP(int curHP) 
	{ 
		m_currentHP = curHP; 
		if (m_currentHP > m_maxHP)
			m_currentHP = m_maxHP;
		if (m_currentHP < 0)
			m_currentHP = 0;
	}


private:
	std::weak_ptr<GameObject> m_target;
	class RectTransformComponent* m_rect = nullptr;
	class ImageComponent* m_image = nullptr;

	int m_currentHP{};
	int m_maxHP{};
	int m_type{};
};
