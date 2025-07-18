#include "Core.Minimal.h"
#include "BTHeader.h"

using namespace BT;

class AtteckAction : public ActionNode
{
public:
	BT_ACTION_BODY(AtteckAction)
	virtual NodeStatus Tick(float deltatime, BlackBoard& blackBoard) override;
};
