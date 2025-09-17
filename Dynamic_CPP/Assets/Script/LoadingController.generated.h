#pragma once

#define ReflectLoadingController \
ReflectionScriptField(LoadingController) \
{ \
	PropertyField \
	({ \
		meta_property(m_rotateDegree) \
	}); \
	FieldEnd(LoadingController, PropertyOnly) \
};
