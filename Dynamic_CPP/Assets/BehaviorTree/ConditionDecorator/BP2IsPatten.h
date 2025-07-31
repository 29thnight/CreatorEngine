#include "Core.Minimal.h"
#include "BTHeader.h"

using namespace BT;

class BP2IsPatten : public ConditionDecoratorNode
{
public:
	BT_CONDITIONDECORATOR_BODY(BP2IsPatten)
	virtual bool ConditionCheck(float deltatime, const BlackBoard& blackBoard) override;
};
