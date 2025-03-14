#pragma once
#include "Core.Minimal.h"
#include "DeviceResources.h"

class Camera
{
public:
	static constexpr Mathf::xVector FORWARD = { 0.f, 0.f, 1.f };
	static constexpr Mathf::xVector RIGHT = { 1.f, 0.f, 0.f };
	static constexpr Mathf::xVector UP = { 0.f, 1.f, 0.f };

	Mathf::xVector m_eyePosition{ XMVectorSet(0, 0, -10, 1) };
	Mathf::xVector m_forward{ FORWARD };
	Mathf::xVector m_right{ RIGHT };
	Mathf::xVector m_up{ UP };
	Mathf::xVector m_lookAt{ m_eyePosition + m_forward };

	float m_pitch{ 0.f };
	float m_yaw{ 0.f };
	float m_nearPlane{ 0.1f };
	float m_farPlane{ 1000.f };
	float m_speed{ 10.f };

	Mathf::Vector4 m_rayDirection{ 0.f, 0.f, 0.f, 0.f };

	virtual Mathf::xMatrix CalculateProjection() = 0;
	Mathf::Vector4 ConvertScreenToWorld(Mathf::Vector2 screenPosition, float depth);
	Mathf::Vector4 RayCast(Mathf::Vector2 screenPosition);
	Mathf::xMatrix CalculateView() const;
	Mathf::xMatrix CalculateInverseView() const;
	Mathf::xMatrix CalculateInverseProjection();
	DirectX11::Sizef GetScreenSize() const;
	DirectX::BoundingFrustum GetFrustum();

	void HandleMovement(float deltaTime);
};

class PerspacetiveCamera : public Camera
{
public:
	float m_aspectRatio{ 1.777f };
	float m_fov{ 45.f };

	Mathf::xMatrix CalculateProjection() override;
};

class OrthographicCamera : public Camera
{
public:
	float m_viewWidth{ 1.f };
	float m_viewHeight{ 1.f };

	Mathf::xMatrix CalculateProjection() override;
};