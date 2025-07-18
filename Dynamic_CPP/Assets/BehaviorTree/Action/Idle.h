#include "Core.Minimal.h"
#include "BTHeader.h"

using namespace BT;

class Idle : public ActionNode
{
public:
	BT_ACTION_BODY(Idle)
	virtual NodeStatus Tick(float deltatime, BlackBoard& blackBoard) override;
};
