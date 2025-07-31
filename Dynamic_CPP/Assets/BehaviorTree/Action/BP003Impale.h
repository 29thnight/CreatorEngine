#include "Core.Minimal.h"
#include "BTHeader.h"

using namespace BT;

class BP003Impale : public ActionNode
{
public:
	BT_ACTION_BODY(BP003Impale)
	virtual NodeStatus Tick(float deltatime, BlackBoard& blackBoard) override;
};
