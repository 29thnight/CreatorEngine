#include "BossBomb.h"
#include "pch.h"
#include "MeshRenderer.h"
#include "Material.h"
#include "Player.h"

void BossBomb::Start()
{
	meshRenderers = GetOwner()->GetComponentsInchildrenDynamicCast<MeshRenderer>();
	for(auto& m : meshRenderers){
		m->m_Material = Material::InstantiateShared(m->m_Material.get(), "clonebomb");
	}

	Initialize();
}

void BossBomb::Update(float tick)
{
	if (isExplosion) return;

	float prevTime = currentTime;
	currentTime += tick * timeScale;

	if (currentTime >= maxTime && prevTime < maxTime) // 1ȸ ���� Ʈ����
	{
		// ������ �� ����Ʈ + ������
		Explosion();
	}
	else {
		ShaderUpdate();
	}
}

void BossBomb::Initialize() {
	currentTime = 0.f;
	isExplosion = false;
}

void BossBomb::ShaderUpdate()
{
	float t = currentTime / maxTime;
	for (auto& m : meshRenderers) {
		m->m_Material->TrySetValue("Param", "lerpValue", &t, sizeof(float));
		m->m_Material->TrySetValue("Param", "maxScale", &maxScale, sizeof(float));
		m->m_Material->TrySetValue("Param", "scaleFrequency", &scaleFrequency, sizeof(float));
		m->m_Material->TrySetValue("Param", "rotFrequency", &rotFrequency, sizeof(float));
		m->m_Material->TrySetValue("FlashBuffer", "flashStrength", &t, sizeof(float));
		m->m_Material->TrySetValue("FlashBuffer", "flashFrequency", &flashFrequency, sizeof(float));
	}
}

void BossBomb::Explosion()
{
	//���� ����Ʈ �� ������ ó��
	isExplosion = true;

	std::vector<HitResult> hits;
	OverlapInput RangeInfo;
	RangeInfo.layerMask = layermask;
	Transform* transform = &GetOwner()->Transform_();
	RangeInfo.position = transform->GetWorldPosition();
	RangeInfo.rotation = transform->GetWorldQuaternion();
	PhysicsManagers->SphereOverlap(RangeInfo, explosionRadius, hits);

	for (auto& h : hits) {
		Player* p = h.gameObject->GetComponent<Player>();
		if (p) {

		}
	}
}