#pragma once
#include "Core.Minimal.h"
#include "Mesh.h"
#include "Texture.h"
#include "Skeleton.h"

class SceneObject;
class ModelLoader;
class Model
{
public:
	std::string			name{};
	file::path			path{};
	std::vector<Mesh>	meshes{};
	Skeleton*			m_skeleton{};
};
