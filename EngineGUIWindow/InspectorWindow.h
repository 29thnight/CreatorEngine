#pragma once
#include "ImGuiRegister.h"

class SceneRenderer;
class Matarial;
class MeshRenderer;
class ModuleBehavior;
class TerrainComponent;
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
	void ImGuiDrawHelperTerrainComponent(TerrainComponent* terrainComponent);
};