#pragma once
#include "Component.h"
#include "../D2Dlib/Canvas.h"
#include "../Utility_Framework/Core.Minimal.h"

class TextRenderComponent final : public Component
{
public:
	TextRenderComponent() = default;
	~TextRenderComponent() = default;
	void Update(float tick) override;
	void Serialize(_inout json& out) override;
	void DeSerialize(_in json& in) override;

private:
	std::string _text;
};