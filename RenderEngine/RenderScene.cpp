#include "RenderScene.h"
#include "ImGuiRegister.h"
#include "../ScriptBinder/Scene.h"
#include "LightProperty.h"
#include "../ScriptBinder/RenderableComponents.h"
#include "Skeleton.h"
#include "LightController.h"
#include "Benchmark.hpp"
#include "MeshRenderer.h"
#include "TimeSystem.h"
#include "DataSystem.h"
#include "SceneManager.h"
#include "RenderCommand.h"
#include "ImageComponent.h"
#include "UIManager.h"

constexpr size_t TRANSFORM_SIZE = sizeof(Mathf::xMatrix) * MAX_BONES;

RenderScene::~RenderScene()
{
	Memory::SafeDelete(m_LightController);
}

void RenderScene::Initialize()
{
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

void RenderScene::RegisterAnimator(Animator* animatorPtr)
{
	if (nullptr == animatorPtr) return;

	HashedGuid animatorGuid = animatorPtr->GetInstanceID();

	if (m_animatorMap.find(animatorGuid) != m_animatorMap.end()) return;

	m_animatorMap[animatorGuid] = animatorPtr;
}

void RenderScene::UnregisterAnimator(Animator* animatorPtr)
{
	if (nullptr == animatorPtr) return;

	HashedGuid animatorGuid = animatorPtr->GetInstanceID();

	if (m_animatorMap.find(animatorGuid) != m_animatorMap.end()) return;

	m_animatorMap.erase(animatorGuid);
}

void RenderScene::RegisterCommand(MeshRenderer* meshRendererPtr)
{
	if (nullptr == meshRendererPtr) return;

	HashedGuid meshRendererGuid = meshRendererPtr->GetInstanceID();

	if (m_proxyMap.find(meshRendererGuid) != m_proxyMap.end()) return;

	// Create a new proxy for the mesh renderer and insert it into the map
	auto managedCommand = std::make_shared<MeshRendererProxy>(meshRendererPtr);
	m_proxyMap[meshRendererGuid] = managedCommand;
}

MeshRendererProxy* RenderScene::FindProxy(size_t guid)
{
	if (m_proxyMap.find(guid) == m_proxyMap.end()) return nullptr;

	return m_proxyMap[guid].get();
}

void RenderScene::UpdateCommand(MeshRenderer* meshRendererPtr)
{
	if (nullptr == meshRendererPtr || meshRendererPtr->IsDestroyMark()) return;

	auto owner = meshRendererPtr->GetOwner();
	if(nullptr == owner || owner->IsDestroyMark()) return;

	Mathf::xMatrix worldMatrix = owner->m_transform.GetWorldMatrix();
	HashedGuid meshRendererGuid = meshRendererPtr->GetInstanceID();

	if (m_proxyMap.find(meshRendererGuid) == m_proxyMap.end()) return;
	
	auto& proxyObject = m_proxyMap[meshRendererGuid];

	if (nullptr == proxyObject) return;

	HashedGuid aniGuid = proxyObject->m_animatorGuid;

	if(m_animatorMap.find(aniGuid) != m_animatorMap.end() 
		&& proxyObject->IsSkinnedMesh())
	{
		auto* dstPalete = &proxyObject->m_finalTransforms;
		auto* srcPalete = &m_animatorMap[aniGuid]->m_FinalTransforms;

		memcpy(dstPalete, srcPalete, TRANSFORM_SIZE);
	}

	proxyObject->m_worldMatrix = worldMatrix;

	constexpr int INVAILD_INDEX = -1;

	int& lightMapIndex		= meshRendererPtr->m_LightMapping.lightmapIndex;
	int& proxyLightMapIndex = proxyObject->m_LightMapping.lightmapIndex;

	if(INVAILD_INDEX != lightMapIndex && proxyLightMapIndex != lightMapIndex)
	{
		proxyObject->m_LightMapping = meshRendererPtr->m_LightMapping;
	}
}

void RenderScene::UnregisterCommand(MeshRenderer* meshRendererPtr)
{
	if (nullptr == meshRendererPtr) return;

	HashedGuid meshRendererGuid = meshRendererPtr->GetInstanceID();

	if (m_proxyMap.find(meshRendererGuid) == m_proxyMap.end()) return;

	m_proxyMap.erase(meshRendererGuid);
}
