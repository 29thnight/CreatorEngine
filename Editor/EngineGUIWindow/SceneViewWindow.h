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

// 씬뷰 피킹의 순수 부분. 창 상태도 ImGui 프레임도 읽지 않으므로 자가 검사가
// 직접 부를 수 있다(editor.selftest — EditorThemeSelfTest.cpp). 셋을 여기로
// 끌어낸 이유는 셋 다 2026-09-14 사고의 당사자인데, 멤버 함수로 두면 프레임
// 루프를 세우지 않고는 한 줄도 잴 수 없기 때문이다.
namespace editor::picking
{
	/// 슬롯맵에서 점유자만 골라낸다. Scene::m_Entities 는 AllocateSlot 이
	/// nullptr 로 늘리고 ReleaseSlot 이 파괴된 슬롯을 비운 채 재사용 전까지
	/// 남기는 **구멍 있는 배열**이다 — 이 함수가 그 구멍을 걷어내는 유일한
	/// 자리이고, 그래서 피킹 순회는 널을 볼 일이 없다.
	///
	/// 널을 건너뛰는 것을 "포인터 목록을 돌려준다" 로 표현한 것은 검사 때문이다.
	/// 순회 안에 `if (!slot) continue` 로 두면 그것을 빼는 변이가 곧바로
	/// 프로세스를 죽여 무엇이 틀렸는지 남지 않는다. 목록으로 돌려주면 같은
	/// 변이가 **널이 섞인 목록**이 되어 검사가 값으로 잡아낸다.
	std::vector<Entity*> CollectOccupants(
		const std::vector<std::unique_ptr<Entity>>& slots);

	/// 레이가 맞힌 것들을 가까운 순으로 모은다.
	std::vector<RayHitResult> GatherRayHits(
		const Ray& ray, const std::vector<std::unique_ptr<Entity>>& slots);

	/// 겹친 것들 사이의 순환 인덱스를 정한다. 이번 클릭이 맞힌 더미의 신원이
	/// 직전과 같으면 뒤엣것으로 넘어가고, 달라졌으면 맨 앞으로 되돌린다.
	/// `lastIdentity` 와 `cursor` 는 호출자가 들고 있는 상태이며 갱신된다.
	/// 반환값은 이번에 고를 `identity`/hit 목록의 인덱스다.
	std::size_t AdvanceCycleIndex(
		const std::vector<EntityHandle>& identity,
		std::vector<EntityHandle>& lastIdentity,
		std::size_t& cursor);
}

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
