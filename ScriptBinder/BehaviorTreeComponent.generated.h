#pragma once

#define ReflectBehaviorTreeComponent \
ReflectionFieldInheritance(BehaviorTreeComponent, Component) \
{ \
	PropertyField \
	({ \
		meta_property(name) \
		meta_property(m_BehaviorTreeGuid) \
		meta_property(m_BlackBoardGuid) \
	}); \
	FieldEnd(BehaviorTreeComponent, PropertyOnlyInheritance) \
};
