#pragma once
#include "ImGui.h"
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

// PHASE 21 W3: 소유자가 없다. 이 클래스가 드는 것은 UI 지역 상태뿐이라
// (히트 결과 둘, 나머지 셋은 남의 것을 가리키는 포인터) 수명을 따로
// 관리할 이유가 없었다. 본문은 자유 함수
// `editor::windows::draw_scene_view` 이고, 상태는 그 짝인 `.cpp` 의
// 익명 이름공간이 든다.
//
// 빌린 포인터 셋은 생성자 인자로 받던 것을 매 프레임 다시 유도한다 —
// 카메라 리그는 `EditorSessionState`, 기즈모는 `GizmoRenderer::GetActive()`
// 가 정본이다. 캐시보다 옳다. 리그나 기즈모가 다시 서면 옛 값이 남지 않는다.
class SceneViewWindow
{
public:
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
