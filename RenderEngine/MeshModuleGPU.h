#pragma once
#include "Model.h"
#include "Texture.h"
#include "RenderModules.h"
#include "ISerializable.h"

enum class MeshType
{
	None,
	Cube,
	Sphere,
	Model
};

struct MeshConstantBuffer
{
	Mathf::Matrix world;
	Mathf::Matrix view;
	Mathf::Matrix projection;
	Mathf::Vector3 cameraPosition;
	float padding;
};

class MeshModuleGPU : public RenderModules, public ISerializable
{
public:
	MeshModuleGPU();
	void Initialize() override;
	void Release() override;
	void Render(Mathf::Matrix world, Mathf::Matrix view, Mathf::Matrix projection) override;

	void SetParticleData(ID3D11ShaderResourceView* particleSRV, UINT instanceCount) override;
	void SetupRenderTarget(RenderPassData* renderData) override;

	virtual void ResetForReuse() override;
	virtual bool IsReadyForReuse() const override;
	virtual void WaitForGPUCompletion() override;

	void UpdatePSOShaders() override;

	// 메시 타입 설정
	void SetMeshType(MeshType type);

	// 모델 설정 (인덱스 기반)
	void SetModel(Model* model, int meshIndex = 0);

	// 모델 설정 (이름 기반)
	void SetModel(Model* model, std::string_view meshName);
	// 카메라 위치 설정
	void SetCameraPosition(const Mathf::Vector3& position);

	// 상태 조회 함수들 (UI에서 사용)
	MeshType GetMeshType() const { return m_meshType; }
	Model* GetCurrentModel() const { return m_model; }
	int GetCurrentMeshIndex() const { return m_meshIndex; }
	UINT GetInstanceCount() const { return m_instanceCount; }
	Mesh* GetCurrentMesh() const;

	Model* GetModel() const { return m_model; }
	Mathf::Vector3 GetCameraPosition() const { return m_constantBufferData.cameraPosition; }

	// 클리핑 지원 선언
	bool SupportsClipping() const override { return true; }

	void OnClippingStateChanged() override;

	void SetClippingAnimation(bool enable, float speed = 1.0f);
	bool IsClippingAnimating() const { return m_isClippingAnimating; }
	float GetClippingAnimationSpeed() const { return m_clippingAnimationSpeed; }

	virtual void CreateClippingBuffer();
	virtual void UpdateClippingBuffer();
	void EnablePolarClipping(bool enable);
	bool IsPolarClippingEnabled() const;
	void SetPolarAngleProgress(float progress);
	void SetPolarCenter(const Mathf::Vector3& center);
	void SetPolarUpAxis(const Mathf::Vector3& upAxis);
	void SetPolarStartAngle(float angleRadians);
	void SetPolarDirection(float direction); // 1.0f: 시계방향, -1.0f: 반시계방향
	void SetPolarClippingAnimation(bool enable, float speed = 1.0f);

	const PolarClippingParams& GetPolarClippingParams() const { return m_polarClippingParams; }
	bool IsPolarClippingAnimating() const { return m_isPolarClippingAnimating; }
	float GetPolarClippingAnimationSpeed() const { return m_polarClippingAnimationSpeed; }
	void SetPolarReferenceDirection(const Mathf::Vector3& referenceDir);

public:
	virtual nlohmann::json SerializeData() const override;
	virtual void DeserializeData(const nlohmann::json& json) override;
	virtual std::string GetModuleType() const override;
	std::pair<Mathf::Vector3, Mathf::Vector3> GetCurrentMeshBounds() const;
private:
	// 내부 함수들
	void CreateCubeMesh();
	void CreateSphereMesh();
	void UpdateConstantBuffer(const Mathf::Matrix& world, const Mathf::Matrix& view,
		const Mathf::Matrix& projection);



	bool m_isClippingAnimating = false;
	float m_clippingAnimationSpeed = 1.0f;

private:
	ComPtr<ID3D11Buffer> m_constantBuffer{};
	MeshConstantBuffer m_constantBufferData{};

	// 클리핑 버퍼
	ComPtr<ID3D11Buffer> m_clippingBuffer{};

	// 메시 관련
	MeshType m_meshType{};
	Model* m_model{};                    // 현재 사용 중인 모델
	int m_meshIndex{};                   // 모델 내 메시 인덱스
	Mesh* m_tempCubeMesh{};              // 임시 큐브 메시 (프리미티브용)

	// 파티클 관련
	ID3D11ShaderResourceView* m_particleSRV{};
	UINT m_instanceCount{};

	// 상대 기반
	Mathf::Matrix m_worldMatrix{};
	Mathf::Matrix m_invWorldMatrix{};
	bool m_useRelativeClipping{};

	ComPtr<ID3D11Buffer> m_polarClippingBuffer{};
	PolarClippingParams m_polarClippingParams{};
	bool m_isPolarClippingAnimating = false;
	float m_polarClippingAnimationSpeed = 1.0f;

	std::mutex m_resetMutex;

	Microsoft::WRL::ComPtr<ID3D11Buffer> m_timeBuffer;
	TimeParams m_timeParams = {};
};