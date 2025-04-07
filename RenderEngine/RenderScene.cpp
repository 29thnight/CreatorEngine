#include "RenderScene.h"
#include "ImGuiRegister.h"
#include "../ScriptBinder/Scene.h"
#include "LightProperty.h"
#include "../ScriptBinder/Renderer.h"
#include "Skeleton.h"
#include "Light.h"
#include "Banchmark.hpp"
#include "TimeSystem.h"
#include "DataSystem.h"

// 콜백 함수: 입력 텍스트 버퍼 크기가 부족할 때 std::string을 재조정
int InputTextCallback(ImGuiInputTextCallbackData* data)
{
	if (data->EventFlag == ImGuiInputTextFlags_CallbackResize)
	{
		// UserData에 저장된 std::string 포인터를 가져옴
		std::string* str = static_cast<std::string*>(data->UserData);
		// 새로운 길이에 맞춰 std::string의 크기 재조정
		str->resize(data->BufTextLen);
		data->Buf = const_cast<char*>(str->c_str());
	}
	return 0;
}

RenderScene::RenderScene()
{
}

RenderScene::~RenderScene()
{
	//TODO : ComPtr이라 자동 해제 -> default로 변경할 것
    ImGui::ContextUnregister("GameObject Hierarchy");
    ImGui::ContextUnregister("GameObject Inspector");

	Memory::SafeDelete(m_LightController);
}

void RenderScene::Initialize()
{
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
				m_animationJob.Update(*this, delta);
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
	ImGui::ContextRegister("Hierarchy", [&]()
	{
		ImGui::BringWindowToDisplayBack(ImGui::GetCurrentWindow());

		ImVec2 availSize = ImGui::GetContentRegionAvail();
		ImVec2 windowPos = ImGui::GetCursorScreenPos();
		ImRect dropRect(windowPos, ImVec2(windowPos.x + availSize.x, windowPos.y + availSize.y));

		if (ImGui::BeginDragDropTargetCustom(dropRect, ImGui::GetID("MyDropTarget")))
		{
			if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("MODEL_RESOURCE"))
			{
				const char* droppedFilePath = (const char*)payload->Data;
				file::path filename = droppedFilePath;
				file::path filepath = PathFinder::Relative("Models\\") / filename.filename();
				
				Model::LoadModelToScene(DataSystems->LoadCashedModel(filepath.string().c_str()), *m_currentScene);
			}
			ImGui::EndDragDropTarget();
		}

		if (m_currentScene)
		{
			if (ImGui::TreeNodeEx(m_currentScene->m_SceneObjects[0]->m_name.ToString().c_str(), ImGuiTreeNodeFlags_DefaultOpen))
			{
				for (auto& obj : m_currentScene->m_SceneObjects)
				{
					if (0 == obj->m_index || obj->m_parentIndex > 0) continue;

					ImGui::PushID((int)&obj);
					DrawSceneObject(obj);
					ImGui::PopID();
				}
				ImGui::TreePop();
			}
		}

		

	}/*,ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoFocusOnAppearing*/);
}

void RenderScene::EditorSceneObjectInspector()
{
	ImGui::ContextRegister("Inspector", [&]()
	{
		ImGui::BringWindowToDisplayBack(ImGui::GetCurrentWindow());

		if (m_currentScene && m_selectedSceneObject)
		{
			// 객체의 이름을 std::string으로 가져옴 (매 프레임마다 갱신되면 안되면 static이 아니어야 함)
			std::string name = m_selectedSceneObject->m_name.ToString();
			if (ImGui::InputText("name",
				&name[0],
				name.capacity() + 1,
				ImGuiInputTextFlags_CallbackResize | ImGuiInputTextFlags_EnterReturnsTrue,
				InputTextCallback,
				static_cast<void*>(&name)))
			{
				m_selectedSceneObject->m_name.SetString(name);
			}
			Mathf::Vector4 position = m_selectedSceneObject->m_transform.position;
			Mathf::Vector4 rotation = m_selectedSceneObject->m_transform.rotation;
			Mathf::Vector4 scale = m_selectedSceneObject->m_transform.scale;

			float pyr[3]; // pitch yaw roll
			Mathf::QuaternionToEular(rotation, pyr[0], pyr[1], pyr[2]);

			for (int i = 0; i < 3; i++)
            {
				pyr[i] = XMConvertToDegrees(pyr[i]);
			}

			if(ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen))
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
			}

			for (auto& component : m_selectedSceneObject->m_components)
			{
				const auto& type = Meta::Find(component->ToString());
				if (!type) continue;

				if(ImGui::CollapsingHeader(component->ToString().c_str(), ImGuiTreeNodeFlags_DefaultOpen));
				{
					Meta::DrawObject(component, *type);
				}
			}
		}
	}, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoFocusOnAppearing);
}

void RenderScene::UpdateModelRecursive(GameObject::Index objIndex, Mathf::xMatrix model)
{
	if(!m_currentScene) return;

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
	if (!m_currentScene) return;

	ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnDoubleClick;
	if (obj.get() == m_selectedSceneObject)
	{
		flags |= ImGuiTreeNodeFlags_Selected;
	}
	else if (0 == obj->m_parentIndex)
	{
		flags |= ImGuiTreeNodeFlags_DefaultOpen;
	}

	if (0 == obj->m_childrenIndices.size())
	{
		flags |= ImGuiTreeNodeFlags_Leaf;
	}

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
