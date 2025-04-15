#include "UIButton.h"



UIButton::UIButton(std::function<void()> func)
{
	m_clickFunction = func;
}
