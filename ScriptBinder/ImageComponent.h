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
	void Draw(std::unique_ptr<SpriteBatch>& sBatch);
	
	ImageInfo uiinfo{};
	std::shared_ptr<Texture> m_curtexture{};
    [[Property]]
	int curindex = 0;

	//ndc좌표 {-1,1}
	Mathf::Vector3 trans{ 0,0,0 };
	Mathf::Vector3 rotat{ 0,0,0 };
private:
	friend class ProxyCommand;
	friend class UIRenderProxy;
	float									rotate{ 0 };
	std::vector<std::shared_ptr<Texture>>	textures;
	XMFLOAT2								origin{};

};

