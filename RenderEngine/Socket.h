#pragma once
#include "Core.Minimal.h"
class Socket
{
public:
	Socket() = default;

    [[Property]]
    std::string m_name;
    [[Property]]
    std::string m_boneName; 
    Mathf::xMatrix m_offset{};
    Mathf::xMatrix worldTransform{};
};

