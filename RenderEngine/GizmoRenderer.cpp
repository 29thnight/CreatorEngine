#ifndef DYNAMICCPP_EXPORTS
#include "GizmoRenderer.h"
#include "SceneRenderer.h"
#include "SceneManager.h"
#include "RenderState.h"
#include "Profiler.h"

GizmoRenderer::GizmoRenderer(SceneRenderer* pRenderer) :
	m_pRenderer(pRenderer),
	m_renderScene(pRenderer->m_renderScene.get()),
	m_pEditorCamera(pRenderer->m_pEditorCamera.get())
{
#ifndef BUILD_FLAG
	m_pGridPass = std::make_unique<GridPass>();
	m_pWireFramePass = std::make_unique<WireFramePass>();
	m_pGizmoPass = std::make_unique<GizmoPass>();
	m_pGizmoLinePass = std::make_unique<GizmoLinePass>();
#endif // !BUILD_FLAG
}

GizmoRenderer::~GizmoRenderer()
{
	m_pGridPass.reset();
	m_pWireFramePass.reset();
	m_pGizmoPass.reset();
	m_pGizmoLinePass.reset();
	m_pRenderer = nullptr;
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
    m_pGridPass->GridSetting();
    ImGui::End();
#endif // !BUILD_FLAG
}

void GizmoRenderer::OnDrawGizmos()
{
#ifndef BUILD_FLAG
	if (SceneManagers->IsSceneLoading()) return;

	PROFILE_CPU_BEGIN("OnDrawGizmos");
	//[*] GridPass
	{
		PROFILE_CPU_BEGIN("GridPass");
		DirectX11::BeginEvent(L"GridPass");
		Benchmark banch;
		m_pGridPass->Execute(*m_renderScene, *m_pEditorCamera);
		RenderStatistics->UpdateRenderState("GridPass", banch.GetElapsedTime());
		DirectX11::EndEvent();
		PROFILE_CPU_END();
	}

	//[*] WireFramePass
	if (m_buseWireFrame)
	{
		PROFILE_CPU_BEGIN("WireFramePass");
		DirectX11::BeginEvent(L"WireFramePass");
		Benchmark banch;
		m_pWireFramePass->Execute(*m_renderScene, *m_pEditorCamera);
		RenderStatistics->UpdateRenderState("WireFramePass", banch.GetElapsedTime());
		DirectX11::EndEvent();
		PROFILE_CPU_END();
	}

	//[*] GizmoPass
	{
		PROFILE_CPU_BEGIN("GizmoPass");
		DirectX11::BeginEvent(L"GizmoPass");
		Benchmark banch;
		m_pGizmoPass->Execute(*m_renderScene, *m_pEditorCamera);
		RenderStatistics->UpdateRenderState("GizmoPass", banch.GetElapsedTime());
		DirectX11::EndEvent();
		PROFILE_CPU_END();
	}

	//[*] GizmoLinePass
	{
		PROFILE_CPU_BEGIN("GizmoLinePass");
		DirectX11::BeginEvent(L"GizmoLinePass");
		Benchmark banch;
		m_pGizmoLinePass->Execute(*m_renderScene, *m_pEditorCamera);
		RenderStatistics->UpdateRenderState("GizmoLinePass", banch.GetElapsedTime());
		DirectX11::EndEvent();
		PROFILE_CPU_END();
	}
	PROFILE_CPU_END();
#endif
}
#endif // !DYNAMICCPP_EXPORTS