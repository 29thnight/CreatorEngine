#include "TestPlayer.h"
#include "InputManager.h"
#include "Animator.h"
#include "AnimationController.h"
#include "AniTransition.h"
#include "AnimationBehviourFatory.h"
#include "InputActionManager.h"
#include "TimeSystem.h"
#include "Skeleton.h"
#include "Socket.h"
void TestPlayer::GetPlayer(GameObject* _player)
{
	player = _player;


	AnimationFactorys->ReisterFactory("Idle", []() {return new IdleAni(); });
	AnimationFactorys->ReisterFactory("Walk", []() {return new WalkAni(); });
	AnimationFactorys->ReisterFactory("Run", []() {return new RunAni(); });
	AnimationFactorys->ReisterFactory("Punch", []() {return new RunAni(); });

	auto animation = player->GetComponent<Animator>();
	animation->m_Skeleton->MakeSocket("HeadSocket", "mixamorig:Hips");
	animation->AddParameter("Speed", speed, ValueType::Float);
	animation->AddParameter("OnPunch", false, ValueType::Trigger);
	animation->CreateController("upper");
	auto controller = animation->GetController("upper");
	controller->CreateMask();
	controller->CreateState("Idle",0);
	controller->CreateState("Walk",2);
	controller->CreateState("Run",1); 
	controller->CreateState("Punch",3);
	controller->SetCurState("Idle");
	controller->CreateTransition("Idle", "Punch")->AddCondition("OnPunch", false, ConditionType::None, ValueType::Trigger);
	controller->CreateTransition("Idle", "Walk")->AddCondition("Speed", 5.3f, ConditionType::Greater, ValueType::Float);
	controller->CreateTransition("Walk", "Idle")->AddCondition("Speed", 5.3f, ConditionType::Less, ValueType::Float);
	controller->CreateTransition("Walk", "Run")->AddCondition("Speed", 35.3f, ConditionType::Greater, ValueType::Float);
	controller->CreateTransition("Run", "Walk")->AddCondition("Speed", 35.3f, ConditionType::Less, ValueType::Float);
	/*animation->CreateController("lower");
	controller->GetAvatarMask()->UseOnlyUpper();
	auto lowercontroller = animation->GetController("lower");
	lowercontroller->CreateMask();
	lowercontroller->CreateState("Idle", 0);
	lowercontroller->CreateState("Walk", 2);
	lowercontroller->CreateState("Run",  1);
	lowercontroller->CreateState("Punch", 3);
	lowercontroller->SetCurState("Idle");
	lowercontroller->CreateTransition("Idle", "Walk")->AddCondition("Speed", 5.3f, ConditionType::Greater, ValueType::Float);
	lowercontroller->CreateTransition("Walk", "Idle")->AddCondition("Speed", 5.3f, ConditionType::Less, ValueType::Float);
	lowercontroller->CreateTransition("Walk", "Run")->AddCondition("Speed", 35.3f, ConditionType::Greater, ValueType::Float);
	lowercontroller->CreateTransition("Run", "Walk")->AddCondition("Speed", 35.3f, ConditionType::Less, ValueType::Float);
	lowercontroller->GetAvatarMask()->UseOnlyLower();*/


	auto playerMap = InputActionManagers->AddActionMap("Player");
	playerMap->AddButtonAction("Punch", 0, InputType::KeyBoard, KeyBoard::LeftControl, KeyState::Down, [this]() { Punch();});
	playerMap->AddButtonAction("Jump",0, InputType::KeyBoard, KeyBoard::Space, KeyState::Released, [this]() {Jump();});

	playerMap->AddButtonAction("TestDe", 0, InputType::GamePad, static_cast<size_t>(ControllerButton::Y), KeyState::Down, [this]() { TestDelete(); });
	playerMap->AddButtonAction("Attack",0, InputType::GamePad, static_cast<size_t>(ControllerButton::A), KeyState::Down, []() { std::cout << "Test  Pad A Click" << std::endl;});
	playerMap->AddButtonAction("Attack2",0, InputType::GamePad, static_cast<size_t>(ControllerButton::B), KeyState::Down, []() { std::cout << "Test  Pad B Click" << std::endl;});

	//playerMap->AddValueAction("Move", 0, InputValueType::Vector2, InputType::GamePad, { static_cast<size_t>(ControllerButton::LEFT_Thumbstick)},
		//[this](Mathf::Vector2 _vector2) {Move(_vector2);});

	playerMap->AddValueAction("Move", 0, InputValueType::Vector2, InputType::KeyBoard, { KeyBoard::LeftArrow,KeyBoard::RightArrow,KeyBoard::DownArrow,KeyBoard::UpArrow },
		[this](Mathf::Vector2 dir) {Move(dir);});
}

void TestPlayer::Update(float deltaTime)
{
	deta = deltaTime;
	auto _player = GameObject::Find("Punch");
	if (!_player) return;
	auto ani = _player->GetComponent<Animator>();
	ani->SetParameter("Speed", speed);


	auto sword = GameObject::Find("plane");
	if (sword)
	{
		Socket* headsocket = ani->m_Skeleton->FindSocket("HeadSocket");
		Mathf::xMatrix socketMatrix = headsocket->worldTransform;

		XMVECTOR scale, rotationQuat, translation;
		Mathf::Quaternion quat;
		Mathf::Vector3 pos;
		if (XMMatrixDecompose(&scale, &rotationQuat, &translation, socketMatrix))
		{
			XMStoreFloat4(&quat, rotationQuat);
			XMStoreFloat3(&pos, translation);
		}
		sword->m_transform.SetPosition(pos);
		sword->m_transform.SetRotation(quat);

	}

	if (InputManagement->IsKeyDown('L'))
	{
		ani->SetParameter("StopPunch", true);
	}

}

void TestPlayer::TestDelete()
{
	InputActionManagers->FindActionMap("Player")->DeleteAction("Jump");
}

void TestPlayer::Punch()
{
	auto _player = GameObject::Find("Punch");
	auto ani = _player->GetComponent<Animator>();
	ani->SetParameter("OnPunch" ,true);
}

void TestPlayer::Move(Mathf::Vector2 _dir)
{
	if (_dir.Length() != 0)
	{
		speed += 0.05;
	}
	else
	{
		if (speed > 0.0f)
		{
			speed -= 0.05f;
			if (speed < 0.0f)
				speed = 0.0f;
		}
	}

	if (speed >= maxSpeed)
		speed = maxSpeed;
	dir = _dir;
	auto _player = GameObject::Find("Punch");
	if (!_player) return;
	_player->m_transform.AddPosition({ dir.x * speed * deta,0  ,dir.y * speed * deta });
	//Debug->Log(std::to_string(dir.x) + "     ");
	//Debug->Log(std::to_string(dir.y));
}

void TestPlayer::Jump()
{
	auto _player = GameObject::Find("Punch");
	if (!_player) return;
	_player->m_transform.AddPosition({0,1,0 });

}
