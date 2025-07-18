#include "Core.Minimal.h"
#include "BTHeader.h"

using namespace BT;

class IsAtteck : public ConditionNode
{
public:
	BT_CONDITION_BODY(IsAtteck)
	virtual bool ConditionCheck(float deltatime, const BlackBoard& blackBoard) override;
};
