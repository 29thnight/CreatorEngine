#pragma once
#include "Core.Minimal.h"

enum PlayerStateFlag : flag {
    CanMove,
    CanAttack,
    CanGrab,
    CanDrop,
    CanSwap,
    MAX
};