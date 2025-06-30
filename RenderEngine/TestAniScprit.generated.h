#pragma once

#define ReflectTestAniScprit \
ReflectionFieldInheritance(TestAniScprit, Component) \
{ \
	MethodField \
	({ \
		meta_method(OnPunch) \
		meta_method(Moving) \
	}); \
	FieldEnd(TestAniScprit, MethodOnlyInheritance) \
};
