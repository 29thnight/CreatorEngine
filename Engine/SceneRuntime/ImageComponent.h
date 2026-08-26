#pragma once
#include "../Utility_Framework/Core.Minimal.h"
#include <mathematics/color.hpp>
#include "Component.h"
#include "IRenderable.h"
#include "UIComponent.h"

//모든 2d이미지 기본?
class Texture;
class UIMesh;
class Canvas;
class ImageComponent : public meta::identity<ImageComponent, UIComponent>
{
   public:
   static consteval auto reflect()
   {
       return meta::schema<Self>(
           meta::field<&Self::texturePaths>,
           meta::field<&Self::color>,
           meta::field<&Self::curindex>,
           meta::field<&Self::rotate>,
           meta::field<&Self::origin>,
           meta::field<&Self::unionScale>,
           meta::field<&Self::clipPercent>,
           meta::field<&Self::clipDirection>,
           meta::field<&Self::useNativeTextureSize>,
           meta::method<&Self::UpdateTexture>);
   }
public:
	ImageComponent();
	~ImageComponent() = default;

	void Load(const std::shared_ptr<Texture>& ptr);
	void DeserializeTexture(const std::shared_ptr<Texture>& ptr);
	void OnDeserialized(); // CT6-d: 텍스처 경로 일괄 로드(구 팩토리 분기)

	virtual void OnInitialized() override;
	virtual void OnUninitializing() override;

	// 레인 UI — 가상 Update 오버라이드를 걷어내고 UITickSystem(조밀 벡터, 전용 틱)
	// 으로 옮겼다. 등록/해지는 씬 편입/이탈 훅으로 한다(DDOL 안전, 근거는
	// UITickSystem.h 주석). OnInitialized/OnUninitializing은 RenderScene 커맨드
	// 등록·UIManager 레지스트리 등록용으로 그대로 둔다(트랙 범위 밖).
	void OnAddedToScene() override;
	void OnRemovingFromScene() override;

	// 옛 Update(float tick)의 본문 그대로(RefreshTransformFromRect() 하나) —
	// UITickSystem::Update가 가드를 통과시킨 뒤 호출한다. 이름을 TickLayout으로
	// 맞춘 이유 둘: ① Component::Update와 이름이 같으면 LifecycleRegistry::
	// MaskOfType이 여전히 Bit_Update를 세워 암묵 구독이 되살아난다(이름이 달라야
	// 감지되지 않는다). ② SpriteSheetComponent::TickLayout·TextComponent::TickLayout과
	// 본문 성격이 동형이다(둘 다 "RectTransform 월드 rect를 읽어 pos/scale을
	// 다시 잡는다") — 서로 다른 클래스라 이름이 같아도 컴파일 충돌은 없고,
	// 오히려 같은 이름을 쓰는 쪽이 "이 셋이 같은 부류의 틱"이라는 사실을
	// 코드에서 바로 드러낸다.
	void TickLayout(float tick);
	void UpdateTexture();
	void SetTexture(int index);

	// RectTransform의 sizeDelta를 현재 텍스처의 픽셀 크기로 맞춘다(uGUI의 SetNativeSize).
	//
	// 예전에는 Awake가 이것을 자동으로 했다. 그러면 저작한 크기를 매번 텍스처 크기가
	// 덮어써서 rect의 크기 필드가 사실상 읽기 전용이 되고, 텍스처가 아직 없으면
	// 0x0으로 무너진다(분석 문서 F-7). 이제는 명시적으로 부를 때만 동작한다.
	void SetNativeSize();

	// 예전 이름. 인스펙터 버튼이 쓰고 있어 남겨 둔다.
	void ResetSize() { SetNativeSize(); }

	bool isThisTextureExist(std::string_view path) const;

	// rect의 결과를 렌더 파라미터(pos·scale·origin)로 옮긴다. Awake와 Update가
	// 같은 코드를 복제하고 있어 한쪽만 고쳐지는 일이 실제로 있었다(Update에는
	// SetSizeDelta 호출이 주석 처리돼 두 경로의 동작이 달랐다).
	void RefreshTransformFromRect();

	const std::vector<std::shared_ptr<Texture>>& GetTextures() const { return textures; }
	const std::vector<std::string>& GetTexturePaths() const { return texturePaths; }
private:
	friend class ProxyCommand;
	friend class UIRenderProxy;
	std::vector<std::string> 				texturePaths;
	std::vector<std::shared_ptr<Texture>>	textures;

public:
	ImageInfo								uiinfo{};
	std::shared_ptr<Texture>				m_curtexture{};
	math::color								color{ 1,1,1,1 };
	int										curindex{ 0 };
	float									rotate{ 0 };
	Mathf::Vector2							origin{};
	float									unionScale{ 1.f };
	float                                   clipPercent{ 1.f };
	ClipDirection                           clipDirection{ ClipDirection::None };

	// 켜면 Awake에서 한 번 SetNativeSize를 부른다 — 예전의 자동 덮어쓰기 동작이다.
	//
	// 기본값이 false인 이유: 현재 에셋 154개의 sizeDelta가 전부 0이 아닌 실제 값이고,
	// 그 값이 곧 저작 당시의 텍스처 크기다. 즉 끄는 쪽이 기존 결과를 그대로 재현하면서
	// 텍스처 로드 타이밍에 흔들리지 않는다. 크기를 텍스처에 맡기고 싶은 이미지만 켠다.
	bool                                    useNativeTextureSize{ false };
};

