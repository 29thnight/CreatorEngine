#pragma once
#include "../Utility_Framework/Core.Minimal.h"

class RenderScene;
class Bone;
class Animator;
class Animation;
class NodeAnimation;

class AnimationJob
{
public:
    void Update(RenderScene& scene, float deltaTime);
private:
    void UpdateBones(Animator& animator);
    void UpdateBone(Bone* bone, Animator& animator, const DirectX::XMMATRIX& transform, float time);
};

