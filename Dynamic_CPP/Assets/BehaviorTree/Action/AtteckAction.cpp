#include "AtteckAction.h"
#include "pch.h"

NodeStatus AtteckAction::Tick(float deltatime, BlackBoard& blackBoard)
{

	// Example action: Print a message to the console
	std::cout << "AtteckAction executed!" << m_owner->GetHashedName().ToString() << std::endl;

	return NodeStatus::Success;
}
