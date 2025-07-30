#include "TrailGenerateModule.h"

void TrailGenerateModul::Initialize()
{
}

void TrailGenerateModul::Update(float deltaTime)
{
}

void TrailGenerateModul::Release()
{
}

void TrailGenerateModul::OnSystemResized(UINT maxParticles)
{
}

void TrailGenerateModul::OnParticleSystemPositionChanged(const Mathf::Vector3& newPosition)
{
}

void TrailGenerateModul::ResetForReuse()
{
}

bool TrailGenerateModul::IsReadyForReuse() const
{
	return false;
}

nlohmann::json TrailGenerateModul::SerializeData() const
{
	return nlohmann::json();
}

void TrailGenerateModul::DeserializeData(const nlohmann::json& json)
{
}

std::string TrailGenerateModul::GetModuleType() const
{
	return std::string();
}
