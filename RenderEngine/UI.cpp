#include "UI.h"
#include "Texture.h"
UI::UI()
{
}

void UI::LoadUI(std::string_view filepath)
{
	std::shared_ptr<Texture> newTextutre = std::shared_ptr<Texture>(Texture::LoadFormPath(filepath));
	textures.push_back(newTextutre.get());
}
