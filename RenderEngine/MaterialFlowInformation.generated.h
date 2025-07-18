#pragma once

#define ReflectMaterialFlowInformation \
ReflectionField(MaterialFlowInformation) \
{ \
	PropertyField \
	({ \
		meta_property(m_windVector) \
		meta_property(m_uvScroll) \
	}); \
	FieldEnd(MaterialFlowInformation, PropertyOnly) \
};
