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
	if (!triggerOnce) return;
	triggerOnce = false;

	std::cout << "MobSpawner Spawn Called" << std::endl;

	int index = 0;
	int maxcount = 0;

	int variable = mobcounts.size();

	for (auto& c : mobcounts) {
		maxcount += c;
	}

	for (int i = 0; i < variable; i++) {
		Benchmark bm1{};
		int count = mobcounts[i];
		std::string prefabName = mobPrefabNames[i];

		std::cout << "Spawning " << count << " of " << prefabName << std::endl;
		auto prefab = PrefabUtilitys->LoadPrefab(prefabName);
		std::cout << "Prefab loaded: " << (prefab ? "Success" : "Failed") << std::endl;
		std::cout << "Elapsed time for loading prefab: " << bm1.GetElapsedTime() << " ms" << std::endl;
		
		std::cout << "Starting instantiation loop for " << prefabName << std::endl;
		while (count > 0) {
			Benchmark bm2{};
			count--;
			if (prefab) {
				std::cout << "Instantiating prefab: " << prefabName << std::endl;
				GameObject* obj = PrefabUtilitys->InstantiatePrefab(prefab, prefabName);
				std::cout << "Prefab instantiated: " << (obj ? "Success" : "Failed") << std::endl;
				std::cout << "Elapsed time for instantiation: " << bm2.GetElapsedTime() << " ms" << std::endl;
				Mathf::Vector3 temp = UniformRandomUpdirection(120, index++, maxcount);
				temp.y = 0;
				temp.Normalize();
				temp *= spawnRadius;
				temp.y += 0.5f;
				obj->m_transform.SetPosition(SpawnArea + temp);
			}
		}
	}

	//GameObject::FindIndex(GetOwner()->m_parentIndex)->Destroy();
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
