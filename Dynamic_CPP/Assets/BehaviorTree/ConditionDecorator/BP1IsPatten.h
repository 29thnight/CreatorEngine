#include "Core.Minimal.h"
#include "BTHeader.h"

using namespace BT;

class BP1IsPatten : public ConditionDecoratorNode
{
public:
	BT_CONDITIONDECORATOR_BODY(BP1IsPatten)
	virtual bool ConditionCheck(float deltatime, const BlackBoard& blackBoard) override;
};
