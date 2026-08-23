#pragma once
#include "Core.Minimal.h"

// 에디터 셸의 기즈모 상태 소유자(E4-4). 와이어프레임 토글과 그리드 설정 창을
// 들고, 상시 러너의 기여 노드(EditorSceneOverlayContributor의 WireFrame 술어)가
// GetActive()로 조건을 묻는다. RenderCore에는 이 타입의 참조가 없다 — 예전
// DYNAMICCPP_EXPORTS 전체 가드는 정의처가 저장소에 없는 죽은 가드라 걷었다.
class RenderScene;
class Camera;
class GizmoRenderer
{
public:
	GizmoRenderer(RenderScene* renderScene, Camera* editorCamera);
	~GizmoRenderer();
	void OnDrawGizmos();

    void EditorView();

	void SetWireFrame() { m_buseWireFrame = !m_buseWireFrame; }

	/// 와이어프레임 모드인가. DX12 상시 러너가 같은 조건으로 그리기 위해
	/// 묻는다 — 무조건 그리면 초록 와이어가 씬 전체를 덮는다(실측).
	bool IsWireFrameEnabled() const { return m_buseWireFrame; }

	/// 지금 활성 GizmoRenderer. 상시 러너의 기여 노드가 조건을 묻는 통로다 —
	/// 에디터 셸이 소유하는 객체라 렌더 쪽에서 참조할 길이 이것뿐이다.
	static GizmoRenderer* GetActive() { return s_active; }

	RenderScene* m_renderScene{};
	Camera* m_pEditorCamera{};
    bool m_bShowGridSettings{ false };

private:
    void ShowGridSettings();

private:
	static GizmoRenderer* s_active;

	bool m_buseWireFrame{ false };
};
