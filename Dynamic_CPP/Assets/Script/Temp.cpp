#include "Temp.h"
#include "pch.h"
#include "Entity.h"
#include "MeshRenderer.h"
#include "Material.h"
#include "MaterialInfomation.h"
void Temp::Start()
{
}

void Temp::Update(float tick)
{
}

void Temp::OnDestroy()
{
	for (auto& entity : m_Entities)
	{
		auto meshrenderer = entity->GetOwner()->GetComponent<MeshRenderer>();
		if (meshrenderer)
		{
			auto material = meshrenderer->m_Material;
			if (material)
			{
				material->m_materialInfo.m_bitflag = 0;
			}
		}
	}
}

void Temp::AddEntity(Entity* entity)
{
	if (entity != nullptr)
	{
		m_Entities.push_back(entity);
		std::cout << "Entity added to Temp: " << entity->GetOwner()->m_name.data() << std::endl;
	}
	else
	{
		std::cout << "Attempted to add a null entity to Temp." << std::endl;
	}
}

void Temp::RemoveEntity(Entity* entity)
{
	if (entity != nullptr)
	{
		auto it = std::find(m_Entities.begin(), m_Entities.end(), entity);
		if (it != m_Entities.end())
		{
			m_Entities.erase(it);
			std::cout << "Entity removed from Temp: " << entity->GetOwner()->m_name.data() << std::endl;
		}
		else
		{
			std::cout << "Entity not found in Temp: " << entity->GetOwner()->m_name.data() << std::endl;
		}
	}
	else
	{
		std::cout << "Attempted to remove a null entity from Temp." << std::endl;
	}
}

