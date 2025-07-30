#include "Core.Minimal.h"
#include "BTHeader.h"

using namespace BT;

class Phase3 : public ConditionDecoratorNode
{
public:
	BT_CONDITIONDECORATOR_BODY(Phase3)
	virtual bool ConditionCheck(float deltatime, const BlackBoard& blackBoard) override;
};
