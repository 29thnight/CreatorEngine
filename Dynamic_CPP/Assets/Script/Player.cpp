#include "Player.h"
#include "SceneManager.h"
#include "InputActionManager.h"
#include "InputManager.h"
#include "CharacterControllerComponent.h"
#include "Animator.h"
#include "pch.h"
void Player::Start()
{
	player = GameObject::Find("Punch");
	
	auto playerMap = SceneManagers->GetInputActionManager()->AddActionMap("Player");
	//playerMap->AddButtonAction("Punch", 0, InputType::KeyBoard, KeyBoard::LeftControl, KeyState::Down, [this]() { Punch();});

	playerMap->AddValueAction("Move", 0, InputValueType::Vector2, InputType::GamePad, { static_cast<size_t>(ControllerButton::LEFT_Thumbstick) },
		[this](Mathf::Vector2 _vector2) {Move(_vector2);});
	//playerMap->AddValueAction("Move", 0, InputValueType::Vector2, InputType::KeyBoard,
	//	{ /*KeyBoard::LeftArrow,KeyBoard::RightArrow,KeyBoard::DownArrow,KeyBoard::UpArrow*/
	//		KeyBoard::UpArrow,KeyBoard::DownArrow,KeyBoard::LeftArrow,KeyBoard::RightArrow,
	//	},
	//	[this](Mathf::Vector2 dir) { Move(dir);});

}

void Player::Update(float tick)
{
	player->m_transform.AddPosition({ 1,0,0 });
	
	static float elasepdTime = 0.f;
	elasepdTime += tick;

	if (elasepdTime >= 4.f)
	{
		SceneManagers->GetActiveScene()->CreateGameObject("newenwenw");
		elasepdTime = 0;
	}

	
}

void Player::Move(Mathf::Vector2 dir)
{
	//실행끝나도 Move함수에 대한 bind는 남아서 지울껀지 플래그처리할껀지 &&&&&
	auto controller = player->GetComponent<CharacterControllerComponent>();
	if (!controller) return;
	controller->Move(dir);
	auto animator = player->GetComponent<Animator>();
	if (controller->IsOnMove())
	{
		animator->SetParameter("OnMove", true);
	}
	else
	{
		animator->SetParameter("OnMove", false);
	}
}

