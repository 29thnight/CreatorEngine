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
#include "ComponentFactory.h"

#include "IconsFontAwesome6.h"
#include "fa.h"

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
					if(component->GetTypeID() == TypeTrait::GUIDCreator::GetTypeID<MeshRenderer>())
					{
						MeshRenderer* meshRenderer = dynamic_cast<MeshRenderer*>(component.get());
						if (nullptr != meshRenderer)
						{
							ImGuiDrawHelperMeshRenderer(meshRenderer);
						}
					}
					else
					{
						Meta::DrawObject(component.get(), *type);
					}
				}
			}
			
			ImGui::Separator();
			if (ImGui::Button("Add Component"))
			{
				ImGui::OpenPopup("AddComponent");
			}

			if (ImGui::BeginPopup("AddComponent"))
			{
				for (const auto& [type_name, type] : ComponentFactorys->m_componentTypes)
				{
					if (ImGui::MenuItem(type_name.c_str()))
					{
						auto component = selectedSceneObject->AddComponent(*type);
					}
				}
				ImGui::EndPopup();
			}
		}
	}, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoFocusOnAppearing);
}

void InspectorWindow::ImGuiDrawHelperMeshRenderer(MeshRenderer* meshRenderer)
{
	if (meshRenderer->m_Material)
	{
		if (ImGui::CollapsingHeader("Material", ImGuiTreeNodeFlags_DefaultOpen))
		{
			ImGui::Text("Element ");
			ImGui::SameLine();
			ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(1.1f, 5.1f));
			ImGui::PushStyleVar(ImGuiStyleVar_ButtonTextAlign, ImVec2(0.0f, 0.5f));
			ImGui::Button(meshRenderer->m_Material->m_name.c_str(), ImVec2(250, 0));
			ImGui::SameLine();
			if (ImGui::Button(ICON_FA_BOX))
			{
				ImGui::GetContext("SelectMatarial").Open();
			}
			ImGui::PopStyleVar(2);
		}
		if (ImGui::CollapsingHeader("MaterialInfo", ImGuiTreeNodeFlags_DefaultOpen))
		{
			const auto& mat_type = Meta::Find("Material");
			const auto& mat_info_type = Meta::Find("MaterialInfomation");
			Meta::DrawProperties(&meshRenderer->m_Material->m_materialInfo, *mat_info_type);
			const auto& mat_render_type = Meta::FindEnum("MaterialRenderingMode");
			for (auto& enumProp : mat_type->properties)
			{
				if (enumProp.typeID == TypeTrait::GUIDCreator::GetTypeID<MaterialRenderingMode>())
				{
					Meta::DrawEnumProperty((int*)&meshRenderer->m_Material->m_renderingMode, mat_render_type, enumProp);
					break;
				}
			}
		}

		if (DataSystems->m_trasfarMaterial)
		{
			std::string name = meshRenderer->m_Material->m_name;
			Meta::MakeCustomChangeCommand(
				[=]
				{
					meshRenderer->m_Material = DataSystems->Materials[name].get();
				},
				[=]
				{
					meshRenderer->m_Material = DataSystems->m_trasfarMaterial;
				}
			);

			meshRenderer->m_Material = DataSystems->m_trasfarMaterial;
			DataSystems->m_trasfarMaterial = nullptr;
		}
	}
	else
	{
		ImGui::Text("No Material");
	}
}
