#include "MonsterProjectile.h"
#include "pch.h"
#include "PlayEffectAll.h"
#include "PrefabUtility.h"
#include "SphereColliderComponent.h"
#include "GameManager.h"
#include "SFXPoolManager.h"
void MonsterProjectile::Start()
{
}

void MonsterProjectile::Update(float tick)
{
    if (m_owner->IsDestroyMark() == true)
    {
        OwnerDestroy = true;
    }
	if (!isInitialize||!m_isMoving) {
        return;
	}

    m_elapsedTime += tick;

    float t = m_elapsedTime / m_duration;
    t = (t > 1.f) ? 1.f : t;

    Mathf::Vector3 currentPos = CalculateBezierPoint(t, m_startPos, m_controlPos, m_endPos);
    m_pOwner->m_transform.SetPosition(currentPos);

    if (t >= 1.f)
    {
        m_isMoving = false;

        // TODO: 여기에 폭발 이펙트 생성, 데미지 처리, 오브젝트 파괴 등의 로직을 추가합니다.
        OverlapInput input;
        
        std::vector<HitResult> results;

        input.position = currentPos;
        input.rotation = SimpleMath::Quaternion::Identity;
        input.layerMask = 1 << 5 | 1 << 7;

        PhysicsManagers->SphereOverlap(input, m_radius, results);

        //test
        Prefab* test = PrefabUtilitys->LoadPrefab("TestSphere");
        if (test)
        {
            GameObject* testObj = PrefabUtilitys->InstantiatePrefab(test, "test");
            testObj->GetComponent<Transform>()->SetPosition(currentPos);
            testObj->GetComponent<SphereColliderComponent>()->SetRadius(m_radius);
        }
        

        auto GMobj = GameObject::Find("GameManager");
        if (GMobj)
        {
            GameManager* GM = GMobj->GetComponent<GameManager>();
            if (GM)
            {
                auto pool = GM->GetSFXPool();
                if (pool)
                {
                    pool->PlayOneShot(GameInstance::GetInstance()->GetSoundName()->GetSoudNameRandom("Mon002AttackExplosion"));
                }
            }
        }




        Prefab* PungPrefab = PrefabUtilitys->LoadPrefab("MonProjectileEffect");
        if (PungPrefab)
        {
            GameObject* pungObj = PrefabUtilitys->InstantiatePrefab(PungPrefab, "PungEffect");
            auto pungEffect = pungObj->GetComponent<PlayEffectAll>();
            Mathf::Vector3 pungPos = GetOwner()->m_transform.GetWorldPosition();
            pungObj->GetComponent<Transform>()->SetPosition(pungPos);
            pungEffect->Initialize();
        }



        for (auto& res : results) {
            auto entity = res.gameObject->GetComponentDynamicCast<Entity>();
            if (entity) {
                entity->SendDamage(m_owner, m_damege);
            }
        }

        if (OwnerDestroy)
        {
            GetOwner()->Destroy();
        }
        GetOwner()->SetEnabled(false);
        
    }
}

void MonsterProjectile::Initialize(Entity* owner, float radius, int damege, Mathf::Vector3 startPos, Mathf::Vector3 controlPos, Mathf::Vector3 endPos, float calculatedDuration)
{
    m_owner = owner;
    m_radius = radius;
    m_damege = damege;

    m_startPos = startPos;
    m_controlPos = controlPos;
    m_endPos = endPos;
    m_duration = calculatedDuration > 0 ? calculatedDuration : 1.f; // 0으로 나누는 것을 방지
    m_elapsedTime = 0.f;
   
    isInitialize = true;
    // 생성 즉시 시작 위치로 이동
    m_pOwner->m_transform.SetPosition(m_startPos);
    m_isMoving = true;
}

Mathf::Vector3 MonsterProjectile::CalculateBezierPoint(float t, const Mathf::Vector3& p0, const Mathf::Vector3& p1, const Mathf::Vector3& p2)
{
    float u = 1.f - t;
    float tt = t * t;
    float uu = u * u;

    Mathf::Vector3 p = uu * p0;      // (1-t)^2 * P0
    p += 2.f * u * t * p1;  // 2 * (1-t) * t * P1
    p += tt * p2;           // t^2 * P2

    return p;
}

