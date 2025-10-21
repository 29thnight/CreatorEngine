#include "StartButton.h"
#include "GameInstance.h"
#include "pch.h"
void StartButton::Start()
{
	Super::Start();
}

void StartButton::Update(float tick)
{
	Super::Update(tick);
}

void StartButton::ClickFunction()
{
	GameInstance::GetInstance()->SetAfterLoadSceneIndex((int)SceneType::Stage);
	GameInstance::GetInstance()->SwitchScene("CharSelectScene");
}