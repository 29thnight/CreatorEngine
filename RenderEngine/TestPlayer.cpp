#include "TestPlayer.h"
#include "InputManager.h"
#include "Animator.h"
#include "aniFSM.h"
#include "AniTransition.h"
#include "anibegvioutFatory.h"
void TestPlayer::GetPlayer(GameObject* _player)
{
	player = _player;
	auto fsm = player->AddComponent<aniFSM>();
	fsm->SetAnimator(player->GetComponent<Animator>());
	aniFactory->ReisterFactory("Idle", []() {return new IdleAni(); });
	aniFactory->ReisterFactory("Walk", []() {return new WalkAni(); });
	aniFactory->ReisterFactory("Run", []() {return new RunAni(); });
	fsm->CreateState("Idle");
	fsm->CreateState("Walk");
	fsm->CreateState("Run");
	fsm->SetCurState("Idle");
	fsm->AddParameter("Speed",player->speed,valueType::Float);
	fsm->CreateTransition("Idle", "Walk")->AddCondition("Speed", 0.3f, conditionType::Greater,valueType::Float);
	fsm->CreateTransition("Walk", "Idle")->AddCondition("Speed", 0.3f, conditionType::Less, valueType::Float);
	fsm->CreateTransition("Walk", "Run")->AddCondition("Speed", 1.3f, conditionType::Greater, valueType::Float);
	fsm->CreateTransition("Run", "Walk")->AddCondition("Speed", 1.3f, conditionType::Less, valueType::Float);
	

}

void TestPlayer::Update(float deltaTime)
{
	auto _player = GameObject::Find("aniTest");
	auto ani = _player->GetComponent<Animator>();
	auto fsm = _player->GetComponent<aniFSM>();
	//fsm->SetAnimator(_player->GetComponent<Animator>());
	//fsm->CreateState("Idle");
	//fsm->CreateState("Walk");
	//fsm->CreateState("Run");
	//fsm->SetCurState("Idle");
	//fsm->AddParameter("Speed", player->speed, valueType::Float);
	//fsm->CreateTransition("Idle", "Walk")->AddCondition("Speed", 0.3f, conditionType::Greater, valueType::Float);
	//fsm->CreateTransition("Walk", "Idle")->AddCondition("Speed", 0.3f, conditionType::Less, valueType::Float);
	//fsm->CreateTransition("Walk", "Run")->AddCondition("Speed", 1.3f, conditionType::Greater, valueType::Float);
	//fsm->CreateTransition("Run", "Walk")->AddCondition("Speed", 1.3f, conditionType::Less, valueType::Float);
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
		_player->speed = 0;
	}

	
	if (_player->speed >= maxSpeed)
		_player->speed = maxSpeed;

	fsm->SetParameter("Speed", _player->speed);
	std::cout << _player->speed << std::endl;

	
	//player->m_transform.AddPosition({ speed * deltaTime* dir,0,0 });
}
