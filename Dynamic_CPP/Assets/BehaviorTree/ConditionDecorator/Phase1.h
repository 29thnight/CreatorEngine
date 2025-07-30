#include "Core.Minimal.h"
#include "BTHeader.h"

using namespace BT;

class Phase1 : public ConditionDecoratorNode
{
public:
	BT_CONDITIONDECORATOR_BODY(Phase1)
	virtual bool ConditionCheck(float deltatime, const BlackBoard& blackBoard) override;
};
