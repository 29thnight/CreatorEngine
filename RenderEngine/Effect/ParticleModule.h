#pragma once
#include "Core.Minimal.h"
#include "random"
#include "LinkedListLib.hpp"
#include "EaseInOut.h"
#include "BaseEffectStruct.h"

#define THREAD_GROUP_SIZE 1024

// easing ��ġ ��� ����� �׳� ��� module�� ������ �ִ� ����

class ParticleModule : public LinkProperty<ParticleModule>
{
public:
	ParticleModule() : LinkProperty<ParticleModule>(this) {}
	virtual ~ParticleModule() = default;
	virtual void Initialize() {}
	virtual void Update(float delta, std::vector<ParticleData>& particles) {}

	void SetEasingType(EasingEffect type)
	{
		m_easingType = type;
		m_useEasing = true;
	}

	void SetAnimationType(StepAnimation type)
	{
		m_animationType = type;
		m_useEasing = true;
	}

	void SetEasingDuration(float duration)
	{
		m_easingDuration = duration;
	}

	void EnableEasing(bool enable)
	{
		m_useEasing = enable;
	}

	bool IsEasingEnabled() const
	{
		return m_useEasing;
	}

	float ApplyEasing(float normalizedTime);

	EaseInOut CreateEasingObject()
	{
		return EaseInOut(m_easingType, m_animationType, m_easingDuration);
	}

	// ���� ���۸� ���� ����
	void SetBuffers(ID3D11UnorderedAccessView* inputUAV, ID3D11ShaderResourceView* inputSRV, ID3D11UnorderedAccessView* outputUAV, ID3D11ShaderResourceView* outputSRV)
	{
		m_inputUAV = inputUAV;
		m_inputSRV = inputSRV;
		m_outputUAV = outputUAV;
		m_outputSRV = outputSRV;
	}

	// ���� ��⿡�� ���� SRV ���
	ID3D11ShaderResourceView* GetInputSRV() const { return m_inputSRV; }
	ID3D11ShaderResourceView* GetOutputSRV() const { return m_outputSRV; }

	// ���� ��⿡�� �� UAV ���
	ID3D11UnorderedAccessView* GetInputUAV() const { return m_inputUAV; }
	ID3D11UnorderedAccessView* GetOutputUAV() const { return m_outputUAV; }

	virtual void OnSystemResized(UINT max) {}

	// *****������������ �������� ���� �ؼ� �ϱ�*****
	ModuleStage GetStage() const { return m_stage; }
	void SetStage(ModuleStage stage) { m_stage = stage; }

	virtual bool NeedsBufferSwap() const { return true; }

protected:
	// ��¡ ����
	bool m_useEasing;
	EasingEffect m_easingType;
	StepAnimation m_animationType;
	float m_easingDuration;

	// ��� ����
	ID3D11UnorderedAccessView* m_inputUAV;
	ID3D11ShaderResourceView* m_inputSRV;
	ID3D11UnorderedAccessView* m_outputUAV;
	ID3D11ShaderResourceView* m_outputSRV;

	// ���������� ����
	ModuleStage m_stage = ModuleStage::SIMULATION;
};