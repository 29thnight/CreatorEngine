#include "Core.Minimal.h"
#include "BTHeader.h"

using namespace BT;

class KnockBackAction : public ActionNode
{
public:
	BT_ACTION_BODY(KnockBackAction)
	virtual NodeStatus Tick(float deltatime, BlackBoard& blackBoard) override;
};
