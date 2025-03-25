#pragma once
#include "Core.Minimal.h"


//단순 이미지
class UI
{

public:
	UI();

	void LoadUI(std::string_view filepath);

	std::vector<Texture*> textures;
};

