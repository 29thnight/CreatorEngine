#ifndef DYNAMICCPP_EXPORTS
#include "RenderPassWindow.h"
#include "SceneRenderer.h"
#include "GizmoRenderer.h"
#include "IconsFontAwesome6.h"
#include "fa.h"
#include "ShaderSystem.h"

RenderPassWindow::RenderPassWindow(SceneRenderer* ptr, GizmoRenderer* gizmo_ptr) : 
	m_sceneRenderer(ptr),
	m_gizmoRenderer(gizmo_ptr)
{
	ImGui::ContextRegister("RenderPass", true, [&]()
	{
		if (ImGui::Button(ICON_FA_ARROWS_ROTATE " Shader Reload", ImVec2(0, 0)))
		{
			ShaderSystem->SetReloading(true);
		}

		ImGui::Separator();
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

		if (ImGui::CollapsingHeader("DecalPass")) {
			m_sceneRenderer->m_pDecalPass->ControlPanel();
		}

		if (ImGui::CollapsingHeader("SSGIPass"))
		{
			m_sceneRenderer->m_pSSGIPass->ControlPanel();
		}

		if (ImGui::CollapsingHeader("BitMaskPass"))
		{
			m_sceneRenderer->m_pBitMaskPass->ControlPanel();
		}

		if (ImGui::CollapsingHeader("SkyBoxPass"))
		{
			m_sceneRenderer->m_pSkyBoxPass->ControlPanel();
		}

		if (ImGui::CollapsingHeader("SpritePass"))
		{
			m_sceneRenderer->m_pSpritePass->ControlPanel();
		}

		ImGui::Separator();
		if (ImGui::CollapsingHeader("PostProcessPass"))
		{
			if (ImGui::CollapsingHeader("AAPass"))
			{
				m_sceneRenderer->m_pAAPass->ControlPanel();
			}

			if (ImGui::CollapsingHeader("BloomPass"))
			{
				m_sceneRenderer->m_pPostProcessingPass->ControlPanel();
			}

			if (ImGui::CollapsingHeader("ScreenSpaceReflectionPass"))
			{
				m_sceneRenderer->m_pScreenSpaceReflectionPass->ControlPanel();
			}

			if (ImGui::CollapsingHeader("SubsurfaceScatteringPass"))
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
		}

		ImGui::Separator();
		if (ImGui::CollapsingHeader("EditorPass"))
		{
			if (ImGui::CollapsingHeader("GizmoPass"))
			{
				m_gizmoRenderer->m_pGizmoPass->ControlPanel();
			}

			if (ImGui::CollapsingHeader("GridPass"))
			{
				m_gizmoRenderer->m_pGridPass->ControlPanel();
			}
		}
	}, ImGuiWindowFlags_AlwaysVerticalScrollbar);

	ImGui::GetContext("RenderPass").Close();
}
#endif // !DYNAMICCPP_EXPORTS