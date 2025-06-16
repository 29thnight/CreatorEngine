#include "Animator.h"
#include "AnimationController.h"
#include "RenderScene.h"
#include "../RenderEngine/Skeleton.h"
#include "NodeEditor.h"

void Animator::Awake()
{
	auto renderScene = SceneManagers->m_ActiveRenderScene;
	if (renderScene)
	{
		renderScene->RegisterAnimator(this);
	}
}

void Animator::Update(float tick)
{
	if (m_animationControllers.empty()) return;

	for (auto& animationController : m_animationControllers)
	{
		animationController->Update(tick);
	}

	for (auto& param : Parameters)
	{
		if (param->vType == ValueType::Trigger)
		{
			param->ResetTrigger();
		}
	}
}

void Animator::OnDistroy()
{
	auto scene = SceneManagers->GetActiveScene();
	auto renderScene = SceneManagers->m_ActiveRenderScene;
	if (renderScene)
	{
		renderScene->UnregisterAnimator(this);
	}
}

void Animator::SetAnimation(int index)
{
	m_AnimIndex = index;
	UpdateAnimation();
}

void Animator::UpdateAnimation()
{

	if (m_AnimIndex <= 0)
		m_AnimIndex = 0;

	if (m_AnimIndex >= m_Skeleton->m_animations.size())
		m_AnimIndex = m_Skeleton->m_animations.size() - 1;

	m_AnimIndexChosen = m_AnimIndex;
	m_TimeElapsed = 0;
	
}

void Animator::CreateController(std::string name)
{
	
	
	AnimationController* animationController = new AnimationController();
	animationController->m_owner = this;
	animationController->name = name;
	//animationController->CreateMask(); &&&&& 기본은 마스크없게 
	animationController->m_nodeEditor = new NodeEditor();
	animationController->CreateState("Ani State", -1, true);
	m_animationControllers.push_back(animationController);
}

void Animator::CreateController_UI()
{
	AnimationController* animationController = new AnimationController();
	animationController->m_owner = this;
	animationController->name = "NewLayer" + std::to_string(m_animationControllers.size());
	//animationController->CreateMask();
	animationController->m_nodeEditor = new NodeEditor();
	animationController->CreateState("Ani State",-1,true);
	m_animationControllers.push_back(animationController);
}

void Animator::DeleteController(int index)
{
	delete m_animationControllers[index];
	m_animationControllers.erase(m_animationControllers.begin() + index);
}

void Animator::DeleteController(std::string controllerName)
{
	auto it = std::remove_if(m_animationControllers.begin(), m_animationControllers.end(),
		[&](AnimationController* controller)
		{
			if (controller->name == controllerName)
			{
				delete controller;
				return true;        
			}
			return false;
		});

	m_animationControllers.erase(it, m_animationControllers.end()); 
	
}

AnimationController* Animator::GetController(std::string name)
{
    for (auto& Controller : m_animationControllers)
    {
            if (Controller->name == name)
                    return Controller;
    }
    return nullptr;
}

void Animator::DeleteParameter(int index)
{
	if (index >= 0 && index < Parameters.size())
	{
		for (auto& controller : m_animationControllers)
		{
			for (auto& state : controller->StateVec)
			{
				for (auto& transition : state->Transitions)
				{
					for (auto& condition : transition->conditions)
					{
						if (condition.valueParameter == Parameters[index])
						{
							condition.valueParameter = nullptr;
						}
					}
				}

			}
		}
		delete Parameters[index];
		Parameters.erase(Parameters.begin() + index);
	}
}

ConditionParameter* Animator::FindParameter(std::string valueName)
{
	for (auto& parameter : Parameters)
	{
		if (parameter->name == valueName)
		{
			return parameter;
		}
	}
	return nullptr; 
}



