#include "Core.Minimal.h"
#include "BTHeader.h"

using namespace BT;

class IsBossAtteck : public ConditionDecoratorNode
{
public:
	BT_CONDITIONDECORATOR_BODY(IsBossAtteck)
	virtual bool ConditionCheck(float deltatime, const BlackBoard& blackBoard) override;
};
