#pragma once

#define ReflectTestEffect \
ReflectionScriptField(TestEffect) \
{ \
	PropertyField \
	({ \
		meta_property(moveSpeed) \
	}); \
	MethodField \
	({ \
		meta_method(Move, "dir") \
	}); \
	FieldEnd(TestEffect, PropertyAndMethod) \
};
