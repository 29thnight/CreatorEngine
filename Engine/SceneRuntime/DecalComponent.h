#pragma once
#include "Ownership.h"
#include "Core.Minimal.h"
#include "Component.h"
#include "AssetDepot/AssetRequest.h"
#include <array>

class Texture;
class [[reflgen::reflect]] DecalComponent : public meta::identity<DecalComponent, Component>
{
   friend struct reflgen::access;
   public:
    [[reflgen::ignore]]
    void gc_trace(gc::tracer& tracer) const override
    {
        Component::gc_trace(tracer);
    }

public:
    DecalComponent() = default;

    void OnInitialized() override;
    void OnUninitializing() override;

    // 트랙 C3: 가상 Update 오버라이드를 걷어내고 DecalSystem(조밀 벡터, 전용
    // 틱)으로 옮겼다 — 등록/해지는 씬 편입/이탈 훅으로 한다(DDOL 안전, 근거는
    // AnimatorSystem.h 주석 참고). OnInitialized/OnUninitializing은 렌더 등록(scene->Collect
    // DecalComponent, RegisterCommand)용으로 그대로 둔다(트랙 범위 밖).
    void OnAddedToScene() override;
    void OnRemovingFromScene() override;

    void SetDecalTexture(const std::string_view& fileName);
    void SetDecalTexture(const FileGuid& fileGuid);

    void SetNormalTexture(const std::string_view& fileName);
    void SetNormalTexture(const FileGuid& fileGuid);

    void SetORMTexture(const std::string_view& fileName);
    void SetORMTexture(const FileGuid& fileGuid);

    // Owner-thread polling also runs for paused/editor scenes before proxy publication.
    [[reflgen::ignore]]
    void PollTextureRequests();

    const Texture* GetDecalTexture() { return m_decalTexture; }
    const Texture* GetNormalTexture() { return m_normalTexture; }
    // Occlusion, Roughness, Metallic
    const Texture* GetORMTexture() { return m_occluroughmetalTexture; }
	const own::shared_owner<const Texture>& GetDecalTextureShared() const { return m_decalTextureOwner; }
	const own::shared_owner<const Texture>& GetNormalTextureShared() const { return m_normalTextureOwner; }
	const own::shared_owner<const Texture>& GetORMTextureShared() const { return m_ormTextureOwner; }

private:
    [[reflgen::ignore]]
    void RequestTexture(std::string_view reference, std::size_t slot,
        own::shared_owner<const Texture>& owner, const Texture*& alias);
    [[reflgen::ignore]]
    void ReleaseManagedResources() override;

    [[reflgen::ignore]]
    std::array<AssetDepot::AssetRequest<Texture>, 3> m_textureRequests{};
    [[reflgen::ignore]]
    std::array<bool, 3> m_texturePending{};
    [[reflgen::ignore]]
    std::array<bool, 3> m_textureRetryOnRevision{};
    [[reflgen::ignore]]
    std::array<std::uint64_t, 3> m_textureResolverRevisions{};

    std::string m_diffusefileName{};
    std::string m_normalFileName{};
    std::string m_ormFileName{};

    const Texture* m_decalTexture{};
    const Texture* m_normalTexture{};
    const Texture* m_occluroughmetalTexture{};

	// 직렬화/인스펙터 호환 raw 별칭은 위에 남기되 실제 수명은 이 셋이 가진다.
	[[reflgen::ignore]]
	own::shared_owner<const Texture> m_decalTextureOwner{};

	[[reflgen::ignore]]
	own::shared_owner<const Texture> m_normalTextureOwner{};

	[[reflgen::ignore]]
	own::shared_owner<const Texture> m_ormTextureOwner{};

public:
    uint32 sliceX = 1;
	uint32 sliceY = 1;
    int sliceNumber = 0;
    float slicePerSeconds = 1.f;

    [[reflgen::ignore]]
    float timer = 0.f;

    bool useAnimation = false;
    bool isLoop = true;



    ////option 
    //int sliceCount = 1;
    //float2 size = {0.f,0.f};
    //int index = 0;
};

