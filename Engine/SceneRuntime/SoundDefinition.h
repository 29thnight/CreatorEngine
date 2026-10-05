#pragma once
#include "Reflection.hpp"
#include "CurvePoint.h"

// Serialized ordinals are retained for existing scenes.
enum class ChannelType
{
    BGM = 0,
    SFX,
    PLAYER,
    MONSTER,
    UI,
    MaxChannel
};

enum class Rolloff
{
    Linear = 0,
    Inverse,
    Custom
};
