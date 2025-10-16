#include "Core.Minimal.h"
#include "BTHeader.h"

using namespace BT;

class BP0013 : public ActionNode
{
public:
	BT_ACTION_BODY(BP0013)
	virtual NodeStatus Tick(float deltatime, BlackBoard& blackBoard) override;
};
