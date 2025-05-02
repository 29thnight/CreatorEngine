#include "TestPlayer.h"
#include "InputManager.h"
#include "Animator.h"
#include "AnimationController.h"
#include "AniTransition.h"
#include "AnimationBehviourFatory.h"
void TestPlayer::GetPlayer(GameObject* _player)
{
	player = _player;
	auto animation = player->GetComponent<Animator>();
	animation->CreateController();
	auto controller = animation->GetController();
	AnimationFactorys->ReisterFactory("Idle", []() {return new IdleAni(); });
	AnimationFactorys->ReisterFactory("Walk", []() {return new WalkAni(); });
	AnimationFactorys->ReisterFactory("Run", []() {return new RunAni(); });
	controller->CreateState("Idle");
	controller->CreateState("Walk");
	controller->CreateState("Run");
	controller->SetCurState("Idle");
	controller->AddParameter("Speed",player->speed,ValueType::Float);
	controller->CreateTransition("Idle", "Walk")->AddCondition("Speed", 0.3f, ConditionType::Greater,ValueType::Float);
	controller->CreateTransition("Walk", "Idle")->AddCondition("Speed", 0.3f, ConditionType::Less, ValueType::Float);
	controller->CreateTransition("Walk", "Run")->AddCondition("Speed", 10.3f, ConditionType::Greater, ValueType::Float);
	controller->CreateTransition("Run", "Walk")->AddCondition("Speed", 10.3f, ConditionType::Less, ValueType::Float);
	

}

void TestPlayer::Update(float deltaTime)
{
	auto _player = GameObject::Find("aniTest");
	auto ani = _player->GetComponent<Animator>();
	auto controller = ani->GetController();
	if (InputManagement->IsKeyDown('1'))
	{
		std::cout << "press 1" << std::endl;
		ani->SetAnimation(0);
	}
	if (InputManagement->IsKeyDown('2'))
	{
		std::cout << "press 2" << std::endl;
		ani->SetAnimation(2);
	}
	if (InputManagement->IsKeyDown('3'))
	{
		std::cout << "press 3" << std::endl;
		ani->SetAnimation(1);
	}
	float dir{};
	if (InputManagement->IsKeyPressed('P'))
	{
		dir = 1.0f;
		_player->speed += 0.01;
	}
	else if(InputManagement->IsKeyPressed('O'))
	{
		dir = -1.0f;
		_player->speed += 0.01;
	}
	else
	{
		if (_player->speed > 0.0f)
		{
			_player->speed -= 0.05f;
			if (_player->speed < 0.0f)
				_player->speed = 0.0f;
		}
	}

	
	if (_player->speed >= maxSpeed)
		_player->speed = maxSpeed;

	controller->SetParameter("Speed", _player->speed);
	//std::cout << _player->speed << std::endl;

	
	//player->m_transform.AddPosition({ speed * deltaTime* dir,0,0 });
}
