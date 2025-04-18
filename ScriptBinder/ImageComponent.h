#pragma once
#include "../Utility_Framework/Core.Minimal.h"
#include "Component.h"
#include "IRenderable.h"
#include "IUpdatable.h"
#include "UIComponent.h"
#include <DirectXTK/SpriteBatch.h>
class Texture;
class UIMesh;
class Canvas;

struct alignas(16) ImageInfo
{
	Mathf::xMatrix world;
	float2 size;
	float2 screenSize;
 
};

class ImageComponent : public UIComponent, public IUpdatable, public Meta::IReflectable<ImageComponent>
{
public:
	ImageComponent();
	~ImageComponent() = default;

	void Load(Texture* ptr);
	virtual void Update(float tick) override;

	void UpdateTexture();
	void SetTexture(int index);
	void Draw(SpriteBatch* sBatch);
	
	ImageInfo uiinfo;
	Texture* m_curtexture{};
	int curindex = 0;
	ReflectionFieldInheritance(ImageComponent, UIComponent)
	{
		PropertyField
		({
			meta_property(curindex)
		});

		MethodField
		({
			meta_method(UpdateTexture)
		});

		FieldEnd(ImageComponent, PropertyAndMethodInheritance)
	};
private:
	float rotate =0;
	std::vector<Texture*> textures;
	XMFLOAT2 origin{};

};

