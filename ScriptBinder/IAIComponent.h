#pragma once

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

protected:
	AIType m_aiType{ AIType::BT }; // Default to Behavior Tree
};