#pragma once

#include "Uuid.h"
#include "VertexLayout.h"
#include "../Assets/TextureCoordinates.h"

#include <mathematics/bounds.hpp>
#include <mathematics/matrix4x4.hpp>
#include <mathematics/quaternion.hpp>
#include <mathematics/vector2.hpp>
#include <mathematics/vector3.hpp>
#include <mathematics/vector4.hpp>

#include <array>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <type_traits>
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

	// ── 수학 값 타입은 Mathematics 가 정본이다 (MathematicsMigrationPlan S4) ──
	//
	// 예전에는 여기서 Float2/Float3/Float4/Matrix4/Bounds 를 직접 정의했다.
	// "Scene 이나 RHI 의 행렬 타입은 이 데이터 모델에 들어오지 않는다"는 규약을
	// 지키려는 것이었는데, 그 규약은 **DirectXMath 를 들이지 않는다**는 뜻이지
	// 값 타입을 손으로 또 만든다는 뜻이 아니었다.
	//
	// Mathematics 는 그 규약을 그대로 만족한다 — 패킹된 standard-layout 값이고
	// 암시 변환도 없다. V2 이후 Vertex 는 이 값 타입을 사용한 68B GPU 배치다.
	//
	// ★ 별칭을 두지 않는다. 계획 §0 이 "Mathf 에 동일 API 를 재포장하지 않는다"고
	//   정했고, 별칭을 남기면 호출부가 어느 쪽 규약을 따르는지 흐려진다.
	//   호출부는 math::vector3 처럼 정본 이름을 쓴다.
	//
	// ★ 갈아끼우며 **기본값 규약 두 개가 달라졌다**. 조용히 넘어가면 안 된다:
	//   - math::matrix4x4 의 기본은 **영행렬**이다(예전 Matrix4 는 항등).
	//     그래서 아래 저장 필드는 전부 identity() 로 명시 초기화한다.
	//   - math::aabb 는 min/max 가 아니라 **center/extents** 이고 기본이
	//     "빈 상자"(extents 음수 센티널)다. 예전 Bounds{} 는 원점 크기 0 이라
	//     merge 에서 원점을 끌어들였다 — 새 규약이 맞다.
	//     min/max 로 만들 때는 반드시 math::aabb::from_min_max 를 쓴다.

	struct BoneInfluence final
	{
		BoneIndex bone{};
		float weight{};
	};

	inline constexpr std::size_t MaxBoneInfluences = 4;
	using PackedBoneIndex = std::uint8_t;
	inline constexpr PackedBoneIndex InvalidPackedBoneIndex =
		(std::numeric_limits<PackedBoneIndex>::max)();
	inline constexpr std::uint32_t MaxPackedBoneIndex =
		static_cast<std::uint32_t>(InvalidPackedBoneIndex) - 1u;

	struct Vertex final
	{
		math::vector3 position{};
		math::vector3 normal{};
		math::vector2 uv0{};
		// xyz는 단위 tangent, w는 bitangent handedness(-1 또는 +1).
		math::vector4 tangent{};
		// GPU BLENDINDICES/BLENDWEIGHT와 같은 분리 배치. 255는 미사용 slot이다.
		std::array<PackedBoneIndex, MaxBoneInfluences> boneIndices{
			InvalidPackedBoneIndex, InvalidPackedBoneIndex,
			InvalidPackedBoneIndex, InvalidPackedBoneIndex };
		std::array<float, MaxBoneInfluences> boneWeights{};
	};

	// V2 논리 정점과 그 고정 GPU 배치가 어긋나지 않는지 직접 증명한다.
	static_assert(std::is_trivially_copyable_v<Vertex>);
	static_assert(sizeof(Vertex) == StrideOf(kV2VertexAttributes));
	static_assert(offsetof(Vertex, position)
		== OffsetOf(kV2VertexAttributes, VertexAttribute::Position));
	static_assert(offsetof(Vertex, normal)
		== OffsetOf(kV2VertexAttributes, VertexAttribute::Normal));
	static_assert(offsetof(Vertex, uv0)
		== OffsetOf(kV2VertexAttributes, VertexAttribute::Uv0));
	static_assert(offsetof(Vertex, tangent)
		== OffsetOf(kV2VertexAttributes, VertexAttribute::Tangent));
	static_assert(offsetof(Vertex, boneIndices)
		== OffsetOf(kV2VertexAttributes, VertexAttribute::BoneIndices));
	static_assert(offsetof(Vertex, boneWeights)
		== OffsetOf(kV2VertexAttributes, VertexAttribute::BoneWeights));

	// 메시별 interleaved 정점 저장소. 논리 Vertex는 CPU 검사/변환 값이고 실제
	// 저장 바이트는 attribute mask가 정한다. 따라서 static mesh는 core 48B만,
	// skinned mesh는 68B를 내며 uv1/color는 원본에 있을 때만 붙는다.
	class VertexBuffer final
	{
	public:
		VertexBuffer() noexcept = default;
		explicit VertexBuffer(VertexAttributeMask attributes) noexcept
		{
			(void)SetLayout(attributes);
		}

		[[nodiscard]] static constexpr bool IsSupportedLayout(
			VertexAttributeMask attributes) noexcept
		{
			return IsSupportedModelVertexLayout(attributes);
		}

		[[nodiscard]] bool SetLayout(VertexAttributeMask attributes) noexcept
		{
			if (!bytes_.empty() || !IsSupportedLayout(attributes)) return false;
			attributes_ = attributes;
			stride_ = StrideOf(attributes);
			return true;
		}

		[[nodiscard]] VertexAttributeMask AttributeMask() const noexcept
		{
			return attributes_;
		}

		[[nodiscard]] std::uint32_t Stride() const noexcept { return stride_; }
		[[nodiscard]] std::size_t ByteSize() const noexcept { return bytes_.size(); }
		[[nodiscard]] std::size_t size() const noexcept
		{
			return stride_ == 0 ? 0 : bytes_.size() / stride_;
		}
		[[nodiscard]] bool empty() const noexcept { return bytes_.empty(); }

		void reserve(std::size_t vertexCount)
		{
			bytes_.reserve(vertexCount * static_cast<std::size_t>(stride_));
		}

		void push_back(const Vertex& vertex)
		{
			(void)Append(vertex);
		}

		[[nodiscard]] bool Append(const Vertex& vertex,
			const math::vector2* uv1 = nullptr,
			const math::vector4* color = nullptr)
		{
			if (!IsSupportedLayout(attributes_)
				|| (Has(attributes_, VertexAttribute::Uv1) && uv1 == nullptr)
				|| (Has(attributes_, VertexAttribute::Color) && color == nullptr))
			{
				return false;
			}

			const std::size_t base = bytes_.size();
			bytes_.resize(base + stride_);
			Write(base, VertexAttribute::Position, vertex.position);
			Write(base, VertexAttribute::Normal, vertex.normal);
			Write(base, VertexAttribute::Uv0, vertex.uv0);
			if (uv1) Write(base, VertexAttribute::Uv1, *uv1);
			Write(base, VertexAttribute::Tangent, vertex.tangent);
			if (color) Write(base, VertexAttribute::Color, *color);
			Write(base, VertexAttribute::BoneIndices, vertex.boneIndices);
			Write(base, VertexAttribute::BoneWeights, vertex.boneWeights);
			return true;
		}

		[[nodiscard]] Vertex operator[](std::size_t index) const noexcept
		{
			Vertex vertex{};
			const std::size_t base = index * static_cast<std::size_t>(stride_);
			Read(base, VertexAttribute::Position, vertex.position);
			Read(base, VertexAttribute::Normal, vertex.normal);
			Read(base, VertexAttribute::Uv0, vertex.uv0);
			Read(base, VertexAttribute::Tangent, vertex.tangent);
			Read(base, VertexAttribute::BoneIndices, vertex.boneIndices);
			Read(base, VertexAttribute::BoneWeights, vertex.boneWeights);
			return vertex;
		}

		[[nodiscard]] std::optional<math::vector2> Uv1(
			std::size_t index) const noexcept
		{
			if (index >= size() || !Has(attributes_, VertexAttribute::Uv1))
				return std::nullopt;
			math::vector2 value{};
			Read(index * static_cast<std::size_t>(stride_),
				VertexAttribute::Uv1, value);
			return value;
		}

		[[nodiscard]] std::optional<math::vector4> Color(
			std::size_t index) const noexcept
		{
			if (index >= size() || !Has(attributes_, VertexAttribute::Color))
				return std::nullopt;
			math::vector4 value{};
			Read(index * static_cast<std::size_t>(stride_),
				VertexAttribute::Color, value);
			return value;
		}

		[[nodiscard]] std::span<const std::byte> Bytes() const noexcept
		{
			return { bytes_.data(), bytes_.size() };
		}

		[[nodiscard]] bool AssignPacked(VertexAttributeMask attributes,
			std::size_t vertexCount, std::span<const std::byte> bytes)
		{
			if (!bytes_.empty() || !IsSupportedLayout(attributes)) return false;
			const std::uint32_t stride = StrideOf(attributes);
			if (vertexCount > (std::numeric_limits<std::size_t>::max)() / stride
				|| bytes.size() != vertexCount * static_cast<std::size_t>(stride))
			{
				return false;
			}
			attributes_ = attributes;
			stride_ = stride;
			bytes_.assign(bytes.begin(), bytes.end());
			return true;
		}

	private:
		template <typename T>
		void Write(std::size_t base, VertexAttribute attribute,
			const T& value) noexcept
		{
			const std::uint32_t offset = OffsetOf(attributes_, attribute);
			if (offset == kInvalidVertexOffset) return;
			std::memcpy(bytes_.data() + base + offset, &value, sizeof(T));
		}

		template <typename T>
		void Read(std::size_t base, VertexAttribute attribute,
			T& value) const noexcept
		{
			const std::uint32_t offset = OffsetOf(attributes_, attribute);
			if (offset == kInvalidVertexOffset) return;
			std::memcpy(&value, bytes_.data() + base + offset, sizeof(T));
		}

		VertexAttributeMask attributes_{ kCoreVertexAttributes };
		std::uint32_t stride_{ StrideOf(kCoreVertexAttributes) };
		std::vector<std::byte> bytes_{};
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
        assets::TextureCoordinates coordinates{};
	};

	using MaterialPropertyValue = std::variant<
		bool,
		std::int32_t,
		std::uint32_t,
		float,
		math::vector2,
		math::vector3,
		math::vector4,
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
        Masked,
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
		VertexBuffer vertices{};
		std::vector<std::uint32_t> indices{};
		math::aabb bounds{};
	};

	// parent 하나만 hierarchy 정본이다. children/count 중복 저장을 없앴고,
	// validation은 parent가 항상 자기보다 앞선 index임을 강제한다. 따라서 향후
	// Scene adapter는 단일 순회로 parent EntityHandle을 안전하게 찾을 수 있다.
	struct ModelNode final
	{
		std::string name{};
		NodeIndex parent{};
		// ★ math::matrix4x4 의 기본은 영행렬이므로 항등을 명시한다.
		//   비워 두면 변환이 없는 노드가 조용히 모든 것을 원점으로 뭉갠다.
		math::matrix4x4 localTransform{ math::matrix4x4::identity() };
		std::vector<MeshIndex> meshes{};
	};

	struct TranslationKey final
	{
		double time{};
		math::vector3 value{};
	};

	struct RotationKey final
	{
		double time{};
		// ★ vector4 가 아니라 quaternion 이다. 크기는 같지만 규약이 다르다 —
		//   quaternion 의 기본은 항등(0,0,0,1)이고 vector4 의 기본은 영이다.
		//   영 쿼터니언은 회전이 아니므로 합성에 들어가면 결과가 무너진다.
		math::quaternion quaternion{};
	};

	struct ScaleKey final
	{
		double time{};
		math::vector3 value{ 1.0f, 1.0f, 1.0f };
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
		math::matrix4x4 inverseBindMatrix{ math::matrix4x4::identity() };
	};

	struct Skeleton final
	{
		BoneIndex rootBone{};
		math::matrix4x4 rootTransform{ math::matrix4x4::identity() };
		math::matrix4x4 globalInverseTransform{ math::matrix4x4::identity() };
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
