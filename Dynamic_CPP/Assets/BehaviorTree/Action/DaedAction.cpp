#include "DaedAction.h"
#include "pch.h"
#include "EffectComponent.h"
#include "EntityMonsterA.h"


NodeStatus DaedAction::Tick(float deltatime, BlackBoard& blackBoard)
{
	// Example action: Print a message to the console
	bool hasState = blackBoard.HasKey("State");
	bool hasIdentity = blackBoard.HasKey("Identity");
	std::string identity = "";
	if (hasIdentity)
	{
		identity = blackBoard.GetValueAsString("Identity");
	}

	if (identity == "MonsterNomal")
	{
		EntityMonsterA* script =m_owner->GetComponent<EntityMonsterA>();
		script->isDead = true;
		return NodeStatus::Success;
	}

	

	std::cout << "DaedAction executed!" << m_owner->GetHashedName().ToString() << std::endl;
	//애니메이션 행동  mowner eney-scropt -> isdead =true  // scrpit에서 죽이기
	

	return NodeStatus::Success;
}
