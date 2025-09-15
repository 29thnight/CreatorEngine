#pragma once
#include "Core.Minimal.h"

enum PlayerStateFlag : flag {
    CanMove,
    CanAttack,
    CanGrab,
    CanThrow,
    CanSwap,
    CanDash,
    MAX
};