#pragma once

#define ReflectGameManager \
ReflectionScriptField(GameManager) \
{ \
	PropertyField \
	({ \
		meta_property(m_isTestReward) \
		meta_property(m_prevSceneIndex) \
		meta_property(m_nextSceneIndex) \
	}); \
	MethodField \
	({ \
		meta_method(LoadPrevScene) \
		meta_method(SwitchPrevScene) \
		meta_method(LoadNextScene) \
		meta_method(SwitchNextScene) \
		meta_method(LoadImidiateNextScene) \
		meta_method(ApplyGlobalEnhancementsToAllPlayers) \
	}); \
	FieldEnd(GameManager, PropertyAndMethod) \
};
