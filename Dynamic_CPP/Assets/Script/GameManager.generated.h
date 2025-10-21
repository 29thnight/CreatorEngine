#pragma once

#define ReflectGameManager \
ReflectionScriptField(GameManager) \
{ \
	PropertyField \
	({ \
		meta_property(m_prevSceneIndex) \
		meta_property(m_nextSceneIndex) \
		meta_property(m_isTestReward) \
	}); \
	MethodField \
	({ \
		meta_method(LoadNextScene) \
		meta_method(SwitchNextScene) \
		meta_method(SwitchNextSceneWithFade) \
		meta_method(LoadImidiateNextScene) \
		meta_method(ApplyGlobalEnhancementsToAllPlayers) \
	}); \
	FieldEnd(GameManager, PropertyAndMethod) \
};
