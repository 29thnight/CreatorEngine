#pragma once
#include "BlackBoard.h"

class AIManager
{
public:
	static AIManager& GetInstance()
	{
		static AIManager instance;
		return instance;
	}
	
	BlackBoard& GetGlobalBlackBoard()
	{
		return m_globalBB;
	}

	void Update(float deltaTime)
	{
		//todo : update all AI components or 
		// global AI logic	or
		// global AI state  or
		// global BlackBoard updates or
		// any other AI related logic
	}

private:
	AIManager() = default;
	~AIManager() = default;
	// Disable copy and move semantics
	AIManager(const AIManager&) = delete;
	AIManager& operator=(const AIManager&) = delete;
	AIManager(AIManager&&) = delete;
	AIManager& operator=(AIManager&&) = delete;

	BlackBoard m_globalBB;
};