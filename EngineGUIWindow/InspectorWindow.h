#pragma once
#include "ImGuiRegister.h"

class SceneRenderer;
class Matarial;
class MeshRenderer;
class InspectorWindow
{
public:
	InspectorWindow(SceneRenderer* ptr);
	~InspectorWindow() = default;

private:
	SceneRenderer* m_sceneRenderer{ nullptr };
	void ImGuiDrawHelperMeshRenderer(MeshRenderer* meshRenderer);
};