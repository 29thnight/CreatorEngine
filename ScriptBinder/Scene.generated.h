#pragma once

#define ReflectScene \
ReflectionField(Scene) \
{ \
	PropertyField \
	({ \
		meta_property(m_SceneObjects) \
		meta_property(m_buildIndex) \
		meta_property(m_sceneName) \
	}); \
	FieldEnd(Scene, PropertyOnly) \
};
