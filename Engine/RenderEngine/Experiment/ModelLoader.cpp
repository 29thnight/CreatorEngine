#include "ModelLoader.h"

#include <span>
#include <algorithm>
#include <cmath>
#include <exception>
#include <iterator>
#include <string_view>
#include <type_traits>
#include <unordered_set>
#include <utility>
#include <variant>

namespace experiment
{
	namespace
	{
		void AddIssue(std::vector<ModelLoadIssue>& issues,
			ModelLoadIssueCode code, std::string context, std::string message,
			ModelLoadIssueSeverity severity = ModelLoadIssueSeverity::Error)
		{
			issues.push_back(ModelLoadIssue{
				severity, code, std::move(context), std::move(message) });
		}

		[[nodiscard]] bool HasErrors(const std::vector<ModelLoadIssue>& issues)
		{
			return std::ranges::any_of(issues, [](const ModelLoadIssue& issue)
			{
				return issue.severity == ModelLoadIssueSeverity::Error;
			});
		}

		[[nodiscard]] bool IsFinite(float value) noexcept
		{
			return std::isfinite(value);
		}

		[[nodiscard]] bool IsFinite(double value) noexcept
		{
			return std::isfinite(value);
		}

		[[nodiscard]] bool IsFinite(const math::vector2& value) noexcept
		{
			return IsFinite(value.x) && IsFinite(value.y);
		}

		[[nodiscard]] bool IsFinite(const math::vector3& value) noexcept
		{
			return IsFinite(value.x) && IsFinite(value.y) && IsFinite(value.z);
		}

		[[nodiscard]] bool IsFinite(const math::vector4& value) noexcept
		{
			return IsFinite(value.x) && IsFinite(value.y)
				&& IsFinite(value.z) && IsFinite(value.w);
		}

		// quaternion 은 vector4 와 다른 타입이라 오버로드가 따로 필요하다.
		// 없으면 조용히 안 되는 게 아니라 컴파일이 막힌다 — 그게 맞다.
		[[nodiscard]] bool IsFinite(const math::quaternion& q) noexcept
		{
			return IsFinite(q.x) && IsFinite(q.y) && IsFinite(q.z) && IsFinite(q.w);
		}

		[[nodiscard]] bool IsFinite(const Vertex& vertex) noexcept
		{
			return IsFinite(vertex.position) && IsFinite(vertex.normal)
				&& IsFinite(vertex.uv0) && IsFinite(vertex.tangent);
		}

		[[nodiscard]] bool HasFiniteNumericValue(
			const MaterialPropertyValue& value) noexcept
		{
			return std::visit([](const auto& element) -> bool
			{
				using Alternative = std::remove_cvref_t<decltype(element)>;
				if constexpr (std::is_same_v<Alternative, float>
					|| std::is_same_v<Alternative, math::vector2>
					|| std::is_same_v<Alternative, math::vector3>
					|| std::is_same_v<Alternative, math::vector4>)
				{
					return IsFinite(element);
				}
				else
				{
					return true;
				}
			}, value);
		}

		[[nodiscard]] bool IsFinite(const math::matrix4x4& value) noexcept
		{
			// math::matrix4x4 는 float m[4][4] 라 16개가 연속이다.
			return std::ranges::all_of(std::span<const float>(&value.m[0][0], 16),
				[](float element)
			{
				return IsFinite(element);
			});
		}

		[[nodiscard]] bool IsValid(const math::aabb& bounds) noexcept
		{
			// math::aabb 는 center/extents 다. min<=max 는 extents 가 음수가
			// 아니라는 것과 같고, 그것이 곧 is_empty() 의 부정이다.
			//
			// ★ 빈 상자를 **합법으로 본다.** 정점이 없는 메시가 실제로 있고
			//   (합성 검사가 그 경우를 만든다), 그때 bounds 는 "없음"이 맞다.
			//   예전 Bounds{} 는 원점 크기 0 이라 없음과 원점을 구분하지 못했다.
			if (!IsFinite(bounds.center)) return false;
			if (bounds.is_empty()) return true;
			return IsFinite(bounds.extents);
		}

		template <typename Key>
		[[nodiscard]] bool HasValidKeyTimes(
			const std::vector<Key>& keys, double duration) noexcept
		{
			double previous = 0.0;
			bool first = true;
			for (const Key& key : keys)
			{
				if (!IsFinite(key.time) || key.time < 0.0 || key.time > duration)
					return false;
				// 같은 시간의 중복 key는 0-길이 구간을 만들므로 순증가만 허용한다.
				if (!first && key.time <= previous) return false;
				previous = key.time;
				first = false;
			}
			return true;
		}

		[[nodiscard]] bool HasFiniteKeyValues(const AnimationChannel& channel) noexcept
		{
			const bool translationsOk = std::ranges::all_of(channel.translations,
				[](const TranslationKey& key) { return IsFinite(key.value); });
			const bool rotationsOk = std::ranges::all_of(channel.rotations,
				[](const RotationKey& key)
				{
					const math::quaternion& q = key.quaternion;
					const bool isZero =
						q.x == 0.0f && q.y == 0.0f && q.z == 0.0f && q.w == 0.0f;
					return IsFinite(q) && !isZero;
				});
			const bool scalesOk = std::ranges::all_of(channel.scales,
				[](const ScaleKey& key) { return IsFinite(key.value); });
			return translationsOk && rotationsOk && scalesOk;
		}

		// per-vertex 검사는 같은 코드의 위반을 mesh당 첫 발견 한 건만 보고한다.
		// 손상된 대형 mesh가 정점 수만큼 이슈를 증식시키는 것을 막기 위한 상한이다.
		void ValidateMeshVertices(std::vector<ModelLoadIssue>& issues,
			const Mesh& mesh, const std::string& context,
			const Skeleton* skeleton, std::uint32_t maxBoneInfluences)
		{
			bool reportedVertexAttribute = false;
			bool reportedInvalidWeight = false;
			bool reportedMissingSkeleton = false;
			bool reportedBoneRange = false;
			bool reportedDuplicateBone = false;
			bool reportedInfluenceBudget = false;

			for (std::size_t vertexIndex = 0;
				vertexIndex < mesh.vertices.size(); ++vertexIndex)
			{
				const Vertex vertex = mesh.vertices[vertexIndex];
				const auto vertexContext = [&](const char* suffix)
				{
					return context + ".vertices["
						+ std::to_string(vertexIndex) + "]" + suffix;
				};

				const std::optional<math::vector2> uv1 = mesh.vertices.Uv1(vertexIndex);
				const std::optional<math::vector4> color = mesh.vertices.Color(vertexIndex);
				if (!reportedVertexAttribute && (!IsFinite(vertex)
					|| (uv1 && !IsFinite(*uv1)) || (color && !IsFinite(*color))))
				{
					reportedVertexAttribute = true;
					AddIssue(issues, ModelLoadIssueCode::InvalidVertexAttribute,
						vertexContext(""),
						"정점 attribute에 유한하지 않은 값이 있다.");
				}

				std::uint32_t activeCount = 0;
				std::array<BoneIndex::value_type, MaxBoneInfluences> activeBones{};
				for (std::size_t slot = 0; slot < MaxBoneInfluences; ++slot)
				{
					const float weight = vertex.boneWeights[slot];
					const PackedBoneIndex packedBone = vertex.boneIndices[slot];
					if (!IsFinite(weight) || weight < 0.0f)
					{
						if (!reportedInvalidWeight)
						{
							reportedInvalidWeight = true;
							AddIssue(issues, ModelLoadIssueCode::InvalidBoneInfluence,
								vertexContext(".skin"),
								"bone weight는 유한한 0 이상의 값이어야 한다.");
						}
						continue;
					}
					if (weight == 0.0f) continue;
					if (!skeleton)
					{
						if (!reportedMissingSkeleton)
						{
							reportedMissingSkeleton = true;
							AddIssue(issues,
								ModelLoadIssueCode::MissingSkeletonForSkinning,
								vertexContext(".skin"),
								"양수 bone weight가 있지만 skeleton이 없다.");
						}
						continue;
					}
					const BoneIndex bone = packedBone == InvalidPackedBoneIndex
						? BoneIndex{} : BoneIndex(static_cast<std::uint32_t>(packedBone));
					if (!IsInRange(bone, skeleton->bones.size()))
					{
						if (!reportedBoneRange)
						{
							reportedBoneRange = true;
							AddIssue(issues, ModelLoadIssueCode::InvalidBoneInfluence,
								vertexContext(".skin"),
								"bone index가 skeleton 범위를 벗어났다.");
						}
						continue;
					}
					for (std::uint32_t activeIndex = 0;
						activeIndex < activeCount; ++activeIndex)
					{
						if (activeBones[activeIndex] == bone.Value()
							&& !reportedDuplicateBone)
						{
							reportedDuplicateBone = true;
							AddIssue(issues, ModelLoadIssueCode::InvalidBoneInfluence,
								vertexContext(".skin"),
								"한 정점 안에서 같은 bone influence가 중복됐다.");
						}
					}
					activeBones[activeCount] = bone.Value();
					++activeCount;
				}

				if (activeCount > maxBoneInfluences && !reportedInfluenceBudget)
				{
					reportedInfluenceBudget = true;
					AddIssue(issues, ModelLoadIssueCode::InvalidBoneInfluence,
						vertexContext(".skin"),
						"양수 weight influence 수가 import option 상한을 넘었다.");
				}
			}
		}
	}

	ModelLoader::ModelLoader(std::unique_ptr<IModelDecoder> decoder) noexcept
		: decoder_(std::move(decoder))
	{
	}

	ModelLoadResult ModelLoader::Load(const ModelLoadRequest& request)
	{
		ModelLoadResult result;
		if (!decoder_)
		{
			AddIssue(result.issues, ModelLoadIssueCode::MissingDecoder,
				"request", "ModelLoader에 decoder가 설치되지 않았다.");
			return result;
		}

		bool hasRequiredPath = false;
		switch (request.sourcePreference)
		{
		case ModelSourcePreference::CookedThenSource:
			hasRequiredPath = !request.cookedPath.empty() || !request.sourcePath.empty();
			break;
		case ModelSourcePreference::SourceOnly:
			hasRequiredPath = !request.sourcePath.empty();
			break;
		case ModelSourcePreference::CookedOnly:
			hasRequiredPath = !request.cookedPath.empty();
			break;
		}
		if (!hasRequiredPath)
		{
			AddIssue(result.issues, ModelLoadIssueCode::EmptyRequestPath,
				"request", "선택한 source preference에 필요한 경로가 비어 있다.");
			return result;
		}
		if (request.importOptions.maxBoneInfluences == 0
			|| request.importOptions.maxBoneInfluences > MaxBoneInfluences)
		{
			AddIssue(result.issues, ModelLoadIssueCode::InvalidImportOptions,
				"request.importOptions.maxBoneInfluences",
				"bone influence 수는 1부터 ModelData의 고정 slot 수 이하여야 한다.");
			return result;
		}

		ModelDecodeResult decoded;
		try
		{
			decoded = decoder_->Decode(request);
		}
		catch (const std::exception& exception)
		{
			AddIssue(result.issues, ModelLoadIssueCode::DecoderFailure,
				"decoder", exception.what());
			return result;
		}
		catch (...)
		{
			AddIssue(result.issues, ModelLoadIssueCode::DecoderFailure,
				"decoder", "알 수 없는 예외가 decoder 경계를 넘었다.");
			return result;
		}

		result.issues = std::move(decoded.issues);
		if (!decoded.draft)
		{
			if (!HasErrors(result.issues))
			{
				AddIssue(result.issues, ModelLoadIssueCode::MissingDraft,
					"decoder", "decoder가 오류나 ModelDraft 어느 쪽도 반환하지 않았다.");
			}
			return result;
		}
		if (HasErrors(result.issues)) return result;

		std::vector<ModelLoadIssue> validation =
			Validate(*decoded.draft, request.importOptions);
		result.issues.insert(result.issues.end(),
			std::make_move_iterator(validation.begin()),
			std::make_move_iterator(validation.end()));
		if (HasErrors(result.issues)) return result;

		// 검증 전에는 Model generation이 존재하지 않는다. 성공한 draft만 한 번에
		// move하여 partial model publication을 구조적으로 막는다.
		result.model = std::make_shared<const Model>(
			std::move(*decoded.draft), Model::ConstructKey{});
		return result;
	}

	std::vector<ModelLoadIssue> ModelLoader::Validate(const ModelDraft& draft)
	{
		return Validate(draft, ModelImportOptions{});
	}

	std::vector<ModelLoadIssue> ModelLoader::Validate(
		const ModelDraft& draft, const ModelImportOptions& importOptions)
	{
		std::vector<ModelLoadIssue> issues;
		if (!draft.metadata.assetId.IsValid())
		{
			AddIssue(issues, ModelLoadIssueCode::InvalidAssetIdentity,
				"metadata.assetId", "nil AssetId는 publish할 수 없다.");
		}
		if (draft.metadata.name.empty())
		{
			AddIssue(issues, ModelLoadIssueCode::EmptyModelName,
				"metadata.name", "모델 이름이 비어 있다.");
		}

		if (draft.nodes.empty())
		{
			AddIssue(issues, ModelLoadIssueCode::MissingNodes,
				"nodes", "모델에는 정확히 하나의 root를 포함한 node가 필요하다.");
		}
		else
		{
			std::size_t rootCount = 0;
			for (std::size_t nodeIndex = 0; nodeIndex < draft.nodes.size(); ++nodeIndex)
			{
				const ModelNode& node = draft.nodes[nodeIndex];
				if (!node.parent.IsValid())
				{
					++rootCount;
				}
				else if (node.parent.Value() >= nodeIndex)
				{
					AddIssue(issues, ModelLoadIssueCode::InvalidNodeHierarchy,
						"nodes[" + std::to_string(nodeIndex) + "].parent",
						"parent는 존재하며 반드시 현재 node보다 앞서야 한다.");
				}
				if (!IsFinite(node.localTransform))
				{
					AddIssue(issues, ModelLoadIssueCode::InvalidNodeHierarchy,
						"nodes[" + std::to_string(nodeIndex) + "].localTransform",
						"node transform에 유한하지 않은 값이 있다.");
				}
				for (MeshIndex mesh : node.meshes)
				{
					if (!IsInRange(mesh, draft.meshes.size()))
					{
						AddIssue(issues, ModelLoadIssueCode::InvalidNodeMesh,
							"nodes[" + std::to_string(nodeIndex) + "].meshes",
							"node가 범위를 벗어난 mesh index를 참조한다.");
					}
				}
			}
			if (rootCount != 1)
			{
				AddIssue(issues, ModelLoadIssueCode::InvalidNodeHierarchy,
					"nodes", "node hierarchy에는 정확히 하나의 root가 있어야 한다.");
			}
		}

		for (std::size_t materialIndex = 0;
			materialIndex < draft.materials.size(); ++materialIndex)
		{
			const Material& material = draft.materials[materialIndex];
			const std::string context =
				"materials[" + std::to_string(materialIndex) + "]";
			if (material.name.empty())
			{
				AddIssue(issues, ModelLoadIssueCode::InvalidMaterial,
					context + ".name", "material 이름이 비어 있다.");
			}

			std::unordered_set<std::string> propertyNames;
			for (const MaterialProperty& property : material.properties)
			{
				if (property.name.empty())
				{
					AddIssue(issues, ModelLoadIssueCode::InvalidMaterial,
						context + ".properties", "이름 없는 material property가 있다.");
					continue;
				}
				if (!propertyNames.insert(property.name).second)
				{
					AddIssue(issues, ModelLoadIssueCode::DuplicateMaterialProperty,
						context + ".properties." + property.name,
						"동일한 material property 이름이 중복됐다.");
				}
				if (const auto* texture = std::get_if<TextureReference>(&property.value);
					texture && !texture->assetId.IsValid() && texture->fallbackPath.empty())
				{
					AddIssue(issues, ModelLoadIssueCode::InvalidTextureReference,
						context + ".properties." + property.name,
						"texture property에는 AssetId 또는 fallback path가 필요하다.");
				}
				if (!HasFiniteNumericValue(property.value))
				{
					AddIssue(issues, ModelLoadIssueCode::InvalidMaterial,
						context + ".properties." + property.name,
						"material property의 숫자 값은 유한해야 한다.");
				}
			}

			for (const std::string& keyword : material.keywords)
			{
				if (keyword.empty())
				{
					AddIssue(issues, ModelLoadIssueCode::InvalidMaterial,
						context + ".keywords", "빈 keyword 문자열이 있다.");
					break;
				}
			}
		}

		const Skeleton* skeleton = draft.skeleton ? &*draft.skeleton : nullptr;
		for (std::size_t meshIndex = 0; meshIndex < draft.meshes.size(); ++meshIndex)
		{
			const Mesh& mesh = draft.meshes[meshIndex];
			const std::string context = "meshes[" + std::to_string(meshIndex) + "]";
			if (!VertexBuffer::IsSupportedLayout(mesh.vertices.AttributeMask())
				|| mesh.vertices.Stride() != StrideOf(mesh.vertices.AttributeMask()))
			{
				AddIssue(issues, ModelLoadIssueCode::InvalidMesh, context,
					"vertex attribute mask와 stride가 기술표 계약에 맞지 않는다.");
			}
			if (mesh.vertices.empty() || mesh.indices.empty())
			{
				AddIssue(issues, ModelLoadIssueCode::InvalidMesh, context,
					"publish되는 mesh에는 vertex와 index가 모두 필요하다.");
			}
			if (mesh.material.IsValid()
				&& !IsInRange(mesh.material, draft.materials.size()))
			{
				AddIssue(issues, ModelLoadIssueCode::InvalidMeshMaterial,
					context + ".material", "mesh material index가 범위를 벗어났다.");
			}
			if (!IsValid(mesh.bounds))
			{
				AddIssue(issues, ModelLoadIssueCode::InvalidBounds,
					context + ".bounds", "mesh bounds가 유한하지 않거나 min/max가 뒤집혔다.");
			}
			for (std::uint32_t vertexIndex : mesh.indices)
			{
				if (vertexIndex >= mesh.vertices.size())
				{
					AddIssue(issues, ModelLoadIssueCode::InvalidMeshIndex,
						context + ".indices", "vertex 범위를 벗어난 index가 있다.");
					break;
				}
			}

			ValidateMeshVertices(issues, mesh, context, skeleton,
				importOptions.maxBoneInfluences);
		}

		if (skeleton)
		{
			if (skeleton->bones.empty() || !IsInRange(skeleton->rootBone, skeleton->bones.size()))
			{
				AddIssue(issues, ModelLoadIssueCode::InvalidSkeleton,
					"skeleton.rootBone", "skeleton에는 유효한 root bone이 필요하다.");
			}
			if (!IsFinite(skeleton->rootTransform)
				|| !IsFinite(skeleton->globalInverseTransform))
			{
				AddIssue(issues, ModelLoadIssueCode::InvalidSkeleton,
					"skeleton", "skeleton transform에 유한하지 않은 값이 있다.");
			}

			std::size_t rootCount = 0;
			for (std::size_t boneIndex = 0; boneIndex < skeleton->bones.size(); ++boneIndex)
			{
				const Bone& bone = skeleton->bones[boneIndex];
				if (!bone.parent.IsValid())
				{
					++rootCount;
					if (skeleton->rootBone.IsValid()
						&& skeleton->rootBone.Value() != boneIndex)
					{
						AddIssue(issues, ModelLoadIssueCode::InvalidBoneHierarchy,
							"skeleton.bones[" + std::to_string(boneIndex) + "].parent",
							"parent 없는 bone이 선언된 rootBone과 다르다.");
					}
				}
				else if (bone.parent.Value() >= boneIndex)
				{
					AddIssue(issues, ModelLoadIssueCode::InvalidBoneHierarchy,
						"skeleton.bones[" + std::to_string(boneIndex) + "].parent",
						"bone parent는 존재하며 현재 bone보다 앞서야 한다.");
				}
				if (bone.name.empty() || !IsFinite(bone.inverseBindMatrix))
				{
					AddIssue(issues, ModelLoadIssueCode::InvalidBoneHierarchy,
						"skeleton.bones[" + std::to_string(boneIndex) + "]",
						"bone 이름과 inverse bind matrix가 유효해야 한다.");
				}
			}
			if (rootCount != 1)
			{
				AddIssue(issues, ModelLoadIssueCode::InvalidBoneHierarchy,
					"skeleton.bones", "bone hierarchy에는 정확히 하나의 root가 있어야 한다.");
			}

			for (std::size_t clipIndex = 0; clipIndex < skeleton->clips.size(); ++clipIndex)
			{
				const AnimationClip& clip = skeleton->clips[clipIndex];
				const std::string context =
					"skeleton.clips[" + std::to_string(clipIndex) + "]";
				const bool hasValidTiming = IsFinite(clip.durationTicks)
					&& clip.durationTicks >= 0.0 && IsFinite(clip.ticksPerSecond)
					&& clip.ticksPerSecond > 0.0;
				if (clip.name.empty() || !hasValidTiming)
				{
					AddIssue(issues, ModelLoadIssueCode::InvalidAnimation,
						context, "animation 이름·duration·ticksPerSecond가 유효해야 한다.");
				}

				std::unordered_set<std::uint32_t> channelBones;
				for (const AnimationChannel& channel : clip.channels)
				{
					if (!IsInRange(channel.bone, skeleton->bones.size()))
					{
						AddIssue(issues, ModelLoadIssueCode::InvalidAnimationChannel,
							context + ".channels", "channel bone index가 범위를 벗어났다.");
						continue;
					}
					if (!channelBones.insert(channel.bone.Value()).second)
					{
						AddIssue(issues, ModelLoadIssueCode::InvalidAnimationChannel,
							context + ".channels", "같은 bone의 channel이 중복됐다.");
					}
					// duration이 이미 무효면 시간 검사는 전 채널이 연쇄 실패하므로
					// 후속 소음을 만들지 않는다. 값 유한성은 duration과 무관하다.
					if (hasValidTiming
						&& (!HasValidKeyTimes(channel.translations, clip.durationTicks)
							|| !HasValidKeyTimes(channel.rotations, clip.durationTicks)
							|| !HasValidKeyTimes(channel.scales, clip.durationTicks)))
					{
						AddIssue(issues, ModelLoadIssueCode::InvalidAnimationKey,
							context + ".channels", "key time이 유한하지 않거나 정렬/범위를 위반한다.");
					}
					if (!HasFiniteKeyValues(channel))
					{
						AddIssue(issues, ModelLoadIssueCode::InvalidAnimationKey,
							context + ".channels",
							"key 값이 유한하지 않거나 zero quaternion이 있다.");
					}
				}
			}
		}

		if (draft.animator)
		{
			if (!skeleton)
			{
				AddIssue(issues, ModelLoadIssueCode::InvalidAnimator,
					"animator", "AnimatorData는 같은 Model generation의 Skeleton을 요구한다.");
			}
			else if (draft.animator->defaultClip.IsValid()
				&& !IsInRange(draft.animator->defaultClip, skeleton->clips.size()))
			{
				AddIssue(issues, ModelLoadIssueCode::InvalidAnimator,
					"animator.defaultClip", "default clip index가 범위를 벗어났다.");
			}
		}

		return issues;
	}
}
