#pragma once
#include "ImGuiRegister.h"

class SceneRenderer;
class GameObject;
class Camera;
class SceneViewWindow
{
public:
	SceneViewWindow(SceneRenderer* ptr);
	~SceneViewWindow() = default;

	void RenderSceneViewWindow();
private:
	void RenderSceneView(float* cameraView, float* cameraProjection, float* matrix, bool editTransformDecomposition, GameObject* obj, Camera* cam);

	SceneRenderer* m_sceneRenderer{ nullptr };
};