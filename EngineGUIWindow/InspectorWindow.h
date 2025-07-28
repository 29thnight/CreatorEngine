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
	void ImguiDrawLuaScriptPopup();


	//node editor combo
	bool BeginNodeCombo(const char* label, const char* preview_value, ImGuiComboFlags flags = 0);
	void EndNodeCombo();

};
#endif // !DYNAMICCPP_EXPORTS