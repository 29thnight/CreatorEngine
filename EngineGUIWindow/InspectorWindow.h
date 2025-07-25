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
	void ImGuiDrawHelperMeshRenderer(MeshRenderer* meshRenderer);
	void ImGuiDrawHelperModuleBehavior(ModuleBehavior* moduleBehavior);
	void ImGuiDrawHelperAnimator(Animator* animator);

	void ImGuiDrawHelperPlayerInput(PlayerInputComponent* playerInput);
	//void DrawMyLink(std::string linkName, std::string from, std::string to);
	void ImGuiDrawHelperTerrainComponent(TerrainComponent* terrainComponent);

	bool m_openFSMPopup{ false };
	void ImGuiDrawHelperFSM(StateMachineComponent* FSMComponent);
	bool m_openBTPopup{ false };
	void ImGuiDrawHelperBT(BehaviorTreeComponent* BTComponent);

	void ImguiDrawLuaScriptPopup();


	//node editor combo
	bool BeginNodeCombo(const char* label, const char* preview_value, ImGuiComboFlags flags = 0);
	void EndNodeCombo();

};
#endif // !DYNAMICCPP_EXPORTS