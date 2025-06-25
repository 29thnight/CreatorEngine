#include "TestPlayer.h"
#include "InputManager.h"
#include "Animator.h"
#include "AnimationController.h"
#include "AniTransition.h"
#include "AnimationBehviourFatory.h"
#include "InputActionManager.h"
#include "Skeleton.h"
#include "Socket.h"
#include "RigidBodyComponent.h"
#include "CharacterControllerComponent.h"
#include "InputActionComponent.h"
void TestPlayer::GetPlayer(GameObject* _player)
{
	player = _player;

	player->AddComponent<RigidBodyComponent>();
	player->AddComponent<CharacterControllerComponent>();
	//auto input = player->AddComponent<InputActionComponent>();
	AnimationFactorys->ReisterFactory("Idle", []() {return new IdleAni(); });
	AnimationFactorys->ReisterFactory("Walk", []() {return new WalkAni(); });
	AnimationFactorys->ReisterFactory("Run", []() {return new RunAni(); });
	AnimationFactorys->ReisterFactory("Punch", []() {return new RunAni(); });

	auto animation = player->GetComponent<Animator>();
	animation->AddParameter("OnMove", true, ValueType::Bool);
	animation->AddParameter("OnPunch", false, ValueType::Trigger);

	animation->CreateController("upper");
	auto upperController = animation->GetController("upper");
	upperController->CreateMask();
	upperController->CreateState("Idle",0);
	upperController->CreateState("Walk",2);
	upperController->CreateState("Punch",3);
	upperController->SetCurState("Idle");
	upperController->CreateTransition("Idle", "Punch")->AddCondition("OnPunch", true, ConditionType::None, ValueType::Trigger);
	upperController->CreateTransition("Walk", "Punch")->AddCondition("OnPunch", true, ConditionType::None, ValueType::Trigger);
	upperController->CreateTransition("Punch", "Idle");
	upperController->CreateTransition("Punch", "Walk");

	upperController->CreateTransition("Idle", "Walk")->AddCondition("OnMove", true, ConditionType::True, ValueType::Bool);
	upperController->CreateTransition("Walk", "Idle")->AddCondition("OnMove", false, ConditionType::False, ValueType::Bool);
	upperController->GetAvatarMask()->UseOnlyUpper();


	animation->CreateController("lower");
	auto lowercontroller = animation->GetController("lower");

	lowercontroller->CreateMask();
	lowercontroller->CreateState("Idle", 0);
	lowercontroller->CreateState("Walk", 2);
	lowercontroller->SetCurState("Idle");
	lowercontroller->CreateTransition("Idle", "Walk")->AddCondition("OnMove", true, ConditionType::True, ValueType::Bool);
	lowercontroller->CreateTransition("Walk", "Idle")->AddCondition("OnMove", false, ConditionType::False, ValueType::Bool);
	lowercontroller->GetAvatarMask()->UseOnlyLower();


	auto playerMap = InputActionManagers->AddActionMap("Player");
	playerMap->AddButtonAction("Punch", 0, InputType::KeyBoard, KeyBoard::LeftControl, KeyState::Down, [this]() { Punch();});

	//playerMap->AddValueAction("Move", 0, InputValueType::Vector2, InputType::GamePad, { static_cast<size_t>(ControllerButton::LEFT_Thumbstick)},
		//[this](Mathf::Vector2 _vector2) {Move(_vector2);});
	playerMap->AddValueAction("Move", 0, InputValueType::Vector2, InputType::KeyBoard, 
		{ /*KeyBoard::LeftArrow,KeyBoard::RightArrow,KeyBoard::DownArrow,KeyBoard::UpArrow*/
			KeyBoard::UpArrow,KeyBoard::DownArrow,KeyBoard::LeftArrow,KeyBoard::RightArrow,
		},
		[this](Mathf::Vector2 dir) { Move(dir);});

	//ani->m_Skeleton->m_animations[3].SetEvent("Punch", 0.353, []() {Debug->Log("Punch! Punch!");});
}

void TestPlayer::Update(float deltaTime)
{
	auto _player = GameObject::Find("Punch");  
	if (!_player) return;


}

void TestPlayer::Punch()
{
	auto _player = GameObject::Find("Punch");
	auto animator = _player->GetComponent<Animator>();
	animator->SetParameter("OnPunch" ,true);
}

void TestPlayer::Move(Mathf::Vector2 _dir)
{
	auto _player = GameObject::Find("Punch");
	if (!_player) return;
	auto controller = _player->GetComponent<CharacterControllerComponent>();
	controller->Move(_dir);
	auto animator = _player->GetComponent<Animator>();
	if (controller->IsOnMove())
	{
		animator->SetParameter("OnMove", true);
	}
	else
	{
		animator->SetParameter("OnMove", false);
	}

}

void TestPlayer::Jump()
{
	auto _player = GameObject::Find("Punch");
	if (!_player) return;
	_player->m_transform.AddPosition({0,1,0 });

}
