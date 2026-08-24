#pragma once

#include "ModelData.h"

#include <memory>
#include <span>

namespace experiment
{
	class ModelLoader;

	// 완전히 검증된 단일 asset generation. 생성 이후 public mutation이 없고,
	// 소비자는 shared_ptr<const Model>을 보관한다. Scene 수명과는 연결하지 않는다.
	class Model final
	{
	public:
		using Shared = std::shared_ptr<const Model>;

		// pass-key: 생성은 ModelLoader만 가능하지만 생성자 자체는 public이므로
		// make_shared가 컨트롤 블록과 객체를 단일 할당으로 잡을 수 있다.
		class ConstructKey final
		{
		private:
			friend class ModelLoader;
			ConstructKey() noexcept = default;
		};

		Model(ModelDraft&& draft, ConstructKey) noexcept;

		Model(const Model&) = delete;
		Model& operator=(const Model&) = delete;
		Model(Model&&) = delete;
		Model& operator=(Model&&) = delete;
		~Model() = default;

		[[nodiscard]] const ModelMetadata& Metadata() const noexcept;
		[[nodiscard]] NodeIndex RootNode() const noexcept;
		[[nodiscard]] std::span<const ModelNode> Nodes() const noexcept;
		[[nodiscard]] std::span<const Mesh> Meshes() const noexcept;
		[[nodiscard]] std::span<const Material> Materials() const noexcept;
		[[nodiscard]] const Skeleton* TryGetSkeleton() const noexcept;
		[[nodiscard]] const AnimatorData* TryGetAnimator() const noexcept;

		[[nodiscard]] const ModelNode* TryGetNode(NodeIndex index) const noexcept;
		[[nodiscard]] const Mesh* TryGetMesh(MeshIndex index) const noexcept;
		[[nodiscard]] const Material* TryGetMaterial(MaterialIndex index) const noexcept;

	private:
		ModelMetadata metadata_{};
		NodeIndex rootNode_{};
		std::vector<ModelNode> nodes_{};
		std::vector<Mesh> meshes_{};
		std::vector<Material> materials_{};
		std::optional<Skeleton> skeleton_{};
		std::optional<AnimatorData> animator_{};
	};
}
