#pragma once
#include "../Utility_Framework/Core.Minimal.h"
#include <mathematics/matrix4x4.hpp>
#include <thread>
#include <memory>
#include <atomic>
#include <cstdint>
#include <mutex>

class RenderScene;
class Animator;
class AnimationController;
class SceneManager;
struct AnimationHudSnapshot;
namespace assets { class ModelAssetGeneration; } // PHASE 3.75 MBC8
namespace animation { class task_cost_model; }
class AnimationScheduler
{
public:
    explicit AnimationScheduler(SceneManager& owner);
    ~AnimationScheduler();

    void Update(float deltaTime);

	// I6-B4b — 결정적 표본 진입점(experiment.animtick 게이트 전용). 제품
	// 포즈 함수를 그대로 태워 m_FinalTransforms 팔레트를 내놓는다.
	// 애니메이터 상태(팔레트·시간축)는 복원한다.
	//
	// ★ D4e-1의 legacy 대조 팔은 B4b에서 죽었다 — 재귀 틱이 없어졌으므로
	//   대조할 상대가 없다. 남은 축은 골든 digest(게이트 6b)다.
	// PHASE 3.75 MBC8/MBC9 — 결정적 표본 진입점(animtick 골든). experiment 판은
	// MBC9에서 은퇴했다 — 골든 8042DC1C는 이 typed 판이 낸다.
	bool EvaluateGenerationPose(Animator& animator,
		const assets::ModelAssetGeneration& generation, int clipIndex,
		float time, math::matrix4x4* outPose);
	// Animator is owned by the scene. Registration and Update run only on the
	// AnimationScheduler owner thread; Update joins all worker jobs before returning.
	// OnAddedToScene/OnRemovingFromScene provide the DDOL-safe registration edge.
	void RegisterAnimator(Animator* animator);
	void UnregisterAnimator(Animator* animator);
	size_t GetAnimatorCount() const;
	// Profiler requests expire when the live page stops drawing. The UI never
	// reads Animator or task containers directly across the scene thread.
	void RequestHudCapture(std::uint64_t animatorId = 0) noexcept;
	[[nodiscard]] std::shared_ptr<const AnimationHudSnapshot> GetHudSnapshot() const;
	void Finalize();
private:
	SceneManager& m_owner;
	void RequireOwnerThread() const;
	void PrepareAnimation();
    void CleanUp();
	void SnapshotAnimators();
    void UpdateBones(Animator& animator);

    // I6-B4b — legacy 재귀 틱 3종과 calculAni는 제거됐다(본문 373줄).
    // MBC8 — BlendAni는 틱 뷰 템플릿의 자유 함수(BlendPose)가 됐다.

	// 두 그룹 사이의 join 뒤에 실행한다. 데이터 출처는 typed generation 하나다.
	void PrepareGeneration(Animator& animator,
		const assets::ModelAssetGeneration& generation, float deltaT);
	void ExecuteGeneration(Animator& animator,
		const assets::ModelAssetGeneration& generation);
	void InterpolateGeneration(Animator& animator,
		const assets::ModelAssetGeneration& generation);
	void FinalizeGenerationQuality(Animator& animator,
		const assets::ModelAssetGeneration& generation);
	Core::DelegateHandle m_sceneLoadedHandle;
	Core::DelegateHandle m_sceneUnloadedHandle;
    Core::DelegateHandle m_AnimationUpdateHandle;
    uint32 m_objectSize{};
	mutable std::mutex m_animatorMutex;
	// Raw pointers remain valid through Update's in-frame job wait. The mutex
	// permits read-only count queries from other threads; it does not own Animators.
	std::unordered_map<size_t, Animator*> m_animators;
	// Game-thread scratch; Update joins its jobs before the next snapshot.
	std::vector<Animator*> m_snapshotAnimators;
	const std::thread::id m_ownerThreadId{ std::this_thread::get_id() };
	std::unique_ptr<animation::task_cost_model> m_taskCostModel;
	std::atomic<std::int64_t> m_hudRequestedAtNs{};
	std::atomic<std::uint64_t> m_hudTargetAnimatorId{};
	std::uint64_t m_updateSequence{};
	mutable std::mutex m_hudMutex;
	std::shared_ptr<const AnimationHudSnapshot> m_hudPublished;
	struct Scratch;
	std::unique_ptr<Scratch> m_scratch;
};
