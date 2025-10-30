#pragma once

#define ReflectSwitchingSceneTrigger \
ReflectionScriptField(SwitchingSceneTrigger) \
{ \
	PropertyField \
	({ \
		meta_property(m_isTestMode) \
		meta_property(m_isTestBossStage) \
		meta_property(m_fadeInDuration) \
		meta_property(m_fadeOutDuration) \
		meta_property(m_autoPlayDelay) \
		meta_property(m_waitAtLastCut) \
	}); \
	FieldEnd(SwitchingSceneTrigger, PropertyOnly) \
};
