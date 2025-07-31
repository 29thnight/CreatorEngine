#include "Core.Minimal.h"
#include "BTHeader.h"

using namespace BT;

class IsInitialize : public ConditionDecoratorNode
{
public:
	BT_CONDITIONDECORATOR_BODY(IsInitialize)
	virtual bool ConditionCheck(float deltatime, const BlackBoard& blackBoard) override;
};
