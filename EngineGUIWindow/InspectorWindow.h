#pragma once
#include "ImGuiRegister.h"
#include <imgui.h>
#include <imgui_internal.h>

class SceneRenderer;
class Matarial;
class MeshRenderer;
class ModuleBehavior;
class Animator;

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

};
