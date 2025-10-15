#include "Core.Minimal.h"
#include "BTHeader.h"

using namespace BT;

class ActionCountCheck : public ConditionDecoratorNode
{
public:
	BT_CONDITIONDECORATOR_BODY(ActionCountCheck)
	virtual bool ConditionCheck(float deltatime, const BlackBoard& blackBoard) override;
};
