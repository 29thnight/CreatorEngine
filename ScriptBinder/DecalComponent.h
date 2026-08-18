#pragma once
#include "Core.Minimal.h"
#include "Component.h"

class Texture;
class DecalComponent : public meta::identity<DecalComponent, Component>
{
   public:
   static consteval auto reflect()
   {
       return meta::schema<Self>(
           meta::field<&Self::m_diffusefileName>,
           meta::field<&Self::m_normalFileName>,
           meta::field<&Self::m_ormFileName>,
           meta::field<&Self::m_decalTexture>,
           meta::field<&Self::m_normalTexture>,
           meta::field<&Self::m_occluroughmetalTexture>,
           meta::field<&Self::sliceX>,
           meta::field<&Self::sliceY>,
           meta::field<&Self::sliceNumber>,
           meta::field<&Self::slicePerSeconds>,
           meta::field<&Self::useAnimation>,
           meta::field<&Self::isLoop>);
   }
public:
    DecalComponent() = default;

    void OnInitialized() override;
    void OnUninitializing() override;

    // 트랙 C3: 가상 Update 오버라이드를 걷어내고 DecalSystem(조밀 벡터, 전용
    // 틱)으로 옮겼다 — 등록/해지는 씬 편입/이탈 훅으로 한다(DDOL 안전, 근거는
    // AnimatorSystem.h 주석 참고). Awake/OnDestroy는 렌더 등록(scene->Collect
    // DecalComponent, RegisterCommand)용으로 그대로 둔다(트랙 범위 밖).
    void OnAddedToScene() override;
    void OnRemovingFromScene() override;

    void SetDecalTexture(const std::string_view& fileName);
    void SetDecalTexture(const FileGuid& fileGuid);

    void SetNormalTexture(const std::string_view& fileName);
    void SetNormalTexture(const FileGuid& fileGuid);

    void SetORMTexture(const std::string_view& fileName);
    void SetORMTexture(const FileGuid& fileGuid);

    Texture* GetDecalTexture() { return m_decalTexture; }
    Texture* GetNormalTexture() { return m_normalTexture; }
    // Occlusion, Roughness, Metallic
    Texture* GetORMTexture() { return m_occluroughmetalTexture; }
	const std::shared_ptr<Texture>& GetDecalTextureShared() const { return m_decalTextureOwner; }
	const std::shared_ptr<Texture>& GetNormalTextureShared() const { return m_normalTextureOwner; }
	const std::shared_ptr<Texture>& GetORMTextureShared() const { return m_ormTextureOwner; }

private:
    std::string m_diffusefileName{};
    std::string m_normalFileName{};
    std::string m_ormFileName{};

    Texture* m_decalTexture{};
    Texture* m_normalTexture{};
    Texture* m_occluroughmetalTexture{};

	// 직렬화/인스펙터 호환 raw 별칭은 위에 남기되 실제 수명은 이 셋이 가진다.
	std::shared_ptr<Texture> m_decalTextureOwner{};
	std::shared_ptr<Texture> m_normalTextureOwner{};
	std::shared_ptr<Texture> m_ormTextureOwner{};

public:
    uint32 sliceX = 1;
	uint32 sliceY = 1;
    int sliceNumber = 0;
    float slicePerSeconds = 1.f;
    float timer = 0.f;
    bool useAnimation = false;
    bool isLoop = true;



    ////option 
    //int sliceCount = 1;
    //float2 size = {0.f,0.f};
    //int index = 0;
};

