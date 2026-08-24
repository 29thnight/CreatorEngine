#pragma once

#include "Uuid.h"

#include <array>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace experiment
{
	template <typename Tag>
	class Index final
	{
	public:
		using value_type = std::uint32_t;
		static constexpr value_type InvalidValue =
			(std::numeric_limits<value_type>::max)();

		constexpr Index() noexcept = default;
		explicit constexpr Index(value_type value) noexcept : value_(value) {}

		[[nodiscard]] constexpr bool IsValid() const noexcept
		{
			return value_ != InvalidValue;
		}

		[[nodiscard]] constexpr value_type Value() const noexcept
		{
			return value_;
		}

		explicit constexpr operator bool() const noexcept { return IsValid(); }
		friend constexpr auto operator<=>(const Index&, const Index&) noexcept = default;

	private:
		value_type value_{ InvalidValue };
	};

	struct NodeIndexTag;
	struct MeshIndexTag;
	struct MaterialIndexTag;
	struct BoneIndexTag;
	struct AnimationClipIndexTag;

	using NodeIndex = Index<NodeIndexTag>;
	using MeshIndex = Index<MeshIndexTag>;
	using MaterialIndex = Index<MaterialIndexTag>;
	using BoneIndex = Index<BoneIndexTag>;
	using AnimationClipIndex = Index<AnimationClipIndexTag>;

	template <typename Tag>
	[[nodiscard]] constexpr bool IsInRange(Index<Tag> index, std::size_t size) noexcept
	{
		return index.IsValid() && static_cast<std::size_t>(index.Value()) < size;
	}

	// 기존 FileGuid의 바이트 정체성과 호환되지만, 실험 구조가 TypeTrait의 전역
	// alias/macro 표면을 열지 않도록 UUID 값만 직접 소유한다. 이후 adapter는
	// FileGuid::m_guid와 명시적으로 왕복한다.
	struct AssetId final
	{
		Uuid::Uuid16 value{};

		[[nodiscard]] bool IsValid() const noexcept { return !value.IsNil(); }
		friend auto operator<=>(const AssetId&, const AssetId&) noexcept = default;
	};

	struct Float2 final
	{
		float x{};
		float y{};
	};

	struct Float3 final
	{
		float x{};
		float y{};
		float z{};
	};

	struct Float4 final
	{
		float x{};
		float y{};
		float z{};
		float w{};
	};

	// 행 우선 4x4 값. decoder가 Assimp/legacy 행렬의 전치 여부를 끝내고 이
	// 경계에 넘긴다. Scene이나 RHI의 행렬 타입은 이 데이터 모델에 들어오지 않는다.
	struct Matrix4 final
	{
		std::array<float, 16> rowMajor{
			1.0f, 0.0f, 0.0f, 0.0f,
			0.0f, 1.0f, 0.0f, 0.0f,
			0.0f, 0.0f, 1.0f, 0.0f,
			0.0f, 0.0f, 0.0f, 1.0f
		};
	};

	struct Bounds final
	{
		Float3 minimum{};
		Float3 maximum{};
	};

	struct BoneInfluence final
	{
		BoneIndex bone{};
		float weight{};
	};

	inline constexpr std::size_t MaxBoneInfluences = 4;

	struct Vertex final
	{
		Float3 position{};
		Float3 normal{};
		Float2 uv0{};
		Float2 uv1{};
		Float3 tangent{};
		Float3 bitangent{};
		std::array<BoneInfluence, MaxBoneInfluences> skin{};
	};

	enum class TextureColorSpace : std::uint8_t
	{
		Linear,
		Srgb,
	};

	// 런타임 Texture*가 아니라 안정 ID + 진단/legacy fallback 경로만 보관한다.
	// 실제 texture generation의 소유권은 이후 resource adapter가 결정한다.
	struct TextureReference final
	{
		AssetId assetId{};
		std::string logicalName{};
		std::filesystem::path fallbackPath{};
		TextureColorSpace colorSpace{ TextureColorSpace::Linear };
	};

	using MaterialPropertyValue = std::variant<
		bool,
		std::int32_t,
		std::uint32_t,
		float,
		Float2,
		Float3,
		Float4,
		std::string,
		TextureReference>;

	struct MaterialProperty final
	{
		std::string name{};
		MaterialPropertyValue value{};
	};

	enum class MaterialBlendMode : std::uint8_t
	{
		Opaque,
		Transparent,
	};

	// 고정 texture pointer 5개 대신 shader property 이름을 정본으로 삼는다.
	// M-phase 디스크 정본(shaderMetaGuid + 이름 기반 property + keyword 선택 +
	// rendering mode)과 1:1 대응하도록 유지한다.
	struct Material final
	{
		AssetId assetId{};
		AssetId shaderAssetId{};
		std::string name{};
		MaterialBlendMode blendMode{ MaterialBlendMode::Opaque };
		std::vector<MaterialProperty> properties{};
		std::vector<std::string> keywords{};
		// ShaderMeta 축 순서 기준 선택 인덱스(디스크 정본 호환 표면). 인덱스의
		// 이름 해석은 shaderAssetId의 meta를 소유한 어댑터 책임이다. 이름 기반
		// keywords가 채워져 있으면 그것이 정본이고 이 값은 보조다.
		std::vector<std::uint16_t> keywordSelections{};
	};

	struct Mesh final
	{
		std::string name{};
		MaterialIndex material{};
		std::vector<Vertex> vertices{};
		std::vector<std::uint32_t> indices{};
		Bounds bounds{};
	};

	// parent 하나만 hierarchy 정본이다. children/count 중복 저장을 없앴고,
	// validation은 parent가 항상 자기보다 앞선 index임을 강제한다. 따라서 향후
	// Scene adapter는 단일 순회로 parent EntityHandle을 안전하게 찾을 수 있다.
	struct ModelNode final
	{
		std::string name{};
		NodeIndex parent{};
		Matrix4 localTransform{};
		std::vector<MeshIndex> meshes{};
	};

	struct TranslationKey final
	{
		double time{};
		Float3 value{};
	};

	struct RotationKey final
	{
		double time{};
		Float4 quaternion{};
	};

	struct ScaleKey final
	{
		double time{};
		Float3 value{ 1.0f, 1.0f, 1.0f };
	};

	// 런타임 샘플러가 실제로 계산할 수 있는 것만 담는다. CubicSpline이 여기
	// 없는 것은 누락이 아니라 계약이다 — source가 그것을 들고 오면 변환 경계가
	// 리샘플하거나 강등하고 손실을 계수해야 한다는 뜻이며, 타입이 강제한다.
	// (importer::KeyInterpolation은 source 표현이라 CubicSpline을 갖는다.
	//  이름을 일부러 다르게 둬 둘이 조용히 섞이면 컴파일이 막히게 했다.)
	enum class InterpolationMode : std::uint8_t
	{
		Linear,
		Step,
	};

	struct AnimationChannel final
	{
		BoneIndex bone{};
		// 트랙마다 따로 둔다 — glTF는 sampler가 트랙 단위이므로 한 채널 안에서
		// rotation만 Step이고 translation은 Linear인 경우가 실제로 있다.
		InterpolationMode translationInterpolation{ InterpolationMode::Linear };
		InterpolationMode rotationInterpolation{ InterpolationMode::Linear };
		InterpolationMode scaleInterpolation{ InterpolationMode::Linear };
		std::vector<TranslationKey> translations{};
		std::vector<RotationKey> rotations{};
		std::vector<ScaleKey> scales{};
	};

	struct AnimationClip final
	{
		std::string name{};
		double durationTicks{};
		double ticksPerSecond{};
		bool looping{ true };
		std::vector<AnimationChannel> channels{};
	};

	struct Bone final
	{
		std::string name{};
		BoneIndex parent{};
		Matrix4 inverseBindMatrix{};
	};

	struct Skeleton final
	{
		BoneIndex rootBone{};
		Matrix4 rootTransform{};
		Matrix4 globalInverseTransform{};
		std::vector<Bone> bones{};
		std::vector<AnimationClip> clips{};
	};

	// Skeleton 포인터를 소유하지 않는다. Model이 Skeleton과 AnimatorData를 같은
	// immutable generation 안에서 값으로 보유하고, loader validation이 둘의 관계를
	// 검사한다. skeleton-only bind pose도 합법이므로 이 값 자체가 optional이다.
	struct AnimatorData final
	{
		AssetId motionAssetId{};
		AnimationClipIndex defaultClip{};
	};

	enum class ModelPayloadKind : std::uint8_t
	{
		SourceImport,
		Cooked,
	};

	struct ModelMetadata final
	{
		AssetId assetId{};
		std::string name{};
		std::filesystem::path sourcePath{};
		std::filesystem::path cookedPath{};
		std::filesystem::file_time_type sourceWriteTime{};
		ModelPayloadKind payloadKind{ ModelPayloadKind::SourceImport };
	};

	// Decoder만 수정할 수 있는 완전 소유 staging 값이다. ModelLoader는 이 전체를
	// 검증한 뒤 한 번에 immutable Model generation으로 move한다.
	struct ModelDraft final
	{
		ModelMetadata metadata{};
		std::vector<ModelNode> nodes{};
		std::vector<Mesh> meshes{};
		std::vector<Material> materials{};
		std::optional<Skeleton> skeleton{};
		std::optional<AnimatorData> animator{};
	};
}
