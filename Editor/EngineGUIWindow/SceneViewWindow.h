#pragma once
#include "ImGuiRegister.h"
#include <mathematics/vector3.hpp>
#include <memory>
#include <vector>

class GizmoRenderer;
class Entity;
class Camera;
class EditorCameraRig;
struct Ray { math::vector3 origin, direction; };
struct RayHitResult
{
	Entity* object;
	float distance;
};

class SceneViewWindow
{
public:
	SceneViewWindow(EditorCameraRig* editorCameraRig, GizmoRenderer* gizmo_ptr);
	~SceneViewWindow() = default;

	void RenderSceneViewWindow();
private:
	void RenderSceneView(float* cameraView, float* cameraProjection, float* matrix, bool editTransformDecomposition, Entity* obj, Camera* cam);
	math::vector3 ConvertMouseToWorldPosition(Camera* cam, const ImVec2& mouseScreenPos, const ImVec2& imagePos, const ImVec2& imageSize, float depth = 0.0f);
	Ray CreateRayFromCamera(Camera* cam, const ImVec2& mousePos, const ImVec2& imagePos, const ImVec2& imageSize);
	//[[deprecated("Soon Deleted")]]
	Entity* PickObjectFromRay(const Ray& ray, const std::vector<std::unique_ptr<Entity>>& sceneObjects);
	
	std::vector<RayHitResult> PickObjectsFromRay(const Ray& ray, const std::vector<std::unique_ptr<Entity>>& sceneObjects);
	
private:
	Camera* m_editorCamera{ nullptr };
	EditorCameraRig* m_editorCameraRig{ nullptr };
	GizmoRenderer* m_gizmoRenderer{ nullptr };

	std::vector<RayHitResult> m_hitResults;
	size_t m_currentHitIndex = 0;
};
