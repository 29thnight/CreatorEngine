#pragma once

#define ReflectSceneTag \
ReflectionScriptField(SceneTag) \
{ \
	PropertyField \
	({ \
		meta_property(m_sceneTag) \
	}); \
	FieldEnd(SceneTag, PropertyOnly) \
};
