#include "Core.Minimal.h"
#include "BTHeader.h"

using namespace BT;

class IsReteat : public ConditionNode
{
public:
	BT_CONDITION_BODY(IsReteat)
	virtual bool ConditionCheck(float deltatime, const BlackBoard& blackBoard) override;
};
