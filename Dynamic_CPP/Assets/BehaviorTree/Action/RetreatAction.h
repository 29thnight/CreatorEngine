#include "Core.Minimal.h"
#include "BTHeader.h"

using namespace BT;

class RetreatAction : public ActionNode
{
public:
	BT_ACTION_BODY(RetreatAction)
	virtual NodeStatus Tick(float deltatime, BlackBoard& blackBoard) override;
};
