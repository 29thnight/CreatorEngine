#pragma once
#include "Core.Minimal.h"
#include "Component.h"
#include "IRegistableEvent.h"
#include "DecalComponent.generated.h"

class Texture;
class DecalComponent : public Component, public RegistableEvent<DecalComponent>
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

