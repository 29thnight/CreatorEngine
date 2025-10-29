#pragma once

#define ReflectSwitchingSceneTrigger \
ReflectionScriptField(SwitchingSceneTrigger) \
{ \
	PropertyField \
	({ \
		meta_property(m_isTestMode) \
		meta_property(m_fadeInDuration) \
		meta_property(m_fadeOutDuration) \
	}); \
	FieldEnd(SwitchingSceneTrigger, PropertyOnly) \
};
