#pragma once

#define ReflectStateMachineComponent \
ReflectionFieldInheritance(StateMachineComponent, Component) \
{ \
	PropertyField \
	({ \
		meta_property(name) \
	}); \
	FieldEnd(StateMachineComponent, PropertyOnlyInheritance) \
};
