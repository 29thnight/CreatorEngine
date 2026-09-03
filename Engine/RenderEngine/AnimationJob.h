#pragma once
#include "../Utility_Framework/Core.Minimal.h"
#include "../Utility_Framework/Core.Thread.hpp"
#include <mathematics/matrix4x4.hpp>

class RenderScene;
class Animator;
class AnimationController;
namespace assets { class ModelAssetGeneration; } // PHASE 3.75 MBC8
class AnimationJob
{
public:
    AnimationJob();
    ~AnimationJob();

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

    // I6-B4b — legacy 재귀 틱 3종과 calculAni는 제거됐다(본문 373줄).
    // MBC8 — BlendAni는 틱 뷰 템플릿의 자유 함수(BlendPose)가 됐다.

	// 재생 틱 — 데이터 출처는 typed generation 하나다(구현부 뷰 템플릿 참조).
	void TickGeneration(Animator& animator,
		const assets::ModelAssetGeneration& generation, float deltaT);
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

