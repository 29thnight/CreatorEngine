#pragma once

#define ReflectBehaviorTreeComponent \
ReflectionFieldInheritance(BehaviorTreeComponent, Component) \
{ \
	PropertyField \
	({ \
		meta_property(name) \
	}); \
	FieldEnd(BehaviorTreeComponent, PropertyOnlyInheritance) \
};
