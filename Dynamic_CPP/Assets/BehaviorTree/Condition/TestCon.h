#include "Core.Minimal.h"
#include "BTHeader.h"

using namespace BT;

class TestCon : public ConditionNode
{
public:
	BT_CONDITION_BODY(TestCon)
	virtual bool ConditionCheck(float deltatime, const BlackBoard& blackBoard) override;
};
