#include "Core.Minimal.h"
#include "BTHeader.h"

using namespace BT;

class IsGroggy : public ConditionNode
{
public:
	BT_CONDITION_BODY(IsGroggy)
	virtual bool ConditionCheck(float deltatime, const BlackBoard& blackBoard) override;
};
