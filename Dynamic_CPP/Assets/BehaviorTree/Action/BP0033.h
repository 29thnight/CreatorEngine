#include "Core.Minimal.h"
#include "BTHeader.h"

using namespace BT;

class BP0033 : public ActionNode
{
public:
	BT_ACTION_BODY(BP0033)
	virtual NodeStatus Tick(float deltatime, BlackBoard& blackBoard) override;
};
