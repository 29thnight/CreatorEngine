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

	fsm->CreateTransition("Idle", "Walk")->AddCondition(&speed, 0.3f, conditionType::Greater);
	fsm->CreateTransition("Walk", "Idle")->AddCondition(&speed, 0.3f, conditionType::Less);
	/*AniTransition movetoRun;
	bool canRun = true;
	movetoRun.AddCondition(&canRun, true, conditionType::Equal);*/
}

void TestPlayer::Update(float deltaTime)
{
	auto ani = player->GetComponent<Animator>();
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
	if (InputManagement->IsKeyPressed(VK_RIGHT))
	{
		dir = 1.0f;
		speed += 0.01;
	}
	else if(InputManagement->IsKeyPressed(VK_LEFT))
	{
		dir = -1.0f;
		speed += 0.01;
	}
	else
	{
		speed = 0;
	}

	
	if (speed >= maxSpeed)
		speed = maxSpeed;
	//player->m_transform.AddPosition({ speed * deltaTime* dir,0,0 });
}
