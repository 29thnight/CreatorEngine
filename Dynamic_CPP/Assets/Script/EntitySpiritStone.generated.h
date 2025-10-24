#pragma once

#define ReflectEntitySpiritStone \
ReflectionScriptFieldInheritance(EntitySpiritStone, Entity) \
{ \
	PropertyField \
	({ \
		meta_property(m_stoneReward) \
		meta_property(maxHP) \
	}); \
	FieldEnd(EntitySpiritStone, PropertyOnlyScriptInheritance) \
};
