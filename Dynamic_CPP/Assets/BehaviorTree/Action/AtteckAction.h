#include "Core.Minimal.h"
#include "BTHeader.h"
#include "CharacterControllerComponent.h"

using namespace BT;

class AtteckAction : public ActionNode
{
public:
	AtteckAction() {
		m_name = "AtteckAction"; m_typeID = TypeTrait::GUIDCreator::GetTypeID<ActionNode>(); m_scriptTypeID = TypeTrait::GUIDCreator::GetTypeID<AtteckAction>();
	} virtual ~AtteckAction() = default;
	virtual NodeStatus Tick(float deltatime, BlackBoard& blackBoard) override;	
};
