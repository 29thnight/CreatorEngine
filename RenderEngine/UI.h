#pragma once
#include "Core.Minimal.h"


//�ܼ� �̹���
class UI
{

public:
	UI();

	void LoadUI(std::string_view filepath);

	std::vector<Texture*> textures;
};

