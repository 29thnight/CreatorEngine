#pragma once
#include "Core.Minimal.h"
#include "random"
#include "LinkedListLib.hpp"
#include "EaseInOut.h"
#include "BaseEffectStruct.h"

#define THREAD_GROUP_SIZE 1024

// easing 위치 고민 현재는 그냥 모든 module에 넣을수 있는 구조

class ParticleSystem;

class ParticleModule : public LinkProperty<ParticleModule>
{
public:
	ParticleModule() : LinkProperty<ParticleModule>(this), m_ownerSystem(nullptr) {}
	virtual ~ParticleModule() = default;
	virtual void Initialize() {}
	virtual void Update(float delta) {}
	virtual void Release() {}

	virtual void ResetForReuse() {}
	virtual bool IsReadyForReuse() const { return true; }
	virtual void WaitForGPUCompletion() {}

	void SetEnabled(bool enabled) { m_enabled = enabled; }
	bool IsEnabled() const { return m_enabled; }

	// Owner 시스템 설정 및 접근
	void SetOwnerSystem(ParticleSystem* owner) { m_ownerSystem = owner; }
	ParticleSystem* GetOwnerSystem() const { return m_ownerSystem; }

	// Owner 시스템이 있는지 확인
	bool HasOwnerSystem() const { return m_ownerSystem != nullptr; }

	// Owner 시스템을 통한 편의 함수들
	Mathf::Vector3 GetSystemWorldPosition() const;
	Mathf::Vector3 GetSystemRelativePosition() const;
	Mathf::Vector3 GetSystemEffectBasePosition() const;
	bool IsSystemRunning() const;

	void SetEasingType(int type)
	{
		m_easingType = static_cast<EasingEffect>(type);
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
	virtual void SetBuffers(ID3D11UnorderedAccessView* inputUAV, ID3D11ShaderResourceView* inputSRV, ID3D11UnorderedAccessView* outputUAV, ID3D11ShaderResourceView* outputSRV)
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
	virtual void OnParticleSystemPositionChanged(const Mathf::Vector3& newPosition) {}

	// *****파이프라인을 스테이지 별로 해서 하기*****
	ModuleStage GetStage() const { return m_stage; }
	void SetStage(ModuleStage stage) { m_stage = stage; }


	// 테스트용 
	virtual bool IsGenerateModule() const { return false; }

protected:
	ParticleSystem* m_ownerSystem;

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

	// 상태 관리
	bool m_enabled = true;
	bool m_isInitialized = false;
};