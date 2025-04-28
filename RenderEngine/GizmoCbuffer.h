#pragma once
#include "IRenderPass.h"

cbuffer GizmoCameraBuffer
{
	Mathf::xMatrix VP{};
	float3 eyePosition{};
};

cbuffer GizmoPos
{
	float3 pos{};
};

cbuffer GizmoSize
{
	float size{};
};

enum class GizmoType
{
	Light,
	Camera,
};

struct LineVertex
{
	DirectX::XMFLOAT3 Position;
	DirectX::XMFLOAT4 Color;
};