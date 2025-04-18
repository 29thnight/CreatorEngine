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


//모든 2d이미지 기본?
class ImageComponent : public UIComponent, public IUpdatable<ImageComponent>, public Meta::IReflectable<ImageComponent>
{
public:
	ImageComponent();
	~ImageComponent() = default;

	void Load(Texture* ptr);
	std::string ToString() const override
	{
		return std::string("ImageComponent");
	}
	virtual void Update(float tick) override;

	void UpdateTexture();
	void SetTexture(int index);
	void Draw(SpriteBatch* sBatch);
	
	ImageInfo uiinfo;
	Texture* m_curtexture{};
	int curindex = 0;
	ReflectionField(ImageComponent, PropertyAndMethod)
	{
		PropertyField
		({
			meta_property(m_IsEnabled)
			meta_property(_layerorder)
			meta_property(curindex)
		});

		MethodField
		({
			meta_method(UpdateTexture)
		});

		ReturnReflection(ImageComponent)
	};
	//ndc좌표 {-1,1}
	Mathf::Vector3 trans{ 0,0,0 };
	Mathf::Vector3 rotat{ 0,0,0 };
private:
	float rotate;
	std::vector<Texture*> textures;
	XMFLOAT2 origin{};

};

