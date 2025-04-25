#include "TestPlayer.h"
#include "InputManager.h"
#include "Animator.h"
#include "aniFSM.h"
void TestPlayer::GetPlayer(GameObject* _player)
{
	player = _player;
	player->AddComponent<aniFSM>();
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
		speed += 0.001;
	}
	else if(InputManagement->IsKeyPressed(VK_LEFT))
	{
		dir = -1.0f;
		speed += 0.001;
	}
	else
	{
		speed = 0;
	}

	if (speed >= maxSpeed)
	{
		ani->SetAnimation(1);
	}
	if (speed >= maxSpeed)
		speed = maxSpeed;
	//player->m_transform.AddPosition({ speed * deltaTime* dir,0,0 });
}
