#pragma once
#include "ImGui.h"
#include "SceneViewportOverlay.h"
#include "EditorViewportCanvas.h"
#include "EntityHandle.h"
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
	math::vector3 ConvertMouseToWorldPosition(Camera* cam, const ImVec2& mouseScreenPos, float depth);
	Ray CreateRayFromCamera(Camera* cam, const ImVec2& mousePos);
	//[[deprecated("Soon Deleted")]]
	Entity* PickObjectFromRay(const Ray& ray, const std::vector<std::unique_ptr<Entity>>& sceneObjects);
	
	std::vector<RayHitResult> PickObjectsFromRay(const Ray& ray, const std::vector<std::unique_ptr<Entity>>& sceneObjects);
	
private:
	Camera* m_editorCamera{ nullptr };
	editor::SceneViewportOverlay m_overlay;
    editor::ViewportCanvas m_canvas;
	EditorCameraRig* m_editorCameraRig{ nullptr };
	GizmoRenderer* m_gizmoRenderer{ nullptr };

	std::vector<RayHitResult> m_hitResults;
	// 직전 클릭이 맞힌 더미의 신원. 이것이 그대로면 다음 클릭은 뒤에 있는
	// 것으로 넘어가고(겹침 순환), 달라지면 인덱스를 0으로 되돌린다. 포인터가
	// 아니라 핸들인 이유는 .cpp 의 채우는 자리 주석 참고.
	std::vector<EntityHandle> m_hitCycleIdentity;
	size_t m_currentHitIndex = 0;
};
