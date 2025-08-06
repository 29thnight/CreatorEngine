#include "Core.Minimal.h"
#include "BTHeader.h"

using namespace BT;

class BP005Breath : public ActionNode
{
public:
	BT_ACTION_BODY(BP005Breath)
	virtual NodeStatus Tick(float deltatime, BlackBoard& blackBoard) override;
};
