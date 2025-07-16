#include "Core.Minimal.h"
#include "BTHeader.h"

using namespace BT;
class TestConCec : public ConditionDecoratorNode
{
public:
	BT_CONDITIONDECORATOR_BODY(TestConCec)
	virtual bool ConditionCheck(float deltatime, const BlackBoard& blackBoard) override;
};
