#pragma once

#define ReflectEntityMonsterA \
ReflectionScriptField(EntityMonsterA) \
{ \
	PropertyField \
	({ \
		meta_property(isAsisAction) \
		meta_property(m_maxHP) \
		meta_property(m_enemyReward) \
		meta_property(m_moveSpeed) \
		meta_property(m_chaseRange) \
		meta_property(m_rangeOutDuration) \
		meta_property(m_attackRange) \
		meta_property(m_attackDamage) \
	}); \
	MethodField \
	({ \
		meta_method(AttackBoxOn) \
		meta_method(AttackBoxOff) \
	}); \
	FieldEnd(EntityMonsterA, PropertyAndMethod) \
};
