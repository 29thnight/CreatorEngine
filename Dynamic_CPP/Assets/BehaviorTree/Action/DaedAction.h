#include "Core.Minimal.h"
#include "BTHeader.h"

using namespace BT;

class DaedAction : public ActionNode
{
public:
	BT_ACTION_BODY(DaedAction)
	virtual NodeStatus Tick(float deltatime, BlackBoard& blackBoard) override;
};
