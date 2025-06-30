#pragma once
#include "Core.Minimal.h"
#include "EffectManager.h"

enum class EffectCommandType
{
	Play,
	Stop,
	Pause,
	Resume,
	SetPosition,
	SetTimeScale,
	CreateEffect,
	RemoveEffect,
	UpdateEffectProperty,
};

class EffectManagerProxy
{
public:
	EffectManagerProxy() = default;


private:
	std::function<void()> m_executeFunction;
	EffectCommandType m_commandType;
	std::string m_effectName;
};

