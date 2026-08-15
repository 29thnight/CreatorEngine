#pragma once
#ifndef DYNAMICCPP_EXPORTS
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
	void RegisterAnimator(const std::shared_ptr<Animator>& animator);
	void UnregisterAnimator(const std::shared_ptr<Animator>& animator);
	size_t GetAnimatorCount() const;
	void Finalize();
private:
	void PrepareAnimation();
    void CleanUp();
	std::vector<std::shared_ptr<Animator>> SnapshotAnimators();
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
	std::unordered_map<size_t, std::weak_ptr<Animator>> m_animators;
};

#endif // !DYNAMICCPP_EXPORTS
