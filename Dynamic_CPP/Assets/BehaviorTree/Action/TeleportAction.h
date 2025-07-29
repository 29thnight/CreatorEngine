#include "Core.Minimal.h"
#include "BTHeader.h"

using namespace BT;

class TeleportAction : public ActionNode
{
public:
	BT_ACTION_BODY(TeleportAction)
	virtual NodeStatus Tick(float deltatime, BlackBoard& blackBoard) override;
};
