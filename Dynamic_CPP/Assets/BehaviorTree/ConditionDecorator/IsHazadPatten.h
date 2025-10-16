#include "Core.Minimal.h"
#include "BTHeader.h"

using namespace BT;

class IsHazadPatten : public ConditionDecoratorNode
{
public:
	BT_CONDITIONDECORATOR_BODY(IsHazadPatten)
	virtual bool ConditionCheck(float deltatime, const BlackBoard& blackBoard) override;
};
