#pragma once
#include "Component.h"
#include "TestAniScprit.generated.h"
class TestAniScprit : public Component
{
public:
   ReflectTestAniScprit
	[[Serializable(Inheritance:Component)]]
	GENERATED_BODY(TestAniScprit)

	[[Method]]
	void OnPunch();

	[[Method]]
	void Moving();

};

