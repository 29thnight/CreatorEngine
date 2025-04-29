#include "RenderScene.h"
#include "ImGuiRegister.h"
#include "../ScriptBinder/Scene.h"
#include "LightProperty.h"
#include "../ScriptBinder/RenderableComponents.h"
#include "Skeleton.h"
#include "LightController.h"
#include "Benchmark.hpp"
#include "TimeSystem.h"
#include "DataSystem.h"
#include "SceneManager.h"
#include "ImageComponent.h"
#include "UIManager.h"

RenderScene::~RenderScene()
{
	//TODO : ComPtr이라 자동 해제 -> default로 변경할 것
    ImGui::ContextUnregister("GameObject Hierarchy");
    ImGui::ContextUnregister("GameObject Inspector");

	Memory::SafeDelete(m_LightController);
}

void RenderScene::Initialize()
{
	//TODO : 곧 컴포넌트로 빠져야함.
	//m_MainCamera.RegisterContainer();

	m_LightController = new LightController();
}

void RenderScene::SetBuffers(ID3D11Buffer* modelBuffer)
{
	m_ModelBuffer = modelBuffer;
}

void RenderScene::Update(float deltaSecond)
{
	m_currentScene = SceneManagers->GetActiveScene();
	if (m_currentScene == nullptr) return;

    m_LightController->m_lightCount = m_currentScene->UpdateLight(m_LightController->m_lightProperties);

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
