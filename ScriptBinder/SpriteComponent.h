#pragma once
#include "../Utility_Framework/Core.Minimal.h"
#include "Component.h"
#include "ILifeSycle.h"
#include "IRenderable.h"
#include "IUpdatable.h"

class Texture;
class UIMesh;

cbuffer UiInfo
{
	Mathf::xMatrix world;
	float2 size;
	float2 screenSize;
 
};

//모든 2d이미지 기본?
class SpriteComponent : public Component, public IRenderable, public IUpdatable<SpriteComponent>, public Meta::IReflectable<SpriteComponent>
{
public:
	SpriteComponent();
	~SpriteComponent() = default;

	void Load(std::string_view filepath);

	std::string ToString() const override
	{
		return std::string("SpriteComponent");
	}

	bool IsEnabled() const override
	{
		return m_IsEnabled;
	}

	void SetEnabled(bool able) override
	{
		m_IsEnabled = able;
	}

	virtual void Update(float tick) override;

	void UpdateTexture();
	void SetTexture(int index);
	void SetOrder(int index) { _layerorder = index;}

	int _layerorder;
	UiInfo uiinfo;
	UIMesh* m_UIMesh{ nullptr };
	Texture* m_curtexture{};
	int curindex = 0;
	
	ReflectionField(SpriteComponent)
	{
		PropertyField
		({
			meta_property(m_IsEnabled)
			meta_property(rotat)
			meta_property(_layerorder)
			meta_property(curindex)
		});

		MethodField
		({
			meta_method(UpdateTexture)
		});

		FieldEnd(SpriteComponent, PropertyAndMethod)
	};
private:
	bool m_IsEnabled =true;

	//화면상 좌표 {1920 / 1080}
	Mathf::Vector3 pos{};
	//ndc좌표 {-1,1}
	Mathf::Vector3 trans{ 0,0,0 };
	Mathf::Vector3 rotat{ 0,0,0 };
	Mathf::Vector3 scale{ 1,1,1 };
	int rotZ;
	std::vector<std::shared_ptr<Texture>> textures;
};

