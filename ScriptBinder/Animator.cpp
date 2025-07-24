#include "Animator.h"
#include "AnimationController.h"
#include "RenderScene.h"
#include "../RenderEngine/Skeleton.h"
#include "NodeEditor.h"
#include "SceneManager.h"
#include "Socket.h"

void Animator::Awake()
{
	auto renderScene = SceneManagers->GetRenderScene();
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

void Animator::OnDestroy()
{
	auto scene = SceneManagers->GetActiveScene();
	auto renderScene = SceneManagers->GetRenderScene();
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
	
	std::shared_ptr<AnimationController> animationController = std::make_shared<AnimationController>();
	//AnimationController* animationController = new AnimationController();
	animationController->m_owner = this;
	animationController->name = name;
	animationController->m_nodeEditor = new NodeEditor();
	animationController->CreateState("Ani State", -1, true);
	m_animationControllers.push_back(animationController);
}

void Animator::CreateController_UI()
{
	std::shared_ptr<AnimationController> animationController = std::make_shared<AnimationController>();
	//AnimationController* animationController = new AnimationController();
	animationController->m_owner = this;
	animationController->name = "NewLayer" + std::to_string(m_animationControllers.size());
;	animationController->m_nodeEditor = new NodeEditor();
	animationController->CreateState("Ani State",-1,true);
	m_animationControllers.push_back(animationController);
}

void Animator::DeleteController(int index)
{	
	m_animationControllers.erase(m_animationControllers.begin() + index);
}

void Animator::DeleteController(std::string controllerName)
{
	auto it = std::remove_if(m_animationControllers.begin(), m_animationControllers.end(),
		[&](std::shared_ptr<AnimationController> controller)
		{
				return controller->name == controllerName;
		});

	m_animationControllers.erase(it, m_animationControllers.end()); 
	
}

AnimationController* Animator::GetController(std::string name)
{
    for (auto& Controller : m_animationControllers)
    {
            if (Controller->name == name)
                    return Controller.get();
    }
    return nullptr;
}

Socket* Animator::MakeSocket(const std::string_view& socketName, const std::string_view& boneName)
{
	Socket* socket = m_Skeleton->FindSocket(socketName);
	if (socket) return socket;
	GameObject* obj = GameObject::Find(boneName);
	SceneManagers->GetActiveScene()->CreateGameObject(socketName, GameObjectType::Empty, obj->m_index);

	Socket* newSocket = new Socket();
	newSocket->m_name = socketName;
	newSocket->GameObjectIndex = obj->m_index;
	newSocket->m_ObjectName = boneName;
	m_Skeleton->m_sockets.push_back(newSocket);
	return newSocket;
}

Socket* Animator::FindSocket(const std::string_view& socketName)
{
	return m_Skeleton->FindSocket(socketName);
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



