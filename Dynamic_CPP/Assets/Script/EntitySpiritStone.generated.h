#pragma once

#define ReflectEntitySpiritStone \
ReflectionScriptFieldInheritance(EntitySpiritStone, Entity) \
{ \
	PropertyField \
	({ \
		meta_property(m_stoneReward) \
	}); \
	FieldEnd(EntitySpiritStone, PropertyOnlyScriptInheritance) \
};
