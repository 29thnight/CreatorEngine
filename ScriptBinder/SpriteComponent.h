#pragma once
#include "../Utility_Framework/Core.Minimal.h"
#include "Component.h"
#include "ILifeSycle.h"
#include "IRenderable.h"

class Texture;
class UIMesh;

struct alignas(16) UiInfo
{
	Mathf::xMatrix world;
	float2 size;
	float2 screenSize;
 
};

//모든 2d이미지 기본?
class SpriteComponent : public Component, public IRenderable, public ILifeSycle
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
	
	virtual void Start() override;
	virtual void Update(float tick) override;
	virtual void FixedUpdate(float fixedTick) override;
	virtual void LateUpdate(float tick) override;

	void UpdateTexture();
	void SetTexture(int index);
	void SetOrder(int index) { _layerorder = index;}

	//	//숫자가 클수록 젤위에보임
	int _layerorder;
	UiInfo uiinfo;
	UIMesh* m_UIMesh{ nullptr };
	Texture* m_curtexture{};
	int curindex = 0;
	
	ReflectionField(SpriteComponent, PropertyAndMethod)
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

		ReturnReflection(SpriteComponent)
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

