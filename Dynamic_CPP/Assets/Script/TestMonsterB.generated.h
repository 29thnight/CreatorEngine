#pragma once

#define ReflectTestMonsterB \
ReflectionScriptField(TestMonsterB) \
{ \
	PropertyField \
	({ \
		meta_property(isAsisAction) \
		meta_property(m_maxHP) \
		meta_property(m_enemyReward) \
		meta_property(m_moveSpeed) \
		meta_property(m_chaseRange) \
		meta_property(m_rangeOutDuration) \
		meta_property(m_rangedAttackDamage) \
		meta_property(m_projectileDamegeRadius) \
		meta_property(m_projectileSpeed) \
		meta_property(m_projectileRange) \
		meta_property(m_projectileArcHeight) \
		meta_property(m_rangedAttackCoolTime) \
	}); \
	MethodField \
	({ \
		meta_method(ShootingAttack) \
		meta_method(DeadEvent) \
	}); \
	FieldEnd(TestMonsterB, PropertyAndMethod) \
};
