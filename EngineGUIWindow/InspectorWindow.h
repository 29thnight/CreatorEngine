#pragma once
#ifndef DYNAMICCPP_EXPORTS
#include "ImGuiRegister.h"
#include <imgui.h>
#include <imgui_internal.h>

class SceneRenderer;
class Matarial;
class MeshRenderer;
class ModuleBehavior;
class Animator;
class PlayerInputComponent;
class TerrainComponent;
class StateMachineComponent;
class BehaviorTreeComponent;
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

	void ImGuiDrawHelperTerrainComponent(TerrainComponent* terrainComponent);
	void ImGuiDrawHelperFSM(StateMachineComponent* FSMComponent);
	void ImGuiDrawHelperBT(BehaviorTreeComponent* BTComponent);
};
#endif // !DYNAMICCPP_EXPORTS