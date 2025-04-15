#pragma once
#include "Component.h"
class UIButton : public Component, public Meta::IReflectable<UIButton>
{
public:
	UIButton(std::function<void()> func);
	~UIButton() = default;
	
	std::string ToString() const override
	{
		return std::string("UIButton");
	}
	void SetFunction(std::function<void()> func)
	{
		m_clickFunction = func;
	}
	void Click()
	{
		m_clickFunction();
	}
	ReflectionField(UIButton, MethodOnly)
	{

		MethodField
		({
			meta_method(Click)
			});
		ReturnReflectionMethodOnly(UIButton)
	};

	std::function<void()> m_clickFunction;
};

