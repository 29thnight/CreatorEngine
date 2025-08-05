#include "Core.Minimal.h"
#include "BTHeader.h"

using namespace BT;

class IsStartBoss : public ConditionDecoratorNode
{
public:
	BT_CONDITIONDECORATOR_BODY(IsStartBoss)
	virtual bool ConditionCheck(float deltatime, const BlackBoard& blackBoard) override;
};
