#include "Core.Minimal.h"
#include "BTHeader.h"

using namespace BT;

class WaitAction : public ActionNode
{
public:
	BT_ACTION_BODY(WaitAction)
	virtual NodeStatus Tick(float deltatime, BlackBoard& blackBoard) override;
};
