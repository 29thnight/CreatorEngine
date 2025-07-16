#include "Core.Minimal.h"
#include "BTHeader.h"

using namespace BT;

class TestAction : public ActionNode
{
public:
	BT_ACTION_BODY(TestAction)
	virtual NodeStatus Tick(float deltatime, BlackBoard& blackBoard) override;
};
