#pragma once
#ifndef DYNAMICCPP_EXPORTS
#include "Core.Minimal.h"
#include "DeviceResources.h"
#include "GridPass.h"
#include "GizmoPass.h"
#include "GizmoLinePass.h"
#include "WireFramePass.h"
#include "SpritePass.h"

class RenderPassWindow;
class SceneRenderer;
class GizmoRenderer
{
private:
	friend class RenderPassWindow;
public:
	GizmoRenderer(SceneRenderer* pRenderer);
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

	SceneRenderer* m_pRenderer{ nullptr };
	std::unique_ptr<GizmoPass>      m_pGizmoPass{};
	std::unique_ptr<WireFramePass>  m_pWireFramePass{};
	std::unique_ptr<GridPass>       m_pGridPass{};
	std::unique_ptr<GizmoLinePass>  m_pGizmoLinePass{};

	bool m_buseWireFrame{ false };
};
#endif // !DYNAMICCPP_EXPORTS
