#pragma once

#define ReflectEntityBigWood \
ReflectionScriptField(EntityBigWood) \
{ \
	PropertyField \
	({ \
		meta_property(m_logDamage) \
		meta_property(type1) \
		meta_property(type1Count) \
		meta_property(type2) \
		meta_property(type2Count) \
		meta_property(type3) \
		meta_property(type3Count) \
		meta_property(m_rewardRandomRange) \
		meta_property(m_rewardUpAngle) \
		meta_property(m_minRewardUpForce) \
		meta_property(m_maxRewardUpForce) \
	}); \
	FieldEnd(EntityBigWood, PropertyOnly) \
};
