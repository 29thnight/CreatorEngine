#pragma once

#include "ModelData.h"

#include <cstdint>
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

		// I6-B0/B2 — 이 generation의 런타임 신원. legacy Skeleton::m_serial이
		// 하던 일을 experiment 축에서 대신한다: BoneComponent가 캐시한 뼈
		// 인덱스가 "어느 스켈레톤에 대해 푼 값인가"를 이 값으로 판별한다.
		//
		// ★ 포인터가 아니라 값인 이유는 legacy 쪽 주석 그대로다 — 해제된 주소를
		//   재사용하면 캐시가 우연히 적중해 **다른 모델의 뼈 인덱스를 조용히
		//   재사용한다**. 생성마다 증가하는 값이면 주소 재사용이 못 속인다.
		//
		// ★ legacy serial과 **번호 공간을 겹치지 않게** 띄운다. 둘 다 1부터
		//   세면 legacy 3에 대해 푼 캐시가 experiment 3을 만나 거짓 적중한다 —
		//   전환기에는 한 씬 안에 두 축이 공존하므로 실재하는 위험이다.
		[[nodiscard]] std::uint64_t Generation() const noexcept;

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
		static std::uint64_t NextGeneration() noexcept;

		std::uint64_t generation_{ NextGeneration() };
		ModelMetadata metadata_{};
		NodeIndex rootNode_{};
		std::vector<ModelNode> nodes_{};
		std::vector<Mesh> meshes_{};
		std::vector<Material> materials_{};
		std::optional<Skeleton> skeleton_{};
		std::optional<AnimatorData> animator_{};
	};
}
