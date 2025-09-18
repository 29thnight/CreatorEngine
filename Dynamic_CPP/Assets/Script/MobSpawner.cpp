#include "MobSpawner.h"
#include "pch.h"
#include "PrefabUtility.h"
#include "Player.h"
void MobSpawner::Start()
{
	SpawnArea = GameObject::FindIndex(GetOwner()->m_parentIndex)->m_transform.GetWorldPosition();
}

void MobSpawner::OnTriggerEnter(const Collision& collision)
{
	Player* p = collision.otherObj->GetComponent<Player>();
	if(p != nullptr)
		Spawn();
}

void MobSpawner::OnCollisionEnter(const Collision& collision)
{
	Player* p = collision.otherObj->GetComponent<Player>();
	if (p != nullptr)
		Spawn();
}

void MobSpawner::Spawn()
{
	int index = 0;
	int maxcount = 0;

	int variable = mobcounts.size();

	for (auto& c : mobcounts) {
		maxcount += c;
	}

	for (int i = 0; i < variable; i++) {
		int count = mobcounts[i];
		std::string prefabName = mobPrefabNames[i];

		auto prefab = PrefabUtilitys->LoadPrefab(prefabName);
		while (count > 0) {
			count--;
			if (prefab) {
				GameObject* obj = PrefabUtilitys->InstantiatePrefab(prefab, prefabName);
				Mathf::Vector3 temp = UniformRandomUpdirection(120, index++, maxcount);
				temp.y = 0;
				temp.Normalize();
				temp *= spawnRadius;
				temp.y += 0.5f;
				obj->m_transform.SetPosition(SpawnArea + temp);
			}
		}
	}
}

void MobSpawner::TestSpawn()
{
	Spawn();
}

double MobSpawner::radical_inverse_base2(unsigned int n)
{
	double inv = 0.0;
	double f = 0.5;
	while (n > 0) {
		inv += f * (n % 2);
		n /= 2;
		f *= 0.5;
	}
	return inv;
}

Mathf::Vector3 MobSpawner::UniformRandomUpdirection(float angle, int curindex, int maxCount)
{
	double cone_angle_rad = (angle / 2.0) * (Mathf::pi / 180.0);
	double cos_cone_angle = cos(cone_angle_rad);

	double u = static_cast<double>(curindex) / maxCount;
	double v = radical_inverse_base2(curindex);

	double phi = u * 2.0 * Mathf::pi;

	double cos_theta = 1.0 - v * (1.0 - cos_cone_angle);
	double sin_theta = sqrt(1.0 - cos_theta * cos_theta);

	Mathf::Vector3 dir;
	dir = {
		(float)(sin_theta * cos(phi)),
		(float)cos_theta,
		(float)(sin_theta * sin(phi))
	};

	return dir;
}
