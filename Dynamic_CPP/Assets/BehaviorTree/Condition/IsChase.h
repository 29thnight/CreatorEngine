#include "Core.Minimal.h"
#include "BTHeader.h"

using namespace BT;

class IsChase : public ConditionNode
{
public:
	BT_CONDITION_BODY(IsChase)
	virtual bool ConditionCheck(float deltatime, const BlackBoard& blackBoard) override;
};
