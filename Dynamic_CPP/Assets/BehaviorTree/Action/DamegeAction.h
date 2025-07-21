#include "Core.Minimal.h"
#include "BTHeader.h"

using namespace BT;

class DamegeAction : public ActionNode
{
public:
	BT_ACTION_BODY(DamegeAction)
	virtual NodeStatus Tick(float deltatime, BlackBoard& blackBoard) override;
};
