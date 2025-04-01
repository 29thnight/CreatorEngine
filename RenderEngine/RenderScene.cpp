#include "RenderScene.h"
#include "ImGuiRegister.h"
#include "../ScriptBinder/Scene.h"
#include "LightProperty.h"
#include "../ScriptBinder/Renderer.h"
#include "Skeleton.h"
#include "Light.h"
#include "Banchmark.hpp"
#include "TimeSystem.h"

RenderScene::RenderScene()
{
}

RenderScene::~RenderScene()
{
	//TODO : ComPtr이라 자동 해제 -> default로 변경할 것
    ImGui::ContextUnregister("SceneObject Hierarchy");
    ImGui::ContextUnregister("SceneObject Inspector");

	Memory::SafeDelete(m_LightController);
}

void RenderScene::Initialize()
{
	m_currentScene->CreateGameObject("Root");
	m_MainCamera.RegisterContainer();
	m_LightController = new LightController();
	EditorSceneObjectHierarchy();
	EditorSceneObjectInspector();

	animationJobThread = std::thread([&]
	{
		using namespace std::chrono;

		auto prev = high_resolution_clock::now();

		while (true)
		{
			auto now = high_resolution_clock::now();
			duration<float> elapsed = now - prev;

			// 16.6ms ~ 60fps 에 맞춰 제한
			if (elapsed.count() >= (1.0f / 60.0f))
			{
				prev = now;
				float delta = elapsed.count();
				Banchmark p1;
				m_animationJob.Update(*this, delta);
				std::cout << "p1 : " << p1.GetElapsedTime() << std::endl;
			}
			else
			{
				std::this_thread::sleep_for(microseconds(1)); // CPU 낭비 방지
			}
		}
	});

	animationJobThread.detach();
}

void RenderScene::SetBuffers(ID3D11Buffer* modelBuffer)
{
	m_ModelBuffer = modelBuffer;
}

void RenderScene::Update(float deltaSecond)
{
	for (auto& objIndex : m_currentScene->m_SceneObjects[0]->m_childrenIndices)
	{
		UpdateModelRecursive(objIndex, XMMatrixIdentity());
	}
}

void RenderScene::ShadowStage(Camera& camera)
{
	m_LightController->SetEyePosition(m_MainCamera.m_eyePosition);
	m_LightController->Update();
	m_LightController->RenderAnyShadowMap(*this, camera);
}

void RenderScene::UseModel()
{
	DirectX11::VSSetConstantBuffer(0, 1, &m_ModelBuffer);
}

void RenderScene::UseModel(ID3D11DeviceContext* deferredContext)
{
	deferredContext->VSSetConstantBuffers(0, 1, &m_ModelBuffer);
}

void RenderScene::UpdateModel(const Mathf::xMatrix& model)
{
	DirectX11::UpdateBuffer(m_ModelBuffer, &model);
}

void RenderScene::UpdateModel(const Mathf::xMatrix& model, ID3D11DeviceContext* deferredContext)
{
	deferredContext->UpdateSubresource(m_ModelBuffer, 0, nullptr, &model, 0, 0);
}

void RenderScene::EditorSceneObjectHierarchy()
{
	ImGui::ContextRegister("SceneObject Hierarchy", [&]()
	{
		ImGui::BringWindowToDisplayBack(ImGui::GetCurrentWindow());

		for (auto& obj : m_currentScene->m_SceneObjects)
		{
			if (obj->m_index == 0 || obj->m_parentIndex > 0) continue;

			ImGui::PushID((int)&obj);
			DrawSceneObject(obj);
			ImGui::PopID();
		}
	},ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoFocusOnAppearing);
}

void RenderScene::EditorSceneObjectInspector()
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

			ImGui::Text(m_selectedSceneObject->m_name.ToString().c_str());
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

void RenderScene::UpdateModelRecursive(GameObject::Index objIndex, Mathf::xMatrix model)
{
	const auto& obj = m_currentScene->GetGameObject(objIndex);

	if(GameObject::Type::Bone == obj->GetType())
	{
		const auto& animator = m_currentScene->GetGameObject(obj->m_rootIndex)->GetComponent<Animator>();
		const auto& bone = animator->m_Skeleton->FindBone(obj->m_name.ToString());
		if (bone)
		{
			obj->m_transform.SetAndDecomposeMatrix(bone->m_globalTransform);
		}
	}
	else
	{
		model = XMMatrixMultiply(obj->m_transform.GetLocalMatrix(), model);
		obj->m_transform.SetAndDecomposeMatrix(model);
	}
	for (auto& childIndex : obj->m_childrenIndices)
	{
		UpdateModelRecursive(childIndex, model);
	}
}

void RenderScene::DrawSceneObject(const std::shared_ptr<GameObject>& obj)
{
	ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick;
	if (obj.get() == m_selectedSceneObject)
		flags |= ImGuiTreeNodeFlags_Selected;

	bool opened = ImGui::TreeNodeEx(obj->m_name.ToString().c_str(), flags);

	if (ImGui::IsItemClicked())
		m_selectedSceneObject = obj.get();

	if (ImGui::BeginDragDropSource())
	{
		ImGui::SetDragDropPayload("SCENE_OBJECT", &obj->m_index, sizeof(GameObject::Index));
		ImGui::Text("Moving %s", obj->m_name.ToString().c_str());
		ImGui::EndDragDropSource();
	}

	if (ImGui::BeginDragDropTarget())
	{
		if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("SCENE_OBJECT"))
		{
			GameObject::Index draggedIndex = *(GameObject::Index*)payload->Data;

			// 부모 변경 로직
			if (draggedIndex != obj->m_index) // 자기 자신에 드롭하는 것 방지
			{
				const auto& draggedObj = m_currentScene->GetGameObject(draggedIndex);
				const auto& oldParent = m_currentScene->GetGameObject(draggedObj->m_parentIndex);

				// 1. 기존 부모에서 제거
				auto& siblings = oldParent->m_childrenIndices;
				std::erase_if(siblings, [&](auto index) { return index == draggedIndex; });

				// 2. 새로운 부모에 추가
				draggedObj->m_parentIndex = obj->m_index;
				obj->m_childrenIndices.push_back(draggedIndex);
			}
		}
		ImGui::EndDragDropTarget();
	}

	if (opened)
	{
		// 자식 노드를 재귀적으로 그리기
		for (auto childIndex : obj->m_childrenIndices)
		{
			auto child = m_currentScene->GetGameObject(childIndex);
			DrawSceneObject(child);
		}
		ImGui::TreePop();
	}
}
