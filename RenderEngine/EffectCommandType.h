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
    RemoveEffect,
    SetRotation,
    SetLoop,
    SetDuration,
    ReplaceEffect,
    PlayWithCustomId,
};

AUTO_REGISTER_ENUM(EffectCommandType)