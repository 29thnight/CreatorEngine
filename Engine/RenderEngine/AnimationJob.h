#pragma once
#include "../Utility_Framework/Core.Minimal.h"
#include "../Utility_Framework/Core.Thread.hpp"
#include <mathematics/matrix4x4.hpp>

class RenderScene;
class Bone;
class Animator;
class Animation;
class NodeAnimation;
class AnimationController;
namespace experiment { struct Skeleton; } // I5-D4e-1
class AnimationJob
{
public:
    AnimationJob();
    ~AnimationJob();

    void Update(float deltaTime);

	// I5-D4e-1 — 패리티 진입점(experiment.animtick 게이트 전용). 같은 시각
	// 입력으로 legacy 재귀(UpdateBone)와 experiment 단일 순회를 각각 돌려
	// m_FinalTransforms 팔레트를 내놓는다 — 제품 틱과 같은 함수를 태우므로
	// 대조가 곧 제품 경로 검증이다. 애니메이터 상태(팔레트·시간축)는 복원한다.
	// experimentSkeleton을 호출자가 준다 — 게이트가 Step→Linear 강등 사본을
	// 먹여 "산술 재현" 축과 "Step 집행 격차(의도)" 축을 분리해 잴 수 있게.
	bool EvaluateParityPose(Animator& animator,
		const experiment::Skeleton& experimentSkeleton, int clipIndex,
		float time, math::matrix4x4* outLegacy, math::matrix4x4* outExperiment);
	// K2: 공유 소유 해체 — Animator는 게임/컴포넌트 측이 소유하고, 여기서는
	// 프레임 안에서만 유효한 관찰용 raw 포인터로 추적한다(등록/해제는 Awake/
	// OnDestroy가 this로 직접 호출). 수명 불변식은 AnimationJob.cpp의
	// SnapshotAnimators 주석 참조.
	void RegisterAnimator(Animator* animator);
	void UnregisterAnimator(Animator* animator);
	size_t GetAnimatorCount() const;
	void Finalize();
private:
	void PrepareAnimation();
    void CleanUp();
	std::vector<Animator*> SnapshotAnimators();
    void UpdateBones(Animator& animator);

    //현재 애니인덱스, 다음애니인덱스, 블렌드지속시간,
    void UpdateBlendBone(Bone* bone, Animator& animator, AnimationController* controller, const math::matrix4x4& transform, float time ,float nextanitime);
    void UpdateBone(Bone* bone, Animator& animator, AnimationController* controller, const math::matrix4x4& transform, float time);
    void UpdateBoneLayer(Bone* bone, Animator& animator, const math::matrix4x4& transform);
    math::matrix4x4 BlendAni(const math::matrix4x4& curAni, const math::matrix4x4& nextAni, float t);
    math::matrix4x4 calculAni(NodeAnimation& nodeAnim, float time, int* _key = nullptr);

	// I5-D4e-1 — experiment 재생 경로. Animator::m_experimentModel이 있으면
	// Update 워커가 legacy 재귀 대신 이것을 탄다. 시간축·이벤트 구조는 legacy와
	// 같고 포즈만 단일 순회다(구현부 주석 참조).
	void TickExperiment(Animator& animator,
		const experiment::Skeleton& skeleton, float deltaT);
	void UpdateExperimentPose(Animator& animator,
		const experiment::Skeleton& skeleton, AnimationController* controller,
		int clipIndex, int nextClipIndex, float time, float nextTime);
	void UpdateExperimentLayer(Animator& animator,
		const experiment::Skeleton& skeleton);
	Core::DelegateHandle m_sceneLoadedHandle;
	Core::DelegateHandle m_sceneUnloadedHandle;
    Core::DelegateHandle m_AnimationUpdateHandle;
    ThreadPool<std::function<void()>>* m_UpdateThreadPool;
    uint32 m_objectSize{};
	mutable std::mutex m_animatorMutex;
	// K2: weak_ptr<Animator>(공유 소유 관찰) → Animator*(프레임-로컬 관찰).
	// Animator가 더 이상 enable_shared_from_this를 상속하지 않으므로 lock()할
	// shared_ptr이 없다 — 등록/해제 시점(Awake/OnDestroy)에 정확히 맞춰
	// 정본을 유지한다.
	std::unordered_map<size_t, Animator*> m_animators;
};

