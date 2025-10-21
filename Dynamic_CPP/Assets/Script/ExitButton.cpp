#include "ExitButton.h"
#include "GameInstance.h"
#include "pch.h"

void ExitButton::Start()
{
	Super::Start();
}

void ExitButton::Update(float tick)
{
	Super::Update(tick);
}

void ExitButton::ClickFunction()
{
	GameInstance::GetInstance()->ExitGame();
}

