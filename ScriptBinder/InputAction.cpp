#include "InputAction.h"

std::string ActionTypeString(ActionType _actionType)
{

	switch (_actionType)
	{
	case ActionType::Value:
			return "Value";
	case ActionType::Button:
		return "Button";
    default:
        return "None";
	}
}

std::string InputTypeString(InputType _inputType)
{

	switch (_inputType)
	{
	case InputType::GamePad:
		return "GamePad";
	case InputType::KeyBoard:
		return "KeyBoard";
	case InputType::Mouse:
		return "Mouse";
    default:
        return "None";
	}
}

std::string KeyStateString(KeyState _keyState)
{
	switch (_keyState)
	{
	case KeyState::Down:
		return "Down";
	case KeyState::Pressed:
		return "Pressed";
	case KeyState::Released:
		return "Released";
    default:
        return "Idle";
	}
}

std::string ControllerButtonString(ControllerButton _btn)
{
	switch (_btn)
	{
	case ControllerButton::A:                return "A";
	case ControllerButton::B:                return "B";
	case ControllerButton::X:                return "X";
	case ControllerButton::Y:                return "Y";
	case ControllerButton::DPAD_UP:          return "DPad Up";
	case ControllerButton::DPAD_DOWN:        return "DPad Down";
	case ControllerButton::DPAD_LEFT:        return "DPad Left";
	case ControllerButton::DPAD_RIGHT:       return "DPad Right";
	case ControllerButton::START_BUTTON:     return "Start";
	case ControllerButton::BACK_BUTTON:      return "Back";
	case ControllerButton::LEFT_SHOULDER:    return "Left Shoulder";
	case ControllerButton::RIGHT_SHOULDER:   return "Right Shoulder";
	case ControllerButton::LEFT_Thumbstick:  return "Left Thumbstick";
	case ControllerButton::RIGHT_Thumbstick: return "Right Thumbstick";
	case ControllerButton::None:             return "None";
	case ControllerButton::MAX:              return "MAX";
	default:                                 return "Unknown";
	}
}

std::string KeyBoardString(KeyBoard _key)
{
	
    switch (_key)
    {
    case KeyBoard::A:           return "A";
    case KeyBoard::B:           return "B";
    case KeyBoard::C:           return "C";
    case KeyBoard::D:           return "D";
    case KeyBoard::E:           return "E";
    case KeyBoard::F:           return "F";
    case KeyBoard::G:           return "G";
    case KeyBoard::H:           return "H";
    case KeyBoard::I:           return "I";
    case KeyBoard::J:           return "J";
    case KeyBoard::K:           return "K";
    case KeyBoard::L:           return "L";
    case KeyBoard::M:           return "M";
    case KeyBoard::N:           return "N";
    case KeyBoard::O:           return "O";
    case KeyBoard::P:           return "P";
    case KeyBoard::Q:           return "Q";
    case KeyBoard::R:           return "R";
    case KeyBoard::S:           return "S";
    case KeyBoard::T:           return "T";
    case KeyBoard::U:           return "U";
    case KeyBoard::V:           return "V";
    case KeyBoard::W:           return "W";
    case KeyBoard::X:           return "X";
    case KeyBoard::Y:           return "Y";
    case KeyBoard::Z:           return "Z";

    case KeyBoard::LeftArrow:   return "Left Arrow";
    case KeyBoard::UpArrow:     return "Up Arrow";
    case KeyBoard::RightArrow:  return "Right Arrow";
    case KeyBoard::DownArrow:   return "Down Arrow";

    case KeyBoard::Space:       return "Space";
    case KeyBoard::LeftControl: return "Left Control";
    case KeyBoard::RightControl:return "Right Control";
    case KeyBoard::LeftShift:   return "Left Shift";
    case KeyBoard::RightShift:  return "Right Shift";
    case KeyBoard::LeftAlt:     return "Left Alt";
    case KeyBoard::RightAlt:    return "Right Alt";

    case KeyBoard::Enter:       return "Enter";
    case KeyBoard::Backspace:   return "Backspace";
    case KeyBoard::Tab:         return "Tab";
    case KeyBoard::Escape:      return "Escape";
    case KeyBoard::CapsLock:    return "Caps Lock";

    case KeyBoard::Insert:      return "Insert";
    case KeyBoard::Delete:      return "Delete";
    case KeyBoard::Home:        return "Home";
    case KeyBoard::End:         return "End";
    case KeyBoard::PageUp:      return "Page Up";
    case KeyBoard::PageDown:    return "Page Down";

    case KeyBoard::NumLock:     return "Num Lock";
    case KeyBoard::ScrollLock:  return "Scroll Lock";

    case KeyBoard::F1:          return "F1";
    case KeyBoard::F2:          return "F2";
    case KeyBoard::F3:          return "F3";
    case KeyBoard::F4:          return "F4";
    case KeyBoard::F5:          return "F5";
    case KeyBoard::F6:          return "F6";
    case KeyBoard::F7:          return "F7";
    case KeyBoard::F8:          return "F8";
    case KeyBoard::F9:          return "F9";
    case KeyBoard::F10:         return "F10";
    case KeyBoard::F11:         return "F11";
    case KeyBoard::F12:         return "F12";

    case KeyBoard::Numpad0:     return "Numpad 0";
    case KeyBoard::Numpad1:     return "Numpad 1";
    case KeyBoard::Numpad2:     return "Numpad 2";
    case KeyBoard::Numpad3:     return "Numpad 3";
    case KeyBoard::Numpad4:     return "Numpad 4";
    case KeyBoard::Numpad5:     return "Numpad 5";
    case KeyBoard::Numpad6:     return "Numpad 6";
    case KeyBoard::Numpad7:     return "Numpad 7";
    case KeyBoard::Numpad8:     return "Numpad 8";
    case KeyBoard::Numpad9:     return "Numpad 9";

    case KeyBoard::None:        return "None";
    default:                   return "Unknown";
    }


}

void InputAction::SetControllerButton(ControllerButton _btn)
{
	m_controllerButton = _btn;

	key.clear();
	key.resize(4);
	for (int index = 0; index < key.size(); index++)
	{
		key[0] = static_cast<size_t>(_btn);
	}
}
