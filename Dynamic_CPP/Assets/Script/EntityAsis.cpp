#include "EntityAsis.h"
#include "pch.h"
#include "EntityItem.h"
#include "Temp.h"
#include "MeshRenderer.h"
#include "Material.h"
#include "MaterialInfomation.h"
#include "SceneManager.h"
#include "InputActionManager.h"

#include "GameManager.h"
using namespace Mathf;
void EntityAsis::Start()
{
	//auto playerMap = SceneManagers->GetInputActionManager()->AddActionMap("Test");
	//playerMap->AddButtonAction("Punch", 0, InputType::KeyBoard, KeyBoard::LeftControl, KeyState::Down, [this]() { Punch();});

	//playerMap->AddValueAction("Move", 0, InputValueType::Vector2, InputType::KeyBoard, { 'A', 'D', 'S', 'W'}, [this](Mathf::Vector2 _vector2) {Inputblabla(_vector2);});

	auto gameManager = GameObject::Find("GameManager");	
	if (gameManager)
	{
		auto gm = gameManager->GetComponent<GameManager>();
		if (gm)
		{
			gm->PushEntity(this);
		}
	}

	auto meshrenderer = GetOwner()->GetComponent<MeshRenderer>();
	if (meshrenderer)
	{
		auto material = meshrenderer->m_Material;
		if (material)
		{
			material->m_materialInfo.m_bitflag = 0;
		}
	}

	asisTail = GameObject::Find("AsisTail");
}

void EntityAsis::Update(float tick)
{
	auto& tr = GetComponent<Transform>();
	Mathf::Vector3 pos = tr.GetWorldPosition();
	dir.Normalize();
	pos += Vector3(dir.x, 0.f, dir.y) * tick * 5.f;
	tr.SetPosition(pos);


	timer += tick;
	angle += tick * 5.f;
	Transform* tailTr = asisTail->GetComponent<Transform>();
	Vector3 tailPos = tailTr->GetWorldPosition();
	Vector3 tailForward = XMVector3Rotate(XMVectorSet(0, 0, 1, 0), tailTr->GetWorldQuaternion());
	for (int i = 0; i < 3; i++)
	{
		if (m_EntityItems.size() < i + 1) return;

		if (m_EntityItems[i] != nullptr)
		{
			float orbitAngle = angle + XM_PI * 2.f * i / 3.f;;
			float r = radius + sinf(timer) * 3.f;
			Vector3 localOrbit = Vector3(cos(orbitAngle) * r, 0.f, sin(orbitAngle) * r);
			XMMATRIX axisRotation = XMMatrixRotationAxis(tailForward, 0.0f);
			XMVECTOR orbitOffset = XMVector3Transform(localOrbit, axisRotation);

			Vector3 finalPos = tailPos + Vector3(orbitOffset.m128_f32[0], orbitOffset.m128_f32[1], orbitOffset.m128_f32[2]);
			m_EntityItems[i]->GetComponent<Transform>().SetPosition(finalPos);
		}
	}
}

void EntityAsis::Inputblabla(Mathf::Vector2 dir)
{
	this->dir = dir;
}

