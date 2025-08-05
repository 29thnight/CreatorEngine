#include "Core.Minimal.h"
#include "BTHeader.h"

using namespace BT;

class BP001BodyAtack : public ActionNode
{
public:
	BT_ACTION_BODY(BP001BodyAtack)
	virtual NodeStatus Tick(float deltatime, BlackBoard& blackBoard) override;
};
