#pragma once

#include <cstdint>

// 관리 코드에 넘기는 객체 핸들.
//
// 관리 코드는 네이티브 포인터의 유효성을 알 수 없다. 슬롯을 재사용할 때 세대를 올려두면
// 파괴된 객체를 가리키던 핸들이 다음 조회에서 자동으로 걸러진다.
// C# 측 ObjectHandle과 배치가 같아야 한다(둘 다 uint32 두 개).
//
// ── EntityHandle과 배치는 같지만 값은 별개다(SceneGraphRedesignPlan 트랙 E4) ──
//
// "C#에 넘기는 핸들이 곧 엔진 핸들"이 트랙 E4의 목표 문구였지만, 이 핸들의 세대를
// Scene::m_generations(EntityHandle의 세대)로 그대로 대체하는 시도는 코드 추적으로
// 기각했다 — DontDestroyOnLoad 이송 창(Scene::DetachEntityHierarchy가 슬롯을
// 먼저 놓고 AttachExistingEntity*가 나중에 다시 잡는 구간) 동안 그 오브젝트는
// 어느 씬의 슬롯에도 없다. 그런데 바로 이 구간에서 실제로 관리 코드가 도는 지점이
// 있다 — SceneManager::LoadSceneImmediate가 Detach 직후·재부착 이전에
// ClrHost::NotifySceneUnload를 부르고, 그 안에서 ScriptRegistry.SweepOrphans가
// 모든 활성 스크립트의 Entity.IsAlive를 확인한다(주석에 "살아 있다 — DDOL
// 포함"이라고 명시되어 있다 — ScriptCore/ScriptRegistry.cs:324). 세대 판정을
// Scene에 위임했다면 이 순간 DDOL 오브젝트가 전부 "죽었다"로 오판되어
// SweepOrphans가 살아있는 스크립트를 매 씬 전환마다 뜯어냈을 것이다 — 자가
// 회귀가 아니라 최초 설계 검토에서 걸러낸 함정이다.
//
// 그래서 이 레지스트리는 자기 세대를 계속 갖는다 — 다만 그 세대를 올리는 지점은
// 하나로 수렴했다: Entity::Destroy()(ScriptObjectRegistry.cpp의 Register/
// Unregister 주석, Entity.cpp:Destroy 참고). Destroy()는 자식까지 재귀하는
// 유일한 진짜 파괴 API이고, DDOL 이송(DetachEntityHierarchy)은 그 경로를
// 타지 않는다 — 그래서 세대가 Scene 것과 별개여도 "진짜 파괴"와 "일시적 씬
// 이탈"을 정확히 가른다. 씬 자체가 통째로 헐릴 때는 Clear()(ClrHost.cpp의
// NotifySceneUnload)가 전 슬롯을 무효화하는 기존 동작을 그대로 유지한다(§1.4
// 보존 대상 — "씬 언로드 경로의 안전성"). 값이 같지 않을 뿐 개념은 여전히
// EntityHandle과 같다 — 슬롯 인덱스 + 세대, 0은 무효.
struct ScriptObjectHandle
{
	uint32_t index{ 0 };
	uint32_t generation{ 0 };   // 0 = 무효

	bool IsValid() const { return generation != 0; }
};

// Flat AssetDepot ABI values. No C++ owner, string, job or resource pointer crosses CLR.
struct ScriptAssetId
{
    std::uint64_t first{};
    std::uint64_t second{};
};

struct ScriptAssetLink
{
    ScriptAssetId asset{};
    ScriptAssetId subasset{};
    std::uint32_t kind{};
    std::uint32_t reserved{};
};

struct ScriptAssetToken
{
    std::uint32_t index{};
    std::uint32_t generation{};
    // Concrete native type proof, independent of ScriptAssetLink::kind. The high
    // bit distinguishes consumer requests from strong owners of that exact type.
    std::uint32_t type{};
};

struct ScriptTextureAssetVariant
{
    std::uint32_t colorSpace{};
    std::uint32_t compress{};
    std::uint32_t role{};
};

struct ScriptAssetRequestSnapshot
{
    std::int32_t status{};
    std::int32_t error{};
    std::int32_t workComplete{};
    std::int32_t messageBytes{};
};

struct ScriptTextureDescriptor
{
    std::uint32_t width{};
    std::uint32_t height{};
    std::uint32_t mipLevels{};
    std::uint32_t arraySize{};
    std::uint32_t isCube{};
};

static_assert(sizeof(ScriptAssetLink) == 40);
static_assert(sizeof(ScriptAssetToken) == 12);
static_assert(sizeof(ScriptTextureAssetVariant) == 12);
static_assert(sizeof(ScriptAssetRequestSnapshot) == 16);
static_assert(sizeof(ScriptTextureDescriptor) == 20);
