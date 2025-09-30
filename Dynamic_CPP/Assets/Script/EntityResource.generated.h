#pragma once

#define ReflectEntityResource \
ReflectionScriptField(EntityResource) \
{ \
	PropertyField \
	({ \
		meta_property(itemCode) \
		meta_property(itemType) \
		meta_property(maxHP) \
		meta_property(m_minRewardUpForce) \
		meta_property(m_maxRewardUpForce) \
	}); \
	FieldEnd(EntityResource, PropertyOnly) \
};
