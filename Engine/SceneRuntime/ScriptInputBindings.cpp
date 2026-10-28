#include "../Containers/ContainerAPI.h"
#include "../Utility_Framework/Foundation/StandardLibrary.h"
#include "ScriptBindingRegistry.h"
#include "InputManager.h"
#if defined(CE_CONTRACT_STANDALONE)
#include "../Platform/WindowsSDK/Creator.WinAPI.h"
#else
import "Creator.WinAPI.h";
#endif

namespace
{
    using namespace ce::script::binding;

    bool GameInputOwned()
    {
        return InputManagement->IsGameInputOwned();
    }

    int __stdcall KeyStateValue(int key)
    {
        if (key < 0 || key >= kKeyboardCount || !GameInputOwned())
        {
            return static_cast<int>(KeyState::Idle);
        }
        return static_cast<int>(InputManagement->m_keyboardState.GetKeyState(static_cast<size_t>(key)));
    }

    int __stdcall ScriptMouseButtonState(int button)
    {
        if (button < 0 || button >= static_cast<int>(MouseKey::MAX) || !GameInputOwned())
        {
            return static_cast<int>(KeyState::Idle);
        }
        const MouseKey key = static_cast<MouseKey>(button);
        if (InputManagement->IsMouseButtonDown(key))
        {
            return static_cast<int>(KeyState::Down);
        }
        if (InputManagement->IsMouseButtonPressed(key))
        {
            return static_cast<int>(KeyState::Pressed);
        }
        if (InputManagement->IsMouseButtonReleased(key))
        {
            return static_cast<int>(KeyState::Released);
        }
        return static_cast<int>(KeyState::Idle);
    }

    int __stdcall ControllerState(int index, int button)
    {
        if (index < 0 || button < 0 || button >= static_cast<int>(ControllerButton::MAX) || !GameInputOwned())
        {
            return static_cast<int>(KeyState::Idle);
        }
        const DWORD pad = static_cast<DWORD>(index);
        const ControllerButton key = static_cast<ControllerButton>(button);
        if (InputManagement->IsControllerButtonDown(pad, key))
        {
            return static_cast<int>(KeyState::Down);
        }
        if (InputManagement->IsControllerButtonPressed(pad, key))
        {
            return static_cast<int>(KeyState::Pressed);
        }
        if (InputManagement->IsControllerButtonReleased(pad, key))
        {
            return static_cast<int>(KeyState::Released);
        }
        return static_cast<int>(KeyState::Idle);
    }

    int __stdcall AnyKey()
    {
        return GameInputOwned() && InputManagement->IsAnyKeyPressed() ? 1 : 0;
    }

    Float2 __stdcall MousePosition()
    {
        const auto value = InputManagement->GetMousePos();
        return { value.x, value.y };
    }

    Float2 __stdcall MouseDelta()
    {
        if (!GameInputOwned())
        {
            return {};
        }
        const auto value = InputManagement->GetMouseDelta();
        return { value.x, value.y };
    }

    int __stdcall WheelDelta()
    {
        return GameInputOwned() ? static_cast<int>(InputManagement->GetWheelDelta()) : 0;
    }

    void __stdcall CursorVisible(int visible)
    {
        if (visible != 0)
        {
            InputManagement->ShowCursor();
        }
        else
        {
            InputManagement->HideCursor();
        }
    }

    int __stdcall ControllerConnected(int index)
    {
        return index >= 0 && InputManagement->IsControllerConnected(static_cast<DWORD>(index)) ? 1 : 0;
    }

    int __stdcall ControllerTriggerLeft(int index)
    {
        return index >= 0 && GameInputOwned() && InputManagement->IsControllerTriggerL(static_cast<DWORD>(index)) ? 1 : 0;
    }

    int __stdcall ControllerTriggerRight(int index)
    {
        return index >= 0 && GameInputOwned() && InputManagement->IsControllerTriggerR(static_cast<DWORD>(index)) ? 1 : 0;
    }

    Float2 __stdcall ControllerThumbLeft(int index)
    {
        if (index < 0 || !GameInputOwned())
        {
            return {};
        }
        const auto value = InputManagement->GetControllerThumbL(static_cast<DWORD>(index));
        return { value.x, value.y };
    }

    Float2 __stdcall ControllerThumbRight(int index)
    {
        if (index < 0 || !GameInputOwned())
        {
            return {};
        }
        const auto value = InputManagement->GetControllerThumbR(static_cast<DWORD>(index));
        return { value.x, value.y };
    }
}

namespace ce::script::binding
{
    void RegisterInputApi(ScriptApiTable& table)
    {
        table.Input_GetKeyState = &KeyStateValue;
        table.Input_GetMouseButtonState = &ScriptMouseButtonState;
        table.Input_GetControllerButtonState = &ControllerState;
        table.Input_IsAnyKeyPressed = &AnyKey;
        table.Input_GetMousePosition = &MousePosition;
        table.Input_GetMouseDelta = &MouseDelta;
        table.Input_GetWheelDelta = &WheelDelta;
        table.Input_SetCursorVisible = &CursorVisible;
        table.Input_IsControllerConnected = &ControllerConnected;
        table.Input_IsControllerTriggerL = &ControllerTriggerLeft;
        table.Input_IsControllerTriggerR = &ControllerTriggerRight;
        table.Input_GetControllerThumbL = &ControllerThumbLeft;
        table.Input_GetControllerThumbR = &ControllerThumbRight;
    }
}
