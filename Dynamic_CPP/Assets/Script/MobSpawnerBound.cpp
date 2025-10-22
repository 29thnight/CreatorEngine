#include "MobSpawnerBound.h"
#include "pch.h"
#include "Entity.h"
#include "MobSpawner.h"

void MobSpawnerBound::Start()
{
	auto children = GetOwner()->m_childrenIndices;
	for(auto& child : children)
	{
		GameObject* childObj = GameObject::FindIndex(child);
		if (childObj->m_tag == "Host")
			m_hostObj = childObj;
		else if (childObj->m_tag == "Target")
			m_targetObj = childObj;

	}



	if (m_hostObj)
	{
		auto hostchildren = m_hostObj->m_childrenIndices;
		for (auto& child : hostchildren)
		{
			GameObject* childObj = GameObject::FindIndex(child);
			Entity* entity    = childObj->GetComponentDynamicCast<Entity>();
			auto moduleShared = entity->shared_from_this();           // ModuleBehavior ����shared_ptr
			auto entityShared = std::static_pointer_cast<Entity>(moduleShared); // Entity ���� shared_ptr
			std::weak_ptr<Entity> weakEntity(entityShared);
			m_hosts.push_back(weakEntity);
		}
	}

	if (m_targetObj)
	{
		auto hostchildren = m_targetObj->m_childrenIndices;
		for (auto& child : hostchildren)
		{
			GameObject* childObj = GameObject::FindIndex(child);
			if (childObj)
			{

				auto childchildren = childObj->m_childrenIndices; //targetObj�� ����
				GameObject* childchildObj = GameObject::FindIndex(childchildren[0]); //������ �޸� ��ü
				auto spawner = childchildObj->GetComponent<MobSpawner>();
				if (spawner)
				{
					m_targets.push_back(spawner);
					spawner->isBound = true;
				}
			}
		}
	}
}

void MobSpawnerBound::Update(float tick)
{

	if (m_targets.empty()) return;
	bool active = false;

	for (auto& host : m_hosts)
	{
		if (auto sharedHost = host.lock()) // ��������� shared_ptr ��ȯ
		{
			if (sharedHost->GetAlive())
			{
				active = true; // �ϳ��� ��������� active
				break;         // �� �̻� üũ�� �ʿ� ����
			}
		}
	}
	if (active == false)  //ȣ��Ʈ�� ���׾�����
	{
		for (auto& target : m_targets)
		{
			target->isActive = false;
		}
		m_targets.clear(); //�ϴ� ������ ������� ����� ������Ʈ�� �ȵ��� 
	}
}

