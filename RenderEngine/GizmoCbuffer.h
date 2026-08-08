#pragma once
// IRenderPass.h가 전이로 주던 것을 직접 든다(D4에서 그 헤더가 사라졌다).
#include "RenderScene.h"
#include "PSO.h"

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