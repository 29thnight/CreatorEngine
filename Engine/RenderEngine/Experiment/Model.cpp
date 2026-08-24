#include "Model.h"

#include <utility>

namespace experiment
{
	Model::Model(ModelDraft&& draft, ConstructKey) noexcept
		: metadata_(std::move(draft.metadata)),
		  nodes_(std::move(draft.nodes)),
		  meshes_(std::move(draft.meshes)),
		  materials_(std::move(draft.materials)),
		  skeleton_(std::move(draft.skeleton)),
		  animator_(std::move(draft.animator))
	{
		for (std::size_t index = 0; index < nodes_.size(); ++index)
		{
			if (!nodes_[index].parent.IsValid())
			{
				rootNode_ = NodeIndex(static_cast<NodeIndex::value_type>(index));
				break;
			}
		}
	}

	const ModelMetadata& Model::Metadata() const noexcept { return metadata_; }
	NodeIndex Model::RootNode() const noexcept { return rootNode_; }

	std::span<const ModelNode> Model::Nodes() const noexcept { return nodes_; }
	std::span<const Mesh> Model::Meshes() const noexcept { return meshes_; }
	std::span<const Material> Model::Materials() const noexcept { return materials_; }

	const Skeleton* Model::TryGetSkeleton() const noexcept
	{
		return skeleton_ ? &*skeleton_ : nullptr;
	}

	const AnimatorData* Model::TryGetAnimator() const noexcept
	{
		return animator_ ? &*animator_ : nullptr;
	}

	const ModelNode* Model::TryGetNode(NodeIndex index) const noexcept
	{
		return IsInRange(index, nodes_.size()) ? &nodes_[index.Value()] : nullptr;
	}

	const Mesh* Model::TryGetMesh(MeshIndex index) const noexcept
	{
		return IsInRange(index, meshes_.size()) ? &meshes_[index.Value()] : nullptr;
	}

	const Material* Model::TryGetMaterial(MaterialIndex index) const noexcept
	{
		return IsInRange(index, materials_.size())
			? &materials_[index.Value()] : nullptr;
	}
}
