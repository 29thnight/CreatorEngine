#pragma once
#include "Core.Minimal.h"
#include "ModuleBehavior.h"
#include "ImageButton.generated.h"
//버튼 함수 상속 전용 스크립트
class ImageButton : public ModuleBehavior
{
public:
   ReflectImageButton
	[[ScriptReflectionField]]
	MODULE_BEHAVIOR_BODY(ImageButton)
	virtual void Start() override;
	virtual void Update(float tick) override;

public:
	virtual void ClickFunction();

protected:
	class GameManager* GM = nullptr;
	class ImageComponent* m_imageComponent{ nullptr };
	class UIButton* m_uiButton{ nullptr };
	[[Property]]
	bool m_isTintChange{ false };
};
