#include "GizmoRenderer.h"
// 그리드 설정 창 본문이 ImGui를 부른다. 전에는 Core.Minimal.h가 Reflection
// 사슬로 대신 끌어와 줬다 — 그 사슬을 걷으면서 직접 든다.
#include "ImGui.h"
#include "EditorWindowNames.h"
#include "Windows/EditorToolboxWindows.h"

GizmoRenderer* GizmoRenderer::s_active = nullptr;

GizmoRenderer::GizmoRenderer(RenderScene* renderScene, Camera* editorCamera) :
	m_renderScene(renderScene),
	m_pEditorCamera(editorCamera)
{
	s_active = this;

	// PHASE 21 M4 3단계: 창 프레임은 셸이 연다. 여기서는 본문만 건다 —
	// 그리드가 이 객체의 것이라 본문의 자리도 여기다.
	editor::windows::bind_window_body(EditorWindowName::kGridSettings, []()
	{
		ImGui::TextUnformatted("GridPass is owned by EnhancedRenderer (DX12).");
	});
}

GizmoRenderer::~GizmoRenderer()
{
	if (this == s_active) s_active = nullptr;

	editor::windows::unbind_window_body(EditorWindowName::kGridSettings);

	m_pEditorCamera = nullptr;
	m_renderScene = nullptr;
}

void GizmoRenderer::OnDrawGizmos()
{
	// DX11 Gizmo/Grid 패스는 dead code다. EnhancedSceneRenderer::RenderOnce가
	// BuildEnhancedGizmoSceneData를 밀봉하고 DX12 패스 체인을 실행한다.
}
