#include "TestPlayer.h"
#include "InputManager.h"
#include "Animator.h"
#include "AnimationController.h"
#include "AniTransition.h"
#include "AnimationBehviourFatory.h"
void TestPlayer::GetPlayer(GameObject* _player)
{
	player = _player;


	AnimationFactorys->ReisterFactory("Idle", []() {return new IdleAni(); });
	AnimationFactorys->ReisterFactory("Walk", []() {return new WalkAni(); });
	AnimationFactorys->ReisterFactory("Run", []() {return new RunAni(); });


	auto animation = player->GetComponent<Animator>();
	animation->CreateController("upper");
	auto controller = animation->GetController("upper");
	controller->CreateState("Idle",0);
	controller->CreateState("Walk",2);
	controller->CreateState("Run",1); 
	controller->SetCurState("Idle");
	controller->CreateTransition("Idle", "Walk")->AddCondition("Speed", 0.3f, ConditionType::Greater,ValueType::Float);
	controller->CreateTransition("Walk", "Idle")->AddCondition("Speed", 0.3f, ConditionType::Less, ValueType::Float);
	controller->CreateTransition("Walk", "Run")->AddCondition("Speed", 10.3f, ConditionType::Greater, ValueType::Float);
	controller->CreateTransition("Run", "Walk")->AddCondition("Speed", 10.3f, ConditionType::Less, ValueType::Float);


	animation->CreateController("lower");
	auto lowercontroller = animation->GetController("lower");
	lowercontroller->CreateState("Idle", 0);
	lowercontroller->CreateState("Walk", 2);
	lowercontroller->CreateState("Run",  1);
	lowercontroller->SetCurState("Idle");
	lowercontroller->CreateTransition("Idle", "Walk")->AddCondition("Walkparm", false, ConditionType::None, ValueType::Trigger);
	lowercontroller->CreateTransition("Walk", "Idle")->AddCondition("Idleparm", false, ConditionType::None, ValueType::Trigger);
	lowercontroller->GetAvatarMask()->UseOnlyLower();
	animation->AddParameter("Speed", player->speed, ValueType::Float);
	animation->AddParameter("Walkparm", false, ValueType::Trigger);
	animation->AddParameter("Idleparm", false, ValueType::Trigger);
}

void TestPlayer::Update(float deltaTime)
{
	auto _player = GameObject::Find("aniTest");
	auto ani = _player->GetComponent<Animator>();
	auto controller = ani->GetController("upper");
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

	ani->SetParameter("Speed", _player->speed);

	if (InputManagement->IsKeyDown('I'))
	{
		ani->SetParameter("Walkparm", true);
	}

	if (InputManagement->IsKeyDown('U'))
	{
		ani->SetParameter("Idleparm", true);
	}
	//std::cout << _player->speed << std::endl;

	
	//player->m_transform.AddPosition({ speed * deltaTime* dir,0,0 });
}
