#include "Core.Minimal.h"
#include "BTHeader.h"

using namespace BT;

class Phase2 : public ConditionDecoratorNode
{
public:
	BT_CONDITIONDECORATOR_BODY(Phase2)
	virtual bool ConditionCheck(float deltatime, const BlackBoard& blackBoard) override;
};
