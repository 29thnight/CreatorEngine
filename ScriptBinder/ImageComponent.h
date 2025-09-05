#pragma once
#include "../Utility_Framework/Core.Minimal.h"
#include "Component.h"
#include "IRenderable.h"
#include "IRegistableEvent.h"
#include "UIComponent.h"
#include "ImageComponent.generated.h"
#include <DirectXTK/SpriteBatch.h>

struct alignas(16) ImageInfo
{
	Mathf::xMatrix world;
	float2 size;
	float2 screenSize;
};

//모든 2d이미지 기본?
class Texture;
class UIMesh;
class Canvas;
class ImageComponent : public UIComponent, public RegistableEvent<ImageComponent>
{
public:
   ReflectImageComponent
    [[Serializable(Inheritance:UIComponent)]]
	ImageComponent();
	~ImageComponent() = default;

	void Load(const std::shared_ptr<Texture>& ptr);
	virtual void Awake() override;
	virtual void Update(float tick) override;
	virtual void OnDestroy() override;
    [[Method]]
	void UpdateTexture();
	void SetTexture(int index);

	bool isThisTextureExist(std::string_view path) const;

	const std::vector<std::shared_ptr<Texture>>& GetTextures() const { return textures; }
	const std::vector<std::string>& GetTexturePaths() const { return texturePaths; }
	
	ImageInfo uiinfo{};
	std::shared_ptr<Texture> m_curtexture{};
    [[Property]]
	int curindex = 0;
	[[Property]]
	Mathf::Color4	color{ 1,1,1,1 };
	[[Property]]
	float			rotate{ 0 };
	[[Property]]
	XMFLOAT2		origin{};
private:
	friend class ProxyCommand;
	friend class UIRenderProxy;
	[[Property]]
	std::vector<std::string> 				texturePaths;
	std::vector<std::shared_ptr<Texture>>	textures;

};

