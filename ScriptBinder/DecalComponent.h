#pragma once
#include "Core.Minimal.h"
#include "Component.h"
#include "DecalComponent.generated.h"

class Texture;
class DecalComponent : public Component
{
public:
   ReflectDecalComponent
    [[Serializable(Inheritance:Component)]]
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
    [[Property]]
    std::string m_diffusefileName{};
    [[Property]]
    std::string m_normalFileName{};
    [[Property]]
    std::string m_ormFileName{};

    [[Property]]
    Texture* m_decalTexture{};
    [[Property]]
    Texture* m_normalTexture{};
    [[Property]]
    Texture* m_occluroughmetalTexture{};

	// 직렬화/인스펙터 호환 raw 별칭은 위에 남기되 실제 수명은 이 셋이 가진다.
	std::shared_ptr<Texture> m_decalTextureOwner{};
	std::shared_ptr<Texture> m_normalTextureOwner{};
	std::shared_ptr<Texture> m_ormTextureOwner{};

public:
    [[Property]]
    uint32 sliceX = 1;
	[[Property]]
	uint32 sliceY = 1;
	[[Property]]
    int sliceNumber = 0;
    [[Property]]
    float slicePerSeconds = 1.f;
    float timer = 0.f;
    [[Property]]
    bool useAnimation = false;
    [[Property]]
    bool isLoop = true;



    ////option 
    //int sliceCount = 1;
    //float2 size = {0.f,0.f};
    //int index = 0;
};

