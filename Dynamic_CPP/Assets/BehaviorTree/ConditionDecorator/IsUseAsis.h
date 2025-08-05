#include "Core.Minimal.h"
#include "BTHeader.h"

using namespace BT;

class IsUseAsis : public ConditionDecoratorNode
{
public:
	BT_CONDITIONDECORATOR_BODY(IsUseAsis)
	virtual bool ConditionCheck(float deltatime, const BlackBoard& blackBoard) override;
};
