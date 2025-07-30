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

ActionType ParseActionType(const std::string& str)
{
    if (str == "Value") return ActionType::Value;
    if (str == "Button") return ActionType::Button;
    return ActionType::Button;
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

InputType ParseInputType(const std::string& str)
{
    if (str == "GamePad")   return InputType::GamePad;
    if (str == "KeyBoard")  return InputType::KeyBoard;
    if (str == "Mouse")     return InputType::Mouse;
    return InputType::KeyBoard;
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

KeyState ParseKeyState(const std::string& str)
{
    if (str == "Down")     return KeyState::Down;
    if (str == "Pressed")  return KeyState::Pressed;
    if (str == "Released") return KeyState::Released;
    return KeyState::Down; // 기본값 또는 오류 처리용
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

ControllerButton ParseControllerButton(const std::string& str)
{
    if (str == "A")                return ControllerButton::A;
    if (str == "B")                return ControllerButton::B;
    if (str == "X")                return ControllerButton::X;
    if (str == "Y")                return ControllerButton::Y;
    if (str == "DPad Up")          return ControllerButton::DPAD_UP;
    if (str == "DPad Down")        return ControllerButton::DPAD_DOWN;
    if (str == "DPad Left")        return ControllerButton::DPAD_LEFT;
    if (str == "DPad Right")       return ControllerButton::DPAD_RIGHT;
    if (str == "Start")            return ControllerButton::START_BUTTON;
    if (str == "Back")             return ControllerButton::BACK_BUTTON;
    if (str == "Left Shoulder")    return ControllerButton::LEFT_SHOULDER;
    if (str == "Right Shoulder")   return ControllerButton::RIGHT_SHOULDER;
    if (str == "Left Thumbstick")  return ControllerButton::LEFT_Thumbstick;
    if (str == "Right Thumbstick") return ControllerButton::RIGHT_Thumbstick;
    if (str == "None")             return ControllerButton::None;
    if (str == "MAX")              return ControllerButton::MAX;
    return ControllerButton::None; // 기본값 혹은 오류 처리용
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

KeyBoard ParseKeyBoard(const std::string& str)
{
    if (str == "A")               return KeyBoard::A;
    if (str == "B")               return KeyBoard::B;
    if (str == "C")               return KeyBoard::C;
    if (str == "D")               return KeyBoard::D;
    if (str == "E")               return KeyBoard::E;
    if (str == "F")               return KeyBoard::F;
    if (str == "G")               return KeyBoard::G;
    if (str == "H")               return KeyBoard::H;
    if (str == "I")               return KeyBoard::I;
    if (str == "J")               return KeyBoard::J;
    if (str == "K")               return KeyBoard::K;
    if (str == "L")               return KeyBoard::L;
    if (str == "M")               return KeyBoard::M;
    if (str == "N")               return KeyBoard::N;
    if (str == "O")               return KeyBoard::O;
    if (str == "P")               return KeyBoard::P;
    if (str == "Q")               return KeyBoard::Q;
    if (str == "R")               return KeyBoard::R;
    if (str == "S")               return KeyBoard::S;
    if (str == "T")               return KeyBoard::T;
    if (str == "U")               return KeyBoard::U;
    if (str == "V")               return KeyBoard::V;
    if (str == "W")               return KeyBoard::W;
    if (str == "X")               return KeyBoard::X;
    if (str == "Y")               return KeyBoard::Y;
    if (str == "Z")               return KeyBoard::Z;

    if (str == "Left Arrow")      return KeyBoard::LeftArrow;
    if (str == "Up Arrow")        return KeyBoard::UpArrow;
    if (str == "Right Arrow")     return KeyBoard::RightArrow;
    if (str == "Down Arrow")      return KeyBoard::DownArrow;

    if (str == "Space")           return KeyBoard::Space;
    if (str == "Left Control")    return KeyBoard::LeftControl;
    if (str == "Right Control")   return KeyBoard::RightControl;
    if (str == "Left Shift")      return KeyBoard::LeftShift;
    if (str == "Right Shift")     return KeyBoard::RightShift;
    if (str == "Left Alt")        return KeyBoard::LeftAlt;
    if (str == "Right Alt")       return KeyBoard::RightAlt;

    if (str == "Enter")           return KeyBoard::Enter;
    if (str == "Backspace")       return KeyBoard::Backspace;
    if (str == "Tab")             return KeyBoard::Tab;
    if (str == "Escape")          return KeyBoard::Escape;
    if (str == "Caps Lock")       return KeyBoard::CapsLock;

    if (str == "Insert")          return KeyBoard::Insert;
    if (str == "Delete")          return KeyBoard::Delete;
    if (str == "Home")            return KeyBoard::Home;
    if (str == "End")             return KeyBoard::End;
    if (str == "Page Up")         return KeyBoard::PageUp;
    if (str == "Page Down")       return KeyBoard::PageDown;

    if (str == "Num Lock")        return KeyBoard::NumLock;
    if (str == "Scroll Lock")     return KeyBoard::ScrollLock;

    if (str == "F1")              return KeyBoard::F1;
    if (str == "F2")              return KeyBoard::F2;
    if (str == "F3")              return KeyBoard::F3;
    if (str == "F4")              return KeyBoard::F4;
    if (str == "F5")              return KeyBoard::F5;
    if (str == "F6")              return KeyBoard::F6;
    if (str == "F7")              return KeyBoard::F7;
    if (str == "F8")              return KeyBoard::F8;
    if (str == "F9")              return KeyBoard::F9;
    if (str == "F10")             return KeyBoard::F10;
    if (str == "F11")             return KeyBoard::F11;
    if (str == "F12")             return KeyBoard::F12;

    if (str == "Numpad 0")        return KeyBoard::Numpad0;
    if (str == "Numpad 1")        return KeyBoard::Numpad1;
    if (str == "Numpad 2")        return KeyBoard::Numpad2;
    if (str == "Numpad 3")        return KeyBoard::Numpad3;
    if (str == "Numpad 4")        return KeyBoard::Numpad4;
    if (str == "Numpad 5")        return KeyBoard::Numpad5;
    if (str == "Numpad 6")        return KeyBoard::Numpad6;
    if (str == "Numpad 7")        return KeyBoard::Numpad7;
    if (str == "Numpad 8")        return KeyBoard::Numpad8;
    if (str == "Numpad 9")        return KeyBoard::Numpad9;

    if (str == "None")            return KeyBoard::None;

    return KeyBoard::A; // 기본값 혹은 에러 처리용
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
