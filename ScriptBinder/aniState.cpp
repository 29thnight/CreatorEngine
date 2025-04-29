#include "aniState.h"
#include "anibegvioutFatory.h"

void aniState::SetBehaviour(std::string name)
{
	behaviour = aniFactory->CreateBehaviour(name);
	behaviourName = behaviour->name;
	behaviour->Owner = this->Owner;
}
