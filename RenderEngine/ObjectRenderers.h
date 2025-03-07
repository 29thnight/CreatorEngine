#pragma once
#include <vector>
#include <DirectXMath.h>

class Mesh;
class Material;
class Texture;
class Skeleton;
class Animator;

struct Renderable
{
    bool m_IsEnabled = false;
};

struct MeshRenderer : public Renderable
{
    Material* m_Material;
    Mesh* m_Mesh;
    Animator* m_Animator = nullptr;
};

struct SpriteRenderer : public Renderable
{
    Texture* m_Sprite = nullptr;
};

struct Animator : public Renderable
{
    Skeleton* m_Skeleton = nullptr;
    float m_TimeElapsed = 0;
    uint32_t m_AnimIndexChosen = 0;
    DirectX::XMMATRIX m_FinalTransforms[50];
};
