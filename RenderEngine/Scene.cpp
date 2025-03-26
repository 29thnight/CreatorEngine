#include "Scene.h"
#include "ImGuiRegister.h"

Scene::Scene()
{
	CreateSceneObject("Root", 0);
	m_MainCamera.RegisterContainer();
	//m_MainCamera.m_applyRenderPipelinePass.m_BlitPass = true;
    EditorSceneObjectHierarchy();
    EditorSceneObjectInspector();
}

Scene::~Scene()
{
	//TODO : ComPtr이라 자동 해제 -> default로 변경할 것
    ImGui::ContextUnregister("SceneObject Hierarchy");
    ImGui::ContextUnregister("SceneObject Inspector");
}

std::shared_ptr<SceneObject> Scene::AddSceneObject(const std::shared_ptr<SceneObject>& sceneObject)
{
	m_SceneObjects.push_back(sceneObject);

    const_cast<SceneObject::Index&>(sceneObject->m_index) = m_SceneObjects.size() - 1;

	m_SceneObjects[0]->m_childrenIndices.push_back(sceneObject->m_index);

	return sceneObject;
}

std::shared_ptr<SceneObject> Scene::CreateSceneObject(const std::string_view& name, SceneObject::Index parentIndex)
{
    SceneObject::Index index = m_SceneObjects.size();

	m_SceneObjects.push_back(std::make_shared<SceneObject>(name, index, parentIndex));
	auto parentObj = GetSceneObject(parentIndex);
	if(parentObj->m_index != index)
	{
		parentObj->m_childrenIndices.push_back(index);
	}
	return m_SceneObjects[index];
}

std::shared_ptr<SceneObject> Scene::GetSceneObject(SceneObject::Index index)
{
	if (index < m_SceneObjects.size())
	{
		return m_SceneObjects[index];
	}
	return m_SceneObjects[0];
}

void Scene::SetBuffers(ID3D11Buffer* modelBuffer)
{
	m_ModelBuffer = modelBuffer;
}

void Scene::Update(float deltaSecond)
{
	m_animationJob.Update(*this, deltaSecond);

	for (auto& objIndex : m_SceneObjects[0]->m_childrenIndices)
	{
		UpdateModelRecursive(objIndex, XMMatrixIdentity());
	}
}

void Scene::ShadowStage(Camera& camera)
{
	m_LightController.SetEyePosition(m_MainCamera.m_eyePosition);
	m_LightController.Update();
	m_LightController.RenderAnyShadowMap(*this, camera);
}

void Scene::UseModel()
{
	DirectX11::VSSetConstantBuffer(0, 1, &m_ModelBuffer);
}

void Scene::UpdateModel(const Mathf::xMatrix& model)
{
	DirectX11::UpdateBuffer(m_ModelBuffer, &model);
}

void Scene::EditorSceneObjectHierarchy()
{
	ImGui::ContextRegister("SceneObject Hierarchy", [&]()
	{
		ImGui::BringWindowToDisplayBack(ImGui::GetCurrentWindow());

		for (auto& obj : m_SceneObjects)
		{
			if (obj->m_index == 0 || obj->m_parentIndex > 0) continue;

			ImGui::PushID((int)&obj);
			DrawSceneObject(obj);
			ImGui::PopID();
		}
	},ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoFocusOnAppearing);
}

void Scene::EditorSceneObjectInspector()
{
	ImGui::ContextRegister("SceneObject Inspector", [&]()
	{
		ImGui::BringWindowToDisplayBack(ImGui::GetCurrentWindow());

		if (m_selectedSceneObject)
		{
			Mathf::Vector4 position = m_selectedSceneObject->m_transform.position;
			Mathf::Vector4 rotation = m_selectedSceneObject->m_transform.rotation;
			Mathf::Vector4 scale = m_selectedSceneObject->m_transform.scale;

			float pyr[3]; // pitch yaw roll
			Mathf::QuaternionToEular(rotation, pyr[0], pyr[1], pyr[2]);

			for (int i = 0; i < 3; i++)
            {
				pyr[i] = XMConvertToDegrees(pyr[i]);
			}

			ImGui::Text(m_selectedSceneObject->m_name.c_str());
			ImGui::Separator();
			ImGui::Text("Position");	
			ImGui::DragFloat3("##Position", &position.x, 0.08f, -1000, 1000);
			ImGui::Text("Rotation");
			ImGui::DragFloat3("##Rotation", &pyr[0], 0.1f);
			ImGui::Text("Scale");
			ImGui::DragFloat3("##Scale", &scale.x, 0.1f, 10);
			ImGui::Text("Index");
			ImGui::InputInt("##Index", const_cast<int*>(&m_selectedSceneObject->m_index), 0, 0, ImGuiInputTextFlags_ReadOnly);
			ImGui::Text("Parent Index");
			ImGui::InputInt("##ParentIndex", const_cast<int*>(&m_selectedSceneObject->m_parentIndex), 0, 0, ImGuiInputTextFlags_ReadOnly);

			for (int i = 0; i < 3; i++)
            {
				pyr[i] = XMConvertToRadians(pyr[i]);
			}

			rotation = XMQuaternionRotationRollPitchYaw(pyr[0], pyr[1], pyr[2]);

			m_selectedSceneObject->m_transform.position = position;
			m_selectedSceneObject->m_transform.rotation = rotation;
			m_selectedSceneObject->m_transform.scale = scale;
			m_selectedSceneObject->m_transform.m_dirty = true;

			m_selectedSceneObject->m_transform.GetLocalMatrix();
		}
	}, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoFocusOnAppearing);
}

void Scene::UpdateModelRecursive(SceneObject::Index objIndex, Mathf::xMatrix model)
{
	auto obj = GetSceneObject(objIndex);
	model = XMMatrixMultiply(obj->m_transform.GetLocalMatrix(), model);
	obj->m_transform.SetAndDecomposeMatrix(model);
	for (auto& childIndex : obj->m_childrenIndices)
	{
		UpdateModelRecursive(childIndex, model);
	}
}

void Scene::DrawSceneObject(const std::shared_ptr<SceneObject>& obj)
{
	ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick;
	if (obj.get() == m_selectedSceneObject)
		flags |= ImGuiTreeNodeFlags_Selected;

	bool opened = ImGui::TreeNodeEx(obj->m_name.c_str(), flags);

	if (ImGui::IsItemClicked())
		m_selectedSceneObject = obj.get();

	if (opened)
	{
		// 자식 노드를 재귀적으로 그리기
		for (auto childIndex : obj->m_childrenIndices)
		{
			auto child = GetSceneObject(childIndex);
			DrawSceneObject(child);
		}
		ImGui::TreePop();
	}
}
