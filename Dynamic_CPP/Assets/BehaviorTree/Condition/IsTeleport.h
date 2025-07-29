#include "Core.Minimal.h"
#include "BTHeader.h"

using namespace BT;

class IsTeleport : public ConditionNode
{
public:
	BT_CONDITION_BODY(IsTeleport)
	virtual bool ConditionCheck(float deltatime, const BlackBoard& blackBoard) override;
};
