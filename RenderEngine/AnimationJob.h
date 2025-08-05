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
	void SetRenderScene(RenderScene* renderScene) { m_renderScene = renderScene; }
	void Finalize();
private:
	void PrepareAnimation();
    void CleanUp();
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
	RenderScene* m_renderScene{ nullptr };
};

#endif // !DYNAMICCPP_EXPORTS