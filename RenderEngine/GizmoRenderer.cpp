#include "GizmoRenderer.h"
#include "SceneRenderer.h"
#include "RenderScene.h"
#include "RenderState.h"

GizmoRenderer::GizmoRenderer(SceneRenderer* pRenderer) :
	m_renderScene(pRenderer->m_renderScene),
	m_pEditorCamera(pRenderer->m_pEditorCamera.get())
{
	m_pGridPass = std::make_unique<GridPass>();
	m_pWireFramePass = std::make_unique<WireFramePass>();
	m_pGizmoPass = std::make_unique<GizmoPass>();
	m_pGizmoLinePass = std::make_unique<GizmoLinePass>();
}

GizmoRenderer::~GizmoRenderer()
{
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
    ImGui::Begin("Grid Settings", &m_bShowGridSettings, ImGuiWindowFlags_AlwaysAutoResize);
    m_pGridPass->GridSetting();
    ImGui::End();
}

void GizmoRenderer::OnDrawGizmos()
{
	//[*] GridPass
	{
		DirectX11::BeginEvent(L"GridPass");
		Benchmark banch;
		m_pGridPass->Execute(*m_renderScene, *m_pEditorCamera);
		RenderStatistics->UpdateRenderState("GridPass", banch.GetElapsedTime());
		DirectX11::EndEvent();
	}

	//[*] WireFramePass
	if (m_buseWireFrame)
	{
		DirectX11::BeginEvent(L"WireFramePass");
		Benchmark banch;
		m_pWireFramePass->Execute(*m_renderScene, *m_pEditorCamera);
		RenderStatistics->UpdateRenderState("WireFramePass", banch.GetElapsedTime());
		DirectX11::EndEvent();
	}

	//[*] GizmoPass
	{
		DirectX11::BeginEvent(L"GizmoPass");
		Benchmark banch;
		m_pGizmoPass->Execute(*m_renderScene, *m_pEditorCamera);
		RenderStatistics->UpdateRenderState("GizmoPass", banch.GetElapsedTime());
		DirectX11::EndEvent();
	}

	//[*] GizmoLinePass
	{
		DirectX11::BeginEvent(L"GizmoLinePass");
		Benchmark banch;
		m_pGizmoLinePass->Execute(*m_renderScene, *m_pEditorCamera);
		RenderStatistics->UpdateRenderState("GizmoLinePass", banch.GetElapsedTime());
		DirectX11::EndEvent();
	}
}
