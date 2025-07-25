#pragma once

#define ReflectTestBehavior \
ReflectionScriptField(TestBehavior) \
{ \
	PropertyField \
	({ \
		meta_property(testValue) \
		meta_property(testString) \
	}); \
	MethodField \
	({ \
		meta_method(Test) \
	}); \
	FieldEnd(TestBehavior, PropertyAndMethod) \
};
