#include "Scene.h"

Scene::Scene()
{
	CreateSceneObject("Root", 0);
}

Scene::~Scene()
{
	//TODO : ComPtr이라 자동 해제 -> default로 변경할 것
}

std::shared_ptr<SceneObject> Scene::AddSceneObject(const std::shared_ptr<SceneObject>& sceneObject)
{
	m_SceneObjects.push_back(sceneObject);

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

void Scene::SetBuffers(ID3D11Buffer* modelBuffer, ID3D11Buffer* viewBuffer, ID3D11Buffer* projBuffer)
{
	m_ModelBuffer = modelBuffer;
	m_ViewBuffer = viewBuffer;
	m_ProjBuffer = projBuffer;
}

void Scene::Start()
{
	m_LightController.
		SetEyePosition(m_MainCamera.m_eyePosition)
		.Update();

	//TODO : 카메라 AspectRatio를 아래에 설정하도록 추가


}

void Scene::Update(float deltaSecond)
{
	m_MainCamera.HandleMovement(deltaSecond);
	m_animationJob.Update(*this, deltaSecond);

	for (auto& objIndex : m_SceneObjects[0]->m_childrenIndices)
	{
		UpdateModelRecursive(objIndex, XMMatrixIdentity());
	}


}

void Scene::ShadowStage()
{
	m_LightController.SetEyePosition(m_MainCamera.m_eyePosition);
	m_LightController.Update();
	m_LightController.RenderAnyShadowMap(*this);
}

void Scene::UseModel()
{
	DirectX11::VSSetConstantBuffer(0, 1, &m_ModelBuffer);
}

void Scene::UpdateModel(const Mathf::xMatrix& model)
{
	DirectX11::UpdateBuffer(m_ModelBuffer, &model);
}

void Scene::UseCamera(Camera& camera)
{
	Mathf::xMatrix view = camera.CalculateView();
	Mathf::xMatrix proj = camera.CalculateProjection();
	DirectX11::UpdateBuffer(m_ViewBuffer, &view);
	DirectX11::UpdateBuffer(m_ProjBuffer, &proj);

	DirectX11::VSSetConstantBuffer(1, 1, &m_ViewBuffer);
	DirectX11::VSSetConstantBuffer(2, 1, &m_ProjBuffer);
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
