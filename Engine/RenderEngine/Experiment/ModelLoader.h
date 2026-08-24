#pragma once

#include "Model.h"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace experiment
{
	enum class ModelLoadIssueSeverity : std::uint8_t
	{
		Warning,
		Error,
	};

	enum class ModelLoadIssueCode : std::uint16_t
	{
		MissingDecoder,
		EmptyRequestPath,
		InvalidImportOptions,
		DecoderFailure,
		MissingDraft,
		InvalidAssetIdentity,
		EmptyModelName,
		MissingNodes,
		InvalidNodeHierarchy,
		InvalidNodeMesh,
		InvalidMesh,
		InvalidMeshMaterial,
		InvalidMeshIndex,
		InvalidBounds,
		InvalidMaterial,
		DuplicateMaterialProperty,
		InvalidTextureReference,
		InvalidSkeleton,
		InvalidBoneHierarchy,
		InvalidAnimation,
		InvalidAnimationChannel,
		InvalidAnimationKey,
		MissingSkeletonForSkinning,
		InvalidBoneInfluence,
		InvalidAnimator,
		InvalidVertexAttribute,
	};

	struct ModelLoadIssue final
	{
		ModelLoadIssueSeverity severity{ ModelLoadIssueSeverity::Error };
		ModelLoadIssueCode code{ ModelLoadIssueCode::DecoderFailure };
		std::string context{};
		std::string message{};
	};

	enum class ModelSourcePreference : std::uint8_t
	{
		CookedThenSource,
		SourceOnly,
		CookedOnly,
	};

	struct ModelImportOptions final
	{
		bool optimizeMeshes{};
		bool improveCacheLocality{};
		std::uint32_t maxBoneInfluences{ static_cast<std::uint32_t>(MaxBoneInfluences) };
	};

	struct ModelLoadRequest final
	{
		std::filesystem::path sourcePath{};
		std::filesystem::path cookedPath{};
		ModelSourcePreference sourcePreference{ ModelSourcePreference::CookedThenSource };
		ModelImportOptions importOptions{};
	};

	struct ModelDecodeResult final
	{
		std::optional<ModelDraft> draft{};
		std::vector<ModelLoadIssue> issues{};
	};

	// Assimp/source와 cooked codec은 이 경계 뒤의 별도 구현이다. decoder는 Scene,
	// Entity, DataSystem singleton에 접근하지 않고 완전 소유 ModelDraft만 반환한다.
	class IModelDecoder
	{
	public:
		virtual ~IModelDecoder() = default;
		virtual ModelDecodeResult Decode(const ModelLoadRequest& request) = 0;
	};

	struct ModelLoadResult final
	{
		Model::Shared model{};
		std::vector<ModelLoadIssue> issues{};

		[[nodiscard]] bool Succeeded() const noexcept
		{
			return static_cast<bool>(model);
		}
	};

	// 인스턴스 하나는 동시에 한 Load만 수행한다. 병렬 import는 decoder까지 포함한
	// ModelLoader 인스턴스를 작업마다 하나씩 소유한다. 내부 mutex나 전역 cache는 없다.
	class ModelLoader final
	{
	public:
		explicit ModelLoader(std::unique_ptr<IModelDecoder> decoder) noexcept;
		ModelLoader(const ModelLoader&) = delete;
		ModelLoader& operator=(const ModelLoader&) = delete;
		ModelLoader(ModelLoader&&) noexcept = default;
		ModelLoader& operator=(ModelLoader&&) noexcept = default;
		~ModelLoader() = default;

		[[nodiscard]] ModelLoadResult Load(const ModelLoadRequest& request);

		// 유한성 검사는 node/bone/skeleton transform, bounds, 정점 attribute,
		// skin weight, animation key 시간·값, material 숫자 property를 포함한다.
		// 같은 종류의 per-vertex 위반은 mesh당 첫 발견 한 건만 보고한다.
		// importOptions 오버로드는 decoder가 maxBoneInfluences 제한을 지켰는지도
		// 검사한다. 단일 인자 버전은 ModelData의 고정 slot 수를 상한으로 쓴다.
		[[nodiscard]] static std::vector<ModelLoadIssue> Validate(
			const ModelDraft& draft);
		[[nodiscard]] static std::vector<ModelLoadIssue> Validate(
			const ModelDraft& draft, const ModelImportOptions& importOptions);

	private:
		std::unique_ptr<IModelDecoder> decoder_{};
	};
}
