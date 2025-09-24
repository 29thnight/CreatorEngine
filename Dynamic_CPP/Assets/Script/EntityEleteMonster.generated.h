#pragma once

#define ReflectEntityEleteMonster \
ReflectionScriptField(EntityEleteMonster) \
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
		meta_property(m_retreatRange) \
		meta_property(m_retreatCoolTime) \
		meta_property(m_retreatDistance) \
		meta_property(m_avoidanceStrength) \
		meta_property(m_teleportDistance) \
		meta_property(m_teleportCoolTime) \
	}); \
	MethodField \
	({ \
		meta_method(ShootingAttack) \
	}); \
	FieldEnd(EntityEleteMonster, PropertyAndMethod) \
};
