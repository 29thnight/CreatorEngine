#pragma once
#include "Core.Minimal.h"
#include "ModuleBehavior.h"
#include "Entity.h"
#include "ItemType.h"
#include "EntityItemHeal.generated.h"

class EffectComponent;
//������ �ٰ����� �÷��̾� HP ȸ���Ǵ� ������ //������ �� ������ ���� ���� ��ũ��Ʈ
class EntityItemHeal : public Entity
{
public:
   ReflectEntityItemHeal
	[[ScriptReflectionField]]
	MODULE_BEHAVIOR_BODY(EntityItemHeal)
	virtual void Awake() override {}
	virtual void Start() override;
	virtual void FixedUpdate(float fixedTick) override {}
	virtual void OnTriggerEnter(const Collision& collision) override;
	virtual void OnTriggerStay(const Collision& collision) override {}
	virtual void OnTriggerExit(const Collision& collision) override {}
	virtual void OnCollisionEnter(const Collision& collision) override {}
	virtual void OnCollisionStay(const Collision& collision) override {}
	virtual void OnCollisionExit(const Collision& collision) override {}
	virtual void Update(float tick) override;
	virtual void LateUpdate(float tick) override {}
	virtual void OnDisable() override  {}
	virtual void OnDestroy() override  {}

	bool CanHeal();
	void Use();
	int GetHealAmount();
	[[Property]]
	int  itemCode = 0;
	[[Property]]
	EItemType itemType = EItemType::Flower;
	
private:
	[[Property]]
	int healAmount = 10;
	EffectComponent* m_effect = nullptr;

	bool canHeal = true; //�÷��̾� 2��� �浹�ص� �Ѹ� ���ؾ� �ϴϱ�
};
