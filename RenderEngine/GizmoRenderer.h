#pragma once
#ifndef DYNAMICCPP_EXPORTS
#include "Core.Minimal.h"

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

	/// 지금 활성 GizmoRenderer. 상시 러너가 조건을 묻는 통로다 —
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
#endif // !DYNAMICCPP_EXPORTS
