#include "Core.Minimal.h"
#include "BTHeader.h"

using namespace BT;

class BP004DeathWorm : public ActionNode
{
public:
	BT_ACTION_BODY(BP004DeathWorm)
	virtual NodeStatus Tick(float deltatime, BlackBoard& blackBoard) override;
};
