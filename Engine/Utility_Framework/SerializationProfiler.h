#pragma once
#include <array>
#include <atomic>
#include <cstdint>
#include <string_view>

// SerializationPlan D0 — 직렬화 기준선 계측의 정본.
//
// 왜 별도 계측인가: PHASE 14 CPU 프로파일러는 프레임 링 히스토리 구조라 "씬 전환 1회"
// 같은 비주기 일회성 구간을 문서에 옮길 결정적 수치로 뽑기에 맞지 않다. 여기서는
// 단계별 누적 시간과 호출 수만 원자적으로 모으고, 판정은 CLI/회귀가 한다.
//
// ★ 이 계측은 제품 로드 경로 안에 직접 있다. 벤치가 경로를 흉내 내면 그 벤치는
//   모형만 재게 된다(dx12.encoderbench 전례). 대신 켜고 끌 수 있게 두어, 평소에는
//   원자 플래그 하나만 읽고 지나간다.
namespace SerializationProfile
{
	enum class Stage : uint32_t
	{
		// 중첩 루트 — 분해 합과 대조하는 "자"다. 아래 단계들을 포함한다.
		SceneLoadTotal = 0,
		SceneParse,          // MetaYml::LoadFile — 텍스트 → Node 트리
		EntityDeserialize,   // Meta::Deserialize(entity, node) — 리플렉션 순회
		ComponentLoad,       // ComponentFactory::LoadComponent — 리플렉션 밖 특례 포함
		AssetCatalog,        // DataSystem::LoadAssetCatalog — 부팅 .meta 전수 파싱
		PrefabInstantiate,   // PrefabUtility::InstantiatePrefab — 소환(중첩 루트)
		PrefabParse,         // PrefabUtility::LoadPrefabFullPath의 LoadFile (캐시 미스만)
		Count
	};

	inline constexpr uint32_t kStageCount = static_cast<uint32_t>(Stage::Count);

	// 표/로그에 쓰는 안정 이름. 회귀 스크립트가 이 문자열로 파싱한다.
	std::string_view StageName(Stage stage);

	// 이 둘만이 SceneLoadTotal/PrefabInstantiate에 포함되는 하위 단계다.
	// 자가 검증(분해 합 <= 루트)의 정의를 코드가 소유하게 해서, 단계를 늘릴 때
	// 문서와 검사가 조용히 어긋나지 않게 한다.
	bool IsSceneLoadChild(Stage stage);

	struct StageSample
	{
		uint64_t nanoseconds{};
		uint64_t calls{};
	};

	struct Snapshot
	{
		std::array<StageSample, kStageCount> stages{};

		const StageSample& operator[](Stage stage) const
		{
			return stages[static_cast<uint32_t>(stage)];
		}
	};

	// 기본 false. 켜기 전 구간은 기록되지 않는다 — 게이트가 "0 calls"를 성공으로
	// 읽지 않도록 회귀에서 호출 수를 함께 단정한다.
	void SetEnabled(bool enabled);
	bool IsEnabled();

	void Reset();
	Snapshot Take();

	// 이미 지나간 부팅 구간(AssetCatalog)처럼 CLI가 켤 수 없는 단계를 위한 창구.
	// Reset이 지우지 않는 별도 슬롯에 보관한다.
	void RecordBootStage(Stage stage, uint64_t nanoseconds, uint64_t calls);
	Snapshot TakeBoot();

	void AddSample(Stage stage, uint64_t nanoseconds);

	// RAII 구간. 비활성이면 시계를 아예 읽지 않는다.
	class Scope
	{
	public:
		explicit Scope(Stage stage);
		~Scope();

		Scope(const Scope&) = delete;
		Scope& operator=(const Scope&) = delete;

	private:
		Stage m_stage;
		bool m_active;
		uint64_t m_startTicks;
	};
}

#define SERIALIZATION_PROFILE_CONCAT_IMPL(x, y) x##y
#define SERIALIZATION_PROFILE_CONCAT(x, y) SERIALIZATION_PROFILE_CONCAT_IMPL(x, y)

#define SERIALIZATION_PROFILE_SCOPE(stage) \
	SerializationProfile::Scope SERIALIZATION_PROFILE_CONCAT(_serializationProfileScope, __LINE__)(stage)
