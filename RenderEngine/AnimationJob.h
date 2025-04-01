#pragma once
#include "../Utility_Framework/Core.Minimal.h"
#include "../Utility_Framework/Core.Thread.hpp"

class RenderScene;
class Bone;
class Animator;
class Animation;
class NodeAnimation;

class AnimationJob
{
public:
    AnimationJob();
    ~AnimationJob();
    void Update(RenderScene& scene, float deltaTime);
private:
    void UpdateBones(Animator& animator);
    void UpdateBone(Bone* bone, Animator& animator, const DirectX::XMMATRIX& transform, float time);

    ThreadPool m_UpdateThreadPool;
    std::vector<Animator*> m_currAnimator;
    uint32 m_objectSize{};
};

