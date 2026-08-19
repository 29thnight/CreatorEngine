#pragma once
#include "BlackBoard.h"

namespace FSM 
{
	class IScriptedFSM
	{
	public:
		virtual ~IScriptedFSM() = default;
		
		virtual void Enter(BlackBoard& bb) = 0;
		virtual void Update(BlackBoard& bb, float deltaTime) = 0;
		virtual void Exit(BlackBoard& bb) = 0;
	};
}
