#include "Core.Minimal.h"
#include "BTHeader.h"

using namespace BT;

class BossIdleAction : public ActionNode
{
public:
	BT_ACTION_BODY(BossIdleAction)
	virtual NodeStatus Tick(float deltatime, BlackBoard& blackBoard) override;
};
