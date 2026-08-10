#include "ShaderSelectionWindow.h"
#ifndef DYNAMICCPP_EXPORTS
#include "ShaderSystem.h"
#include "ShaderPSO.h"
#include "Material.h"
#include "ImageComponent.h"
#include "ImGuiRegister.h"

namespace
{
	// 고른 결과를 받을 대상. 창이 하나씩뿐이라 상태도 하나씩이다.
	Material*       g_shaderTarget{ nullptr };
	ImageComponent* g_imageTarget{ nullptr };
}

void ShaderSelectionWindow::SetShaderTarget(Material* material)
{
	g_shaderTarget = material;
}

void ShaderSelectionWindow::SetImageTarget(ImageComponent* image)
{
	g_imageTarget = image;
}

void ShaderSelectionWindow::Register()
{
	ImGui::ContextRegister("SelectShader", true, []() {
		ImGui::Text("Select Shader");
		if (ImGui::BeginListBox("##ShaderList"))
		{
			if (ImGui::Selectable("None"))
			{
				if (g_shaderTarget)
				{
					g_shaderTarget->SetShaderPSO(nullptr);
					g_shaderTarget = nullptr;
				}
				ImGui::GetContext("SelectShader").Close();
			}
			for (auto& [name, pso] : ShaderSystem->ShaderAssets)
			{
				if (ImGui::Selectable(name.c_str()))
				{
					if (g_shaderTarget)
					{
						g_shaderTarget->SetShaderPSO(pso);
						g_shaderTarget = nullptr;
					}
					ImGui::GetContext("SelectShader").Close();
				}
			}
			ImGui::EndListBox();
		}
		});
	ImGui::GetContext("SelectShader").Close();

	ImGui::ContextRegister("SelectImageCustomShader", true, []() {
		ImGui::Text("Select PixelShader");
		if (ImGui::BeginListBox("##PixelShaderList"))
		{
			for (auto& [name, shader] : ShaderSystem->PixelShaders)
			{
				if (ImGui::Selectable(name.c_str()))
				{
					if (g_imageTarget)
					{
						g_imageTarget->SetCustomPixelShader(name);
						g_imageTarget = nullptr;
					}
					ImGui::GetContext("SelectImageCustomShader").Close();
				}
			}
			ImGui::EndListBox();
		}
		});
	ImGui::GetContext("SelectImageCustomShader").Close();
}

#endif // !DYNAMICCPP_EXPORTS
