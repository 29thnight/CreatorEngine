#pragma once
#include "Delegate.h"

enum class AIType
{
	BT,
	FSM,
};

class IAIComponent
{
public:
	virtual ~IAIComponent() = default;
	// Initialize the AI component
	virtual void Initialize() = 0;

	virtual AIType GetAIType() const { return m_aiType; }
	virtual void InternalAIUpdate(float deltaSecond) {};

	Core::DelegateHandle m_handle{};

protected:
	AIType m_aiType{ AIType::BT }; // Default to Behavior Tree
};