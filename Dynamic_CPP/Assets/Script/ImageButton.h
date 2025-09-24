#pragma once
#include "Core.Minimal.h"
#include "ModuleBehavior.h"
//버튼 함수 상속 전용 스크립트
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
