#include "EntityResource.h"
#include "pch.h"
#include "Player.h"
#include "GameManager.h"
#include "RigidBodyComponent.h"
#include "EntityItem.h"
void EntityResource::Start()
{
}

void EntityResource::Update(float tick)
{
}

void EntityResource::Attack(Entity* sender, int damage)
{
	//std::cout << "EntityResource Attack" << std::endl;
	if (sender)
	{
		auto player = dynamic_cast<Player*>(sender);
		if (player)
		{
			// hit
			m_currentHP -= std::max(damage, 0);

			if (m_currentHP <= 0) {
				// dead

				auto& resources = GameObject::Find("GameManager")->GetComponent<GameManager>()->GetResourcePool();
				/*if (!resources.empty()) {
					int tempidx = 0;
					auto item = resources[tempidx];
					resources.erase(resources.begin() + tempidx);
					Mathf::Vector3 spawnPos = GetOwner()->m_transform.GetWorldPosition();
					spawnPos.y += 3.f;
					item->GetOwner()->m_transform.SetPosition(spawnPos);
					item->GetComponent<RigidBodyComponent>().AddForce({ 0.f, 300.f, 300.f }, EForceMode::IMPULSE);
				}*/
				int tempidx = 0;
				for (auto& resource : resources)
				{
					auto item = dynamic_cast<EntityItem*>(resource);
					if (item && itemCode == item->itemCode)
					{
						resources.erase(resources.begin() + tempidx);
						Mathf::Vector3 spawnPos = GetOwner()->m_transform.GetWorldPosition();
						spawnPos.y += 3.f;
						item->GetOwner()->m_transform.SetPosition(spawnPos);
						item->GetComponent<RigidBodyComponent>().AddForce({ 0.f, 300.f, 300.f }, EForceMode::IMPULSE);
						break;
					}
					tempidx++;
				}
				GetOwner()->Destroy();
			}
		}
	}
	else
	{
		std::cout << "EntityResource Attack sender is nullptr" << std::endl;
	}
}

