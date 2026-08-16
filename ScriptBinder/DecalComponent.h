#pragma once
#include "Core.Minimal.h"
#include "Component.h"

class Texture;
class DecalComponent : public Component
{
public:
   static consteval auto describe()
   {
       return meta::describe<DecalComponent>(
           meta::base<Component>(),
           meta::member<&DecalComponent::m_diffusefileName>(),
           meta::member<&DecalComponent::m_normalFileName>(),
           meta::member<&DecalComponent::m_ormFileName>(),
           meta::member<&DecalComponent::m_decalTexture>(),
           meta::member<&DecalComponent::m_normalTexture>(),
           meta::member<&DecalComponent::m_occluroughmetalTexture>(),
           meta::member<&DecalComponent::sliceX>(),
           meta::member<&DecalComponent::sliceY>(),
           meta::member<&DecalComponent::sliceNumber>(),
           meta::member<&DecalComponent::slicePerSeconds>(),
           meta::member<&DecalComponent::useAnimation>(),
           meta::member<&DecalComponent::isLoop>());
   }
    GENERATED_BODY(DecalComponent)

    void Awake() override;
	void Update(float deltaSeconds) override;
    void OnDestroy() override;

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

