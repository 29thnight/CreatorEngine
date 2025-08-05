#include "Core.Minimal.h"
#include "BTHeader.h"

using namespace BT;

class BP002FireBall : public ActionNode
{
public:
	BT_ACTION_BODY(BP002FireBall)
	virtual NodeStatus Tick(float deltatime, BlackBoard& blackBoard) override;
};
