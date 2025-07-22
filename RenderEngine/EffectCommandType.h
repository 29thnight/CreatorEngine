#pragma once
#include "Core.Minimal.h"

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
    CreateInstance,
    SetRotation,
};

AUTO_REGISTER_ENUM(EffectCommandType)