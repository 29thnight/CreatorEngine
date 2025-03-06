#pragma once
#include <vector>
#include <atomic>

constexpr uint32_t MAX_JOINTS = 512;

class Mesh;
class Material;
class Texture;
class Skeleton;
class Animator;

struct MeshType
{
	Material*		 m_pMaterial{};
	Mesh*			 m_pMesh{};
	Animator*		 m_pAnimator{};
	std::atomic_bool m_isEnabled{ false };
};

struct SpriteType
{
	Material*		 m_pMaterial{};
	Texture*		 m_pTexture{};
	std::atomic_bool m_isEnabled{ false };
};

struct AnimatorType
{
	Skeleton*		 m_pSkeleton{};
	float			 m_elapsedTime{};
	uint32_t		 m_currentAnimation{};
	std::atomic_bool m_isEnabled{ false };
};