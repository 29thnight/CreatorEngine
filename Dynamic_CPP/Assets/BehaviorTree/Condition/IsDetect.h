#include "Core.Minimal.h"
#include "BTHeader.h"

using namespace BT;

class IsDetect : public ConditionNode
{
public:
	BT_CONDITION_BODY(IsDetect)
	virtual bool ConditionCheck(float deltatime, const BlackBoard& blackBoard) override;
};
