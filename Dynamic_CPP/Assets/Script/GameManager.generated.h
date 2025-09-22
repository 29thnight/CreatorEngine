#pragma once

#define ReflectGameManager \
ReflectionScriptField(GameManager) \
{ \
	PropertyField \
	({ \
		meta_property(m_isTestReward) \
		meta_property(m_nextSceneName) \
	}); \
	MethodField \
	({ \
		meta_method(LoadTestScene) \
		meta_method(SwitchTestScene) \
		meta_method(LoadNextScene) \
		meta_method(SwitchNextScene) \
		meta_method(LoadImidiateNextScene) \
	}); \
	FieldEnd(GameManager, PropertyAndMethod) \
};
