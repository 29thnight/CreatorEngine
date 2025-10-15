#include "Core.Minimal.h"
#include "BTHeader.h"

using namespace BT;

class BurrowMove : public ActionNode
{
public:
	BT_ACTION_BODY(BurrowMove)
	virtual NodeStatus Tick(float deltatime, BlackBoard& blackBoard) override;
};
