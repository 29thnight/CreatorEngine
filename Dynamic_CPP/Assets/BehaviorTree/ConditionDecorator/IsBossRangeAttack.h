#include "Core.Minimal.h"
#include "BTHeader.h"

using namespace BT;

class IsBossRangeAttack : public ConditionDecoratorNode
{
public:
	BT_CONDITIONDECORATOR_BODY(IsBossRangeAttack)
	virtual bool ConditionCheck(float deltatime, const BlackBoard& blackBoard) override;
};
