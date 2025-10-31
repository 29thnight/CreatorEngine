#include "BossWarningAni.h"
#include "pch.h"
#include "AnimationController.h"
#include "Animator.h"
#include "TBoss1.h"
#include "GameInstance.h"
#include "GameManager.h"
#include "SFXPoolManager.h"
void BossWarningAni::Enter()
{
    AnimationController* controller = m_ownerController;
    if (controller)
    {
        animator = controller->GetOwner();
    }

    if (animator)
    {
        animator->SetParameter("CanTransitionToAttack", false);
        GameObject* model = animator->GetOwner();
        if (model)
        {
            ownerObj = GameObject::FindIndex(model->m_parentIndex);
        }
    }

    // Ÿ�̸Ӹ� �ʱ�ȭ�մϴ�.
    m_stateTimer = 0.0f;

    auto GMobj = GameObject::Find("GameManager");
    if (GMobj)
    {
        GameManager* GM = GMobj->GetComponent<GameManager>();
        if (GM)
        {
            auto pool = GM->GetSFXPool();
            if (pool)
            {
                pool->PlayOneShot(GameInstance::GetInstance()->GetSoundName()->GetSoudNameRandom("BossPremotion"));
            }
        }
    }
}

void BossWarningAni::Update(float deltaTime)
{
    if (!animator) return;
    if (!ownerObj) return;

	TBoss1* script = ownerObj->GetComponent<TBoss1>();
    if (script == nullptr) return;

    ConditionParameter* param = animator->FindParameter("CanTransitionToAttack");
    // ���� ��ȯ �㰡�� ���� �ʾ��� ���� Ÿ�̸Ӹ� üũ�մϴ�.


    if (param&& param->bValue==false)
    {
        m_stateTimer += deltaTime;
        float warningTime = script->GetAttackWarningTime();

        if (m_stateTimer >= warningTime)
        {
            std::cout << "[BossWarningAni::Update] Ÿ�̸� �Ϸ�. OnWarningFinished() ȣ�� �õ�." << std::endl;
            // 1. C++ ���¸� Spawning���� ��ȯ�ϵ��� �����մϴ�.
            script->OnWarningFinished();
        }
    }
}

void BossWarningAni::Exit()
{

    TBoss1* script = ownerObj->GetComponent<TBoss1>();
    if (script != nullptr) {
        std::cout << "[BossWarningAni::Exit] ���� ����. ���� C++ ������: " << script->GetPatternPhaseToString(script->m_patternPhase) << std::endl;
        if (script->m_patternPhase == TBoss1::EPatternPhase::Warning) {
            script->OnWarningFinished();
        }
    }
}
