#include "Core.Minimal.h"
#include "BTHeader.h"

using namespace BT;

class GroggyAction : public ActionNode
{
public:
	BT_ACTION_BODY(GroggyAction)
	virtual NodeStatus Tick(float deltatime, BlackBoard& blackBoard) override;
};
