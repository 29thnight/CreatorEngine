#include "Core.Minimal.h"
#include "BTHeader.h"
#include "CharacterControllerComponent.h"

using namespace BT;

class ChaseAction : public ActionNode
{
public:
	BT_ACTION_BODY(ChaseAction)
	virtual NodeStatus Tick(float deltatime, BlackBoard& blackBoard) override;
};
