#include "InspectorWindow.h"
#include "SceneRenderer.h"
#include "RenderScene.h"
#include "Scene.h"
#include "Object.h"
#include "GameObject.h"
#include "Model.h"
#include "ImageComponent.h"
#include "UIManager.h"
#include "DataSystem.h"
#include "PathFinder.h"
#include "Transform.h"

InspectorWindow::InspectorWindow(SceneRenderer* ptr) :
	m_sceneRenderer(ptr)
{
	ImGui::ContextRegister("Inspector", [&]()
	{
		ImGui::BringWindowToDisplayBack(ImGui::GetCurrentWindow());

		Scene* scene = nullptr;
		RenderScene* renderScene = nullptr;
		GameObject* selectedSceneObject = nullptr;

		if (m_sceneRenderer)
		{
			scene = m_sceneRenderer->m_currentScene;
			renderScene = m_sceneRenderer->m_renderScene;
			selectedSceneObject = renderScene->m_selectedSceneObject;

			if (!scene && !renderScene)
			{
				ImGui::Text("Not Init InspectorWindow");
				ImGui::End();
				return;
			}
		}

		if (scene && selectedSceneObject)
		{
			std::string name = selectedSceneObject->m_name.ToString();
			if (ImGui::InputText("name",
				&name[0],
				name.capacity() + 1,
				ImGuiInputTextFlags_CallbackResize | ImGuiInputTextFlags_EnterReturnsTrue,
				Meta::InputTextCallback,
				static_cast<void*>(&name)))
			{
				selectedSceneObject->m_name.SetString(name);
			}
			Mathf::Vector4 position = selectedSceneObject->m_transform.position;
			Mathf::Vector4 rotation = selectedSceneObject->m_transform.rotation;
			Mathf::Vector4 scale = selectedSceneObject->m_transform.scale;

			float pyr[3]; // pitch yaw roll
			Mathf::QuaternionToEular(rotation, pyr[0], pyr[1], pyr[2]);

			for (float& i : pyr)
			{
				i *= Mathf::Rad2Deg;
			}

			if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen))
			{
				ImGui::Text("Position ");
				ImGui::SameLine();
				ImGui::DragFloat3("##Position", &position.x, 0.08f, -1000, 1000);
				ImGui::Text("Rotation");
				ImGui::SameLine();
				ImGui::DragFloat3("##Rotation", &pyr[0], 0.1f);
				ImGui::Text("Scale     ");
				ImGui::SameLine();
				ImGui::DragFloat3("##Scale", &scale.x, 0.1f, 10);

				{
					for (float& i : pyr)
					{
						i *= Mathf::Deg2Rad;
					}

					rotation = XMQuaternionRotationRollPitchYaw(pyr[0], pyr[1], pyr[2]);

					Mathf::Vector4 prevPosition = selectedSceneObject->m_transform.position;
					Mathf::Vector4 prevRotation = selectedSceneObject->m_transform.rotation;
					Mathf::Vector4 prevScale = selectedSceneObject->m_transform.scale;

					if (prevScale != scale)
					{
						Meta::MakeCustomChangeCommand([=] 
							{
								selectedSceneObject->m_transform.scale = prevScale;
								selectedSceneObject->m_transform.m_dirty = true;
							},
							[=]
							{
								selectedSceneObject->m_transform.scale = scale;
								selectedSceneObject->m_transform.m_dirty = true;
							});
					}

					if (prevRotation != rotation)
					{
						Meta::MakeCustomChangeCommand([=]
							{
								selectedSceneObject->m_transform.rotation = prevRotation;
								selectedSceneObject->m_transform.m_dirty = true;
							},
							[=]
							{
								selectedSceneObject->m_transform.rotation = rotation;
								selectedSceneObject->m_transform.m_dirty = true;
							});
					}

					if (prevPosition != position)
					{
						Meta::MakeCustomChangeCommand([=]
							{
								selectedSceneObject->m_transform.position = prevPosition;
								selectedSceneObject->m_transform.m_dirty = true;
							},
							[=]
							{
								selectedSceneObject->m_transform.position = position;
								selectedSceneObject->m_transform.m_dirty = true;
							});
					}
					selectedSceneObject->m_transform.GetLocalMatrix();
				}
			}

			for (auto& component : selectedSceneObject->m_components)
			{
				const auto& type = Meta::Find(component->ToString());
				if (!type) continue;

				if (ImGui::CollapsingHeader(component->ToString().c_str(), ImGuiTreeNodeFlags_DefaultOpen));
				{
					Meta::DrawObject(component.get(), *type);
				}
			}
		}
	}, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoFocusOnAppearing);
}
