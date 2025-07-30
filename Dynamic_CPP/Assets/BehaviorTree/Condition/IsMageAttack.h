#include "Core.Minimal.h"
#include "BTHeader.h"

using namespace BT;

class IsMageAttack : public ConditionNode
{
public:
	BT_CONDITION_BODY(IsMageAttack)
	virtual bool ConditionCheck(float deltatime, const BlackBoard& blackBoard) override;
};
