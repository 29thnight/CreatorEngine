#include "Core.Minimal.h"
#include "BTHeader.h"

using namespace BT;

class IsDaed : public ConditionNode
{
public:
	BT_CONDITION_BODY(IsDaed)
	virtual bool ConditionCheck(float deltatime, const BlackBoard& blackBoard) override;
};
