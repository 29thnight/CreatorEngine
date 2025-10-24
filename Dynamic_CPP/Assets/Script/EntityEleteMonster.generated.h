#pragma once

#define ReflectEntityEleteMonster \
ReflectionScriptField(EntityEleteMonster) \
{ \
	PropertyField \
	({ \
		meta_property(isAsisAction) \
		meta_property(maxHP) \
		meta_property(m_enemyReward) \
		meta_property(m_moveSpeed) \
		meta_property(m_chaseRange) \
		meta_property(m_rangeOutDuration) \
		meta_property(m_rangedAttackDamage) \
		meta_property(m_projectileDamegeRadius) \
		meta_property(m_projectileSpeed) \
		meta_property(m_projectileRange) \
		meta_property(m_rangedAttackCoolTime) \
		meta_property(m_retreatRange) \
		meta_property(m_retreatCoolTime) \
		meta_property(m_retreatDistance) \
		meta_property(m_avoidanceStrength) \
		meta_property(m_teleportDistance) \
		meta_property(m_teleportCoolTime) \
		meta_property(m_teleportDelay) \
		meta_property(KnockbackDistacneX) \
		meta_property(KnockbackDistacneY) \
		meta_property(KnockbackTime) \
	}); \
	MethodField \
	({ \
		meta_method(ShootingAttack) \
		meta_method(DeadEvent) \
	}); \
	FieldEnd(EntityEleteMonster, PropertyAndMethod) \
};
