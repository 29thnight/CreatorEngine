#pragma once
#ifndef DYNAMICCPP_EXPORTS
#include "ImGuiRegister.h"
#include <imgui.h>
#include <imgui_internal.h>

class SceneRenderer;
class Matarial;
class GameObject;
class MeshRenderer;
class ModuleBehavior;
class Animator;
class PlayerInputComponent;
class TerrainComponent;
class StateMachineComponent;
class BehaviorTreeComponent;
class VolumeComponent;
class RectTransformComponent;
class DecalComponent;
class InspectorWindow
{
public:
	InspectorWindow(SceneRenderer* ptr);
	~InspectorWindow() = default;

private:
	SceneRenderer* m_sceneRenderer{ nullptr };
	bool m_openScriptPopup{ false };
	bool m_openNewScriptPopup{ false };
	bool m_openNewTagPopup{ false };
	bool m_openNewLayerPopup{ false };
	bool m_openFSMPopup{ false };
	bool m_openBTPopup{ false };

	void ImGuiDrawHelperGameObjectBaseInfo(GameObject* gameObject);
	void ImGuiDrawHelperRectTransformComponent(RectTransformComponent* rectTransformComponent);
	void ImGuiDrawHelperTransformComponent(GameObject* gameObject);
	void ImGuiDrawHelperTerrainComponent(TerrainComponent* terrainComponent);
	void ImGuiDrawHelperFSM(StateMachineComponent* FSMComponent);
	void ImGuiDrawHelperBT(BehaviorTreeComponent* BTComponent);
	void ImGuiDrawHelperVolume(VolumeComponent* volumeComponent);
	void ImGuiDrawHelperDecal(DecalComponent* decalComponent);
};
#endif // !DYNAMICCPP_EXPORTS