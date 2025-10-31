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

    // 타이머를 초기화합니다.
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
    // 아직 전환 허가를 하지 않았을 때만 타이머를 체크합니다.


    if (param&& param->bValue==false)
    {
        m_stateTimer += deltaTime;
        float warningTime = script->GetAttackWarningTime();

        if (m_stateTimer >= warningTime)
        {
            std::cout << "[BossWarningAni::Update] 타이머 완료. OnWarningFinished() 호출 시도." << std::endl;
            // 1. C++ 상태를 Spawning으로 전환하도록 보고합니다.
            script->OnWarningFinished();
        }
    }
}

void BossWarningAni::Exit()
{

    TBoss1* script = ownerObj->GetComponent<TBoss1>();
    if (script != nullptr) {
        std::cout << "[BossWarningAni::Exit] 상태 종료. 현재 C++ 페이즈: " << script->GetPatternPhaseToString(script->m_patternPhase) << std::endl;
        if (script->m_patternPhase == TBoss1::EPatternPhase::Warning) {
            script->OnWarningFinished();
        }
    }
}
