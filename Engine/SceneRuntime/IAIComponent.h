#pragma once
#if defined(CE_CONTRACT_STANDALONE)
#include "../../Tools/build/ModuleAdapters/Creator.Core.Delegate.h"
#else
import Creator.Core.Delegate;
#endif

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

	ce::delegate_handle m_handle{};

protected:
	AIType m_aiType{ AIType::BT }; // Default to Behavior Tree
};