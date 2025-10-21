#include "TutorialButton.h"
#include "GameInstance.h"
#include "pch.h"

void TutorialButton::Start()
{
	Super::Start();
}

void TutorialButton::Update(float tick)
{
	Super::Update(tick);
}

void TutorialButton::ClickFunction()
{
	GameInstance::GetInstance()->SetAfterLoadSceneIndex((int)SceneType::Tutorial);
	GameInstance::GetInstance()->SwitchScene("CharSelectScene");
}

