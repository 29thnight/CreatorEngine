#pragma once
#include "../Utility_Framework/Core.Minimal.h"
#include "../Utility_Framework/Core.Thread.hpp"

class RenderScene;
class Bone;
class Animator;
class Animation;
class NodeAnimation;
class AnimationController;
class AnimationJob
{
public:
    AnimationJob();
    ~AnimationJob();

    void Update(float deltaTime);
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
    void UpdateBlendBone(Bone* bone, Animator& animator, AnimationController* controller, const DirectX::XMMATRIX& Transform, float time ,float nextanitime);
    void UpdateBone(Bone* bone, Animator& animator, AnimationController* controller, const DirectX::XMMATRIX& Transform, float time);
    void UpdateBoneLayer(Bone* bone, Animator& animator,  const DirectX::XMMATRIX& Transform);
    XMMATRIX BlendAni(XMMATRIX curAni, XMMATRIX nextAni, float t);
    XMMATRIX calculAni(NodeAnimation& nodeAnim, float time, int* _key = nullptr);
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

