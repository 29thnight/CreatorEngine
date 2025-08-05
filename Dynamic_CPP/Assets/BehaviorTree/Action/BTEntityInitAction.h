#include "Core.Minimal.h"
#include "BTHeader.h"

using namespace BT;

class BTEntityInitAction : public ActionNode
{
public:
	BT_ACTION_BODY(BTEntityInitAction)
	virtual NodeStatus Tick(float deltatime, BlackBoard& blackBoard) override;
};
