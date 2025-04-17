#pragma once
#include "../Utility_Framework/Core.Minimal.h"
#include "Component.h"
#include "IRenderable.h"
#include "IUpdatable.h"
#include "UIComponent.h"
class Texture;
class UIMesh;
class Canvas;

struct alignas(16) UiInfo
{
	Mathf::xMatrix world;
	float2 size;
	float2 screenSize;
 
};


//모든 2d이미지 기본?
class ImageComponent : public UIComponent, public IRenderable, public IUpdatable<ImageComponent>, public Meta::IReflectable<ImageComponent>
{
public:
	ImageComponent();
	~ImageComponent() = default;

	void Load(Texture* ptr);

	std::string ToString() const override
	{
		return std::string("ImageComponent");
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

	//void SetCanvas(Canvas* canvas) { ownerCanvas = canvas; }
	//Canvas* GetOwnerCanvas() { return ownerCanvas;  }
	void UpdateTexture();
	void SetTexture(int index);
	void SetOrder(int index) { _layerorder = index;}

	//다음 방향 오브젝트리턴
	//GameObject* GetNextNavi(Direction dir);

	//void SetNavi(Direction dir, GameObject* other);
	//패드용 네비게이션
	//std::unordered_map<Direction, GameObject*> navigation;

	//숫자가 클수록 젤위에보임
	int _layerorder;
	UiInfo uiinfo;
	UIMesh* m_UIMesh{ nullptr };
	Texture* m_curtexture{};
	int curindex = 0;

	//text 사용 여부
	bool hasText = false;
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
	//화면상 좌표 {1920 / 1080}
	//Mathf::Vector3 pos{960,540,0};
	//ndc좌표 {-1,1}
	Mathf::Vector3 trans{ 0,0,0 };
	Mathf::Vector3 rotat{ 0,0,0 };
	//Mathf::Vector3 scale{ 1,1,1 };
private:
	bool m_IsEnabled =true;

	int rotZ;
	//Canvas* ownerCanvas = nullptr;
	std::vector<Texture*> textures;
	

};

