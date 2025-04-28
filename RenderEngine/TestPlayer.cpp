#include "TestPlayer.h"
#include "InputManager.h"
#include "Animator.h"
#include "aniFSM.h"
#include "AniTransition.h"

void TestPlayer::GetPlayer(GameObject* _player)
{
	player = _player;
	auto fsm = player->AddComponent<aniFSM>();
	fsm->SetAnimator(player->GetComponent<Animator>());
	fsm->CreateState<IdleAni>("Idle");
	fsm->CreateState<WalkAni>("Walk");
	fsm->CreateState<RunAni>("Run");
	fsm->SetCurState("Idle");
	fsm->AddParameter("Speed",player->speed,valueType::Float);
	fsm->CreateTransition("Idle", "Walk")->AddCondition("Speed", 0.3f, conditionType::Greater,valueType::Float);
	fsm->CreateTransition("Walk", "Idle")->AddCondition("Speed", 0.3f, conditionType::Less, valueType::Float);
	/*AniTransition movetoRun;
	bool canRun = true;
	movetoRun.AddCondition(&canRun, true, conditionType::Equal);*/
}

void TestPlayer::Update(float deltaTime)
{
	auto _player = GameObject::Find("aniTest");
	auto ani = _player->GetComponent<Animator>();
	auto fsm = _player->GetComponent<aniFSM>();
	fsm->SetAnimator(_player->GetComponent<Animator>());
	fsm->CreateState<IdleAni>("Idle");
	fsm->CreateState<WalkAni>("Walk");
	fsm->CreateState<RunAni>("Run");
	fsm->CreateTransition("Idle", "Walk")->AddCondition("Speed", 0.3f, conditionType::Greater, valueType::Float);
	fsm->CreateTransition("Walk", "Idle")->AddCondition("Speed", 0.3f, conditionType::Less, valueType::Float);
	fsm->CreateTransition("Walk", "Run")->AddCondition("Speed", 1.3f, conditionType::Greater, valueType::Float);
	fsm->CreateTransition("Run", "Walk")->AddCondition("Speed", 1.3f, conditionType::Less, valueType::Float);
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
		player->speed += 0.01;
	}
	else if(InputManagement->IsKeyPressed('O'))
	{
		dir = -1.0f;
		player->speed += 0.01;
	}
	else
	{
		player->speed = 0;
	}

	
	if (player->speed >= maxSpeed)
		player->speed = maxSpeed;

	fsm->SetParameter("Speed",player->speed);
	std::cout << player->speed << std::endl;

	
	//player->m_transform.AddPosition({ speed * deltaTime* dir,0,0 });
}
