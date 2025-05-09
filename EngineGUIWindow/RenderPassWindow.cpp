#include "RenderPassWindow.h"
#include "SceneRenderer.h"
#include "GizmoRenderer.h"

RenderPassWindow::RenderPassWindow(SceneRenderer* ptr, GizmoRenderer* gizmo_ptr) : 
	m_sceneRenderer(ptr),
	m_gizmoRenderer(gizmo_ptr)
{
	ImGui::ContextRegister("RenderPass", true, [&]()
	{
		if (ImGui::CollapsingHeader("ShadowPass"))
		{
			m_sceneRenderer->m_renderScene->m_LightController->m_shadowMapPass->ControlPanel();
		}

		if (ImGui::CollapsingHeader("SSAOPass"))
		{
			m_sceneRenderer->m_pSSAOPass->ControlPanel();
		}

		if (ImGui::CollapsingHeader("DeferredPass"))
		{
			m_sceneRenderer->m_pDeferredPass->ControlPanel();
		}

		if (ImGui::CollapsingHeader("SkyBoxPass"))
		{
			m_sceneRenderer->m_pSkyBoxPass->ControlPanel();
		}

		if(ImGui::CollapsingHeader("ScreenSpaceReflectionPass"))
		{
			m_sceneRenderer->m_pScreenSpaceReflectionPass->ControlPanel();
		}

		if(ImGui::CollapsingHeader("SubsurfaceScatteringPass"))
		{
			m_sceneRenderer->m_pSubsurfaceScatteringPass->ControlPanel();
		}

		if (ImGui::CollapsingHeader("VignettePass"))
		{
			m_sceneRenderer->m_pVignettePass->ControlPanel();
		}

		if (ImGui::CollapsingHeader("ToneMapPass"))
		{
			m_sceneRenderer->m_pToneMapPass->ControlPanel();
		}

		if (ImGui::CollapsingHeader("ColorGradingPass"))
		{
			m_sceneRenderer->m_pColorGradingPass->ControlPanel();
		}

		if (ImGui::CollapsingHeader("VolumetricFogPass"))
		{
			m_sceneRenderer->m_pVolumetricFogPass->ControlPanel();
		}

		if (ImGui::CollapsingHeader("SpritePass"))
		{
			if(ImGui::CollapsingHeader("SceneRenderer"))
			{
				m_sceneRenderer->m_pSpritePass->ControlPanel();
			}

			if (ImGui::CollapsingHeader("GizmoRenderer"))
			{
				m_gizmoRenderer->m_pGizmoPass->ControlPanel();
			}
		}

		if (ImGui::CollapsingHeader("AAPass"))
		{
			m_sceneRenderer->m_pAAPass->ControlPanel();
		}

		if (ImGui::CollapsingHeader("BlitPass"))
		{
			m_sceneRenderer->m_pBlitPass->ControlPanel();
		}

		if (ImGui::CollapsingHeader("WireFramePass"))
		{
			m_gizmoRenderer->m_pWireFramePass->ControlPanel();
		}

		if (ImGui::CollapsingHeader("GridPass"))
		{
			m_gizmoRenderer->m_pGridPass->ControlPanel();
		}

		ImGui::Spacing();
		if (ImGui::CollapsingHeader("PostProcessPass"))
		{
			m_sceneRenderer->m_pPostProcessingPass->ControlPanel();
		}
	}, ImGuiWindowFlags_AlwaysVerticalScrollbar);

	ImGui::GetContext("RenderPass").Close();
}
