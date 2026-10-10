// Checked-in accessor source for ../Gameplay.inputgraph.
// Source-reviewed only. Re-export from LX and verify in the native ABI gate.
#pragma once
#include "InputGraphTypes.h"

namespace Generated
{
    struct GameplayInputs final
    {
        static constexpr Input::InputSignal<Input::InputVector2> Move{
            { 0x9ad58e309ff74f2aULL, 0xa8c2435a3c126801ULL }, { 0x3217cd6aa3e5272bULL, 10 },
            0x18afb5ca5ee64670ULL, 1, 1 };
        static constexpr Input::InputSignal<Input::Button> Jump{
            { 0x9ad58e309ff74f2aULL, 0xa8c2435a3c126801ULL }, { 0x3217cd6aa3e5272bULL, 20 },
            0x18afb5ca5ee64670ULL, 1, 1 };
        static constexpr Input::InputSignal<Input::InputVector2> Look{
            { 0x9ad58e309ff74f2aULL, 0xa8c2435a3c126801ULL }, { 0x3217cd6aa3e5272bULL, 30 },
            0x18afb5ca5ee64670ULL, 1, 1 };
        struct Layers final
        {
            static constexpr Input::LayerID Gameplay{ 0x3217cd6aa3e5272bULL, 1 };
        };
    };
}
