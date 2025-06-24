#pragma once

class IAIComponent
{
public:
	virtual ~IAIComponent() = default;
	// Initialize the AI component
	virtual void Initialize() = 0;
	// Update the AI component
	virtual void Tick(float deltaTime) = 0;
};