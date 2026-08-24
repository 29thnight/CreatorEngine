#pragma once
#include "IScriptedFSM.h"

namespace FSM
{
	class FSMState : public IScriptedFSM
	{
	public:
		FSMState(const std::string& stateName) : m_stateName(stateName) {}
		virtual ~FSMState() = default;
		// Called when entering the state
		virtual void Enter(BlackBoard& bb) override
		{
			// Default implementation can be empty or contain common logic
		}
		// Called every frame while in the state
		virtual void Update(BlackBoard& bb, float deltaTime) override
		{
			// Default implementation can be empty or contain common logic
		}
		// Called when exiting the state
		virtual void Exit(BlackBoard& bb) override
		{
			// Default implementation can be empty or contain common logic
		}

		const std::string& GetName() const
		{
			return m_stateName;
		}

		void SetName(const std::string& name)
		{
			m_stateName = name;
		}

	private:
		std::string m_stateName;
	};
}