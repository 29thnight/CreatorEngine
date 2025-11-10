#pragma once

#define ReflectMaterialManager \
ReflectionScriptField(MaterialManager) \
{ \
	PropertyField \
	({ \
		meta_property(leftPosition) \
		meta_property(rightPosition) \
		meta_property(leftColor) \
		meta_property(rightColor) \
	}); \
	MethodField \
	({ \
		meta_method(SetGradationMaterials) \
	}); \
	FieldEnd(MaterialManager, PropertyAndMethod) \
};
