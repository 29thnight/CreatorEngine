#pragma once
#include "Core.Minimal.h"
#include "ModuleBehavior.h"
//��ư �Լ� ��� ���� ��ũ��Ʈ
class ImageButton : public ModuleBehavior
{
public:
	MODULE_BEHAVIOR_BODY(ImageButton)
	virtual void Start() override;
	virtual void Update(float tick) override;

public:
	virtual void ClickFunction();

protected:
	class ImageComponent* m_imageComponent{ nullptr };
	class UIButton* m_uiButton{ nullptr };
};
