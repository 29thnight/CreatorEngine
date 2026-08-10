#ifndef DYNAMICCPP_EXPORTS
#include "GizmoRenderer.h"
// ShowGridSettings가 ImGui를 부른다. 전에는 Core.Minimal.h가 Reflection 사슬로
// 대신 끌어와 줬다 — 그 사슬을 걷으면서 직접 든다.
#include "ImGuiRegister.h"

GizmoRenderer* GizmoRenderer::s_active = nullptr;

GizmoRenderer::GizmoRenderer(RenderScene* renderScene, Camera* editorCamera) :
	m_renderScene(renderScene),
	m_pEditorCamera(editorCamera)
{
	s_active = this;
}

GizmoRenderer::~GizmoRenderer()
{
	if (this == s_active) s_active = nullptr;

	m_pEditorCamera = nullptr;
	m_renderScene = nullptr;
}

void GizmoRenderer::EditorView()
{
    if (m_bShowGridSettings)
    {
        ShowGridSettings();
    }
}

void GizmoRenderer::ShowGridSettings()
{
#ifndef BUILD_FLAG
    ImGui::Begin("Grid Settings", &m_bShowGridSettings, ImGuiWindowFlags_AlwaysAutoResize);
    ImGui::TextUnformatted("GridPass is owned by EnhancedRenderer (DX12).");
    ImGui::End();
#endif // !BUILD_FLAG
}

void GizmoRenderer::OnDrawGizmos()
{
	// DX11 Gizmo/Grid 패스는 dead code다. EnhancedSceneRenderer::RenderOnce가
	// BuildEnhancedGizmoSceneData를 밀봉하고 DX12 패스 체인을 실행한다.
}
#endif // !DYNAMICCPP_EXPORTS
