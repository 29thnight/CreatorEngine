#pragma once
#include "Core.Minimal.h"
#include "random"
#include "LinkedListLib.hpp"
#include "EaseInOut.h"
#include "BaseEffectStruct.h"

#define THREAD_GROUP_SIZE 1024

// easing 위치 고민 현재는 그냥 모든 module에 넣을수 있는 구조

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

	// 더블 버퍼를 위해 설정
	void SetBuffers(ID3D11UnorderedAccessView* inputUAV, ID3D11ShaderResourceView* inputSRV, ID3D11UnorderedAccessView* outputUAV, ID3D11ShaderResourceView* outputSRV)
	{
		m_inputUAV = inputUAV;
		m_inputSRV = inputSRV;
		m_outputUAV = outputUAV;
		m_outputSRV = outputSRV;
	}

	// 이전 모듈에서 읽을 SRV 얻기
	ID3D11ShaderResourceView* GetInputSRV() const { return m_inputSRV; }
	ID3D11ShaderResourceView* GetOutputSRV() const { return m_outputSRV; }

	// 현재 모듈에서 쓸 UAV 얻기
	ID3D11UnorderedAccessView* GetInputUAV() const { return m_inputUAV; }
	ID3D11UnorderedAccessView* GetOutputUAV() const { return m_outputUAV; }

	virtual void OnSystemResized(UINT max) {}

	// *****파이프라인을 스테이지 별로 해서 하기*****
	ModuleStage GetStage() const { return m_stage; }
	void SetStage(ModuleStage stage) { m_stage = stage; }

	virtual bool NeedsBufferSwap() const { return true; }

protected:
	// 이징 변수
	bool m_useEasing;
	EasingEffect m_easingType;
	StepAnimation m_animationType;
	float m_easingDuration;

	// 멤버 변수
	ID3D11UnorderedAccessView* m_inputUAV;
	ID3D11ShaderResourceView* m_inputSRV;
	ID3D11UnorderedAccessView* m_outputUAV;
	ID3D11ShaderResourceView* m_outputSRV;

	// 파이프라인 변수
	ModuleStage m_stage = ModuleStage::SIMULATION;
};