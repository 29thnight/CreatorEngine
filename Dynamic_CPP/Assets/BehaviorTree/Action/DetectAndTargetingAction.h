#include "Core.Minimal.h"
#include "BTHeader.h"

using namespace BT;

class DetectAndTargetingAction : public ActionNode
{
public:
	BT_ACTION_BODY(DetectAndTargetingAction)
	virtual NodeStatus Tick(float deltatime, BlackBoard& blackBoard) override;
};
