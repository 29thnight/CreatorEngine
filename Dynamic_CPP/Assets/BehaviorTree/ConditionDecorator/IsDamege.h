#include "Core.Minimal.h"
#include "BTHeader.h"
#include "CharacterControllerComponent.h"

using namespace BT;

class IsDamege : public ConditionDecoratorNode
{
public:
	BT_CONDITIONDECORATOR_BODY(IsDamege)
	virtual bool ConditionCheck(float deltatime, const BlackBoard& blackBoard) override;
};
