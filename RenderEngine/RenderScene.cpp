#include "RenderScene.h"
#include "ImGuiRegister.h"
#include "../ScriptBinder/Scene.h"
#include "LightProperty.h"
#include "../ScriptBinder/RenderableComponents.h"
#include "Skeleton.h"
#include "Light.h"
#include "Benchmark.hpp"
#include "TimeSystem.h"
#include "DataSystem.h"
#include "SceneManager.h"
#include "ImageComponent.h"
#include "UIManager.h"

// 콜백 함수: 입력 텍스트 버퍼 크기가 부족할 때 std::string을 재조정
//int InputTextCallback(ImGuiInputTextCallbackData* data)
//{
//	if (data->EventFlag == ImGuiInputTextFlags_CallbackResize)
//	{
//		// UserData에 저장된 std::string 포인터를 가져옴
//		std::string* str = static_cast<std::string*>(data->UserData);
//		// 새로운 길이에 맞춰 std::string의 크기 재조정
//		str->resize(data->BufTextLen);
//		data->Buf = const_cast<char*>(str->c_str());
//	}
//	return 0;
//}

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
	//EditorSceneObjectHierarchy();
	//EditorSceneObjectInspector();

	//animationJobThread = std::thread([&]
	//{
	//	using namespace std::chrono;

	//	auto prev = high_resolution_clock::now();

	//	while (true)
	//	{
	//		auto now = high_resolution_clock::now();
	//		duration<float> elapsed = now - prev;

	//		// 16.6ms ~ 60fps 에 맞춰 제한
	//		if (elapsed.count() >= (1.0f / 60.0f))
	//		{
	//			prev = now;
	//			float delta = elapsed.count();
	//			m_animationJob.Update(delta);
	//		}
	//		else
	//		{
	//			std::this_thread::sleep_for(microseconds(1)); // CPU 낭비 방지
	//		}
	//	}
	//});

	//animationJobThread.detach();
}

void RenderScene::SetBuffers(ID3D11Buffer* modelBuffer)
{
	m_ModelBuffer = modelBuffer;
}

void RenderScene::Update(float deltaSecond)
{
	m_currentScene = SceneManagers->GetActiveScene();
	if (m_currentScene == nullptr) return;

	for (auto& objIndex : m_currentScene->m_SceneObjects[0]->m_childrenIndices)
	{
		UpdateModelRecursive(objIndex, XMMatrixIdentity());
	}
}

void RenderScene::ShadowStage(Camera& camera)
{
	m_LightController->SetEyePosition(camera.m_eyePosition);
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

void RenderScene::UpdateModelRecursive(GameObject::Index objIndex, Mathf::xMatrix model)
{
	if(!m_currentScene) return;

	const auto& obj = m_currentScene->GetGameObject(objIndex);

	if (!obj || obj->IsDestroyMark())
	{
		return;
	}

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
