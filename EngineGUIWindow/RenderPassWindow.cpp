#include "RenderPassWindow.h"
#include "SceneRenderer.h"

RenderPassWindow::RenderPassWindow(SceneRenderer* ptr) : m_sceneRenderer(ptr)
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

		if (ImGui::CollapsingHeader("ToneMapPass"))
		{
			m_sceneRenderer->m_pToneMapPass->ControlPanel();
		}

		if (ImGui::CollapsingHeader("SpritePass"))
		{
			m_sceneRenderer->m_pSpritePass->ControlPanel();
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
			m_sceneRenderer->m_pWireFramePass->ControlPanel();
		}

		if (ImGui::CollapsingHeader("GridPass"))
		{
			m_sceneRenderer->m_pGridPass->ControlPanel();
		}

		ImGui::Spacing();
		if (ImGui::CollapsingHeader("PostProcessPass"))
		{
			m_sceneRenderer->m_pPostProcessingPass->ControlPanel();
		}
	});

	ImGui::GetContext("RenderPass").Close();
}
