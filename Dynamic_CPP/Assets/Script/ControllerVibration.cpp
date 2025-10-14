#include "ControllerVibration.h"
#include "pch.h"
#include "GameManager.h"
void ControllerVibration::Start()
{
	auto GM = GetOwner()->GetComponentDynamicCast<GameManager>();
	if (GM)
	{
		GM->PushControllerVibration(this);
	}
}

void ControllerVibration::Update(float tick)
{
}

