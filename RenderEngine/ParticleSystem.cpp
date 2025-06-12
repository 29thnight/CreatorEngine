#include "ParticleSystem.h"

ParticleSystem::ParticleSystem(int maxParticles) : m_maxParticles(maxParticles), m_isRunning(false)
{
	m_particleData.resize(maxParticles);
	for (auto& particle : m_particleData)
	{
		particle.isActive = false;
	}

	m_instanceData.resize(maxParticles);

	CreateParticleBuffer(maxParticles);
	InitializeParticleIndices();
}

ParticleSystem::~ParticleSystem()
{
	ReleaseBuffers();

	for (auto* module : m_renderModules)
	{
		delete module;
	}
	m_renderModules.clear();
}

void ParticleSystem::Play()
{
	m_isRunning = true;
	m_activeParticleCount = 0;

	for (auto& particle : m_particleData)
	{
		particle.isActive = 0;
	}
	InitializeParticleIndices();
}

void ParticleSystem::Update(float delta)
{
	if (!m_isRunning || m_isPaused)
		return;

	// 현재 읽기/쓰기 버퍼 결정
	ID3D11UnorderedAccessView* inputUAV = m_usingBufferA ? m_particleUAV_A : m_particleUAV_B;
	ID3D11ShaderResourceView* inputSRV = m_usingBufferA ? m_particleSRV_A : m_particleSRV_B;
	ID3D11UnorderedAccessView* outputUAV = m_usingBufferA ? m_particleUAV_B : m_particleUAV_A;
	ID3D11ShaderResourceView* outputSRV = m_usingBufferA ? m_particleSRV_B : m_particleSRV_A;

	// 모든 모듈 실행
	for (auto it = m_moduleList.begin(); it != m_moduleList.end(); ++it)
	{
		ParticleModule& module = *it;

		// 버퍼 설정 및 실행
		module.SetBuffers(inputUAV, inputSRV, outputUAV, outputSRV);
		module.Update(delta, m_particleData);

		// 다음 모듈을 위해 입력↔출력 스왑
		std::swap(inputUAV, outputUAV);
		std::swap(inputSRV, outputSRV);
	}

	// 최종 버퍼 스왑 (모듈 개수가 홀수면 결과적으로 버퍼가 바뀜)
	if (m_moduleList.size() % 2 == 1) {
		m_usingBufferA = !m_usingBufferA;
	}

	DeviceState::g_pDeviceContext->Flush();

#ifdef _DEBUG
	static int frameCount = 0;
	if (++frameCount % 60 == 0) {
		std::cout << "Particle system running..." << std::endl;
	}
#endif
}

void ParticleSystem::Render(RenderScene& scene, Camera& camera)
{
	if (!m_isRunning)
		return;

	auto& deviceContext = DeviceState::g_pDeviceContext;

	Mathf::Matrix world = XMMatrixIdentity();
	Mathf::Matrix view = XMMatrixTranspose(camera.CalculateView());
	Mathf::Matrix projection = XMMatrixTranspose(camera.CalculateProjection());

	for (auto* renderModule : m_renderModules)
	{
		renderModule->GetPSO()->Apply();
		renderModule->Render(world, view, projection);
	}
}

void ParticleSystem::SetPosition(const Mathf::Vector3& position)
{
	m_position = position;
	// SpawnModule은 위치를 직접 저장하지 않으므로, 모든 파티클의 위치를 이동
	for (auto& particle : m_particleData) {
		if (particle.isActive) {
			// 기존 위치에서 새 위치로의 오프셋 적용
			particle.position += position - m_position;
		}
	}
}

// ParticleSystem::CreateParticleBuffer 수정버전
void ParticleSystem::CreateParticleBuffer(UINT numParticles)
{
	// 기본 D3D11_USAGE_DEFAULT로 충분 (GPU만 사용)
	D3D11_BUFFER_DESC bufferDesc = {};
	bufferDesc.ByteWidth = sizeof(ParticleData) * numParticles;
	bufferDesc.Usage = D3D11_USAGE_DEFAULT;
	bufferDesc.BindFlags = D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE;
	bufferDesc.CPUAccessFlags = 0;
	bufferDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
	bufferDesc.StructureByteStride = sizeof(ParticleData);

	std::vector<ParticleData> initialData(numParticles);

	D3D11_SUBRESOURCE_DATA initData = {};
	initData.pSysMem = initialData.data();
	initData.SysMemPitch = 0;
	initData.SysMemSlicePitch = 0;

	// 초기화된 데이터로 버퍼 생성
	HRESULT hr = DeviceState::g_pDevice->CreateBuffer(&bufferDesc, &initData, &m_particleBufferA);
	if (FAILED(hr)) return;

	hr = DeviceState::g_pDevice->CreateBuffer(&bufferDesc, &initData, &m_particleBufferB);
	if (FAILED(hr)) return;

	// UAV 생성
	D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
	uavDesc.Format = DXGI_FORMAT_UNKNOWN;
	uavDesc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
	uavDesc.Buffer.FirstElement = 0;
	uavDesc.Buffer.NumElements = numParticles;
	uavDesc.Buffer.Flags = 0;

	hr = DeviceState::g_pDevice->CreateUnorderedAccessView(m_particleBufferA, &uavDesc, &m_particleUAV_A);
	if (FAILED(hr)) return;

	hr = DeviceState::g_pDevice->CreateUnorderedAccessView(m_particleBufferB, &uavDesc, &m_particleUAV_B);
	if (FAILED(hr)) return;

	// SRV 생성
	D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.Format = DXGI_FORMAT_UNKNOWN;
	srvDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
	srvDesc.Buffer.FirstElement = 0;
	srvDesc.Buffer.NumElements = numParticles;

	hr = DeviceState::g_pDevice->CreateShaderResourceView(m_particleBufferA, &srvDesc, &m_particleSRV_A);
	if (FAILED(hr)) return;

	hr = DeviceState::g_pDevice->CreateShaderResourceView(m_particleBufferB, &srvDesc, &m_particleSRV_B);
	if (FAILED(hr)) return;

	m_usingBufferA = true;
}

void ParticleSystem::InitializeParticleIndices()
{
	// CPU 측 파티클 데이터 비활성화
	for (auto& particle : m_particleData) {
		particle.isActive = 0;
	}
	m_activeParticleCount = 0;

	// 현재 사용 중인 버퍼 선택
	ID3D11UnorderedAccessView* particleUAV = m_usingBufferA ? m_particleUAV_A : m_particleUAV_B;

	// 초기화 완료 후 GPU 동기화
	DeviceState::g_pDeviceContext->Flush();
}

void ParticleSystem::ResizeParticleSystem(UINT newMaxParticles)
{
	if (newMaxParticles == m_maxParticles)
		return;

	// 1. 파티클 시스템 일시 정지 및 GPU 작업 완료 대기
	bool wasRunning = m_isRunning;
	m_isRunning = false;
	DeviceState::g_pDeviceContext->Flush();

	// 2. 기존 GPU 리소스 정리
	ReleaseParticleBuffers();

	// 3. 새로운 크기로 CPU 데이터 구조 업데이트
	m_maxParticles = newMaxParticles;
	m_particleData.resize(newMaxParticles);
	m_instanceData.resize(newMaxParticles);

	// CPU 데이터는 단순히 크기만 맞춰주고 초기화는 GPU에서 담당
	for (auto& particle : m_particleData) {
		particle.isActive = 0;
	}

	// 4. 새 GPU 버퍼 생성
	CreateParticleBuffer(m_maxParticles);

	// 5. GPU에서 모든 초기화 수행
	InitializeParticleIndices();

	// 6. 모듈들에게 크기 변경 알림
	for (auto it = m_moduleList.begin(); it != m_moduleList.end(); ++it) {
		ParticleModule& module = *it;
		module.OnSystemResized(m_maxParticles);
	}

	// 7. 상태 복원
	m_activeParticleCount = 0;
	m_usingBufferA = true;
	m_isRunning = wasRunning;
}

void ParticleSystem::ReleaseBuffers()
{
	ReleaseParticleBuffers();
}

void ParticleSystem::ReleaseParticleBuffers()
{
	// 파티클 버퍼만 해제
	if (m_particleBufferA) { m_particleBufferA->Release(); m_particleBufferA = nullptr; }
	if (m_particleBufferB) { m_particleBufferB->Release(); m_particleBufferB = nullptr; }
	if (m_particleUAV_A) { m_particleUAV_A->Release(); m_particleUAV_A = nullptr; }
	if (m_particleUAV_B) { m_particleUAV_B->Release(); m_particleUAV_B = nullptr; }
	if (m_particleSRV_A) { m_particleSRV_A->Release(); m_particleSRV_A = nullptr; }
	if (m_particleSRV_B) { m_particleSRV_B->Release(); m_particleSRV_B = nullptr; }
}

ID3D11ShaderResourceView* ParticleSystem::GetCurrentRenderingSRV() const
{
	bool finalIsBufferA = m_usingBufferA;
	if (m_moduleList.size() % 2 == 1) {
		// 모듈 개수가 홀수이면 반전
		finalIsBufferA = !finalIsBufferA;
	}

	return finalIsBufferA ? m_particleSRV_A : m_particleSRV_B;
}

void ParticleSystem::ConfigureModuleBuffers(ParticleModule& module, bool isFirstModule)
{
	// Update 함수에서 직접 처리하므로 이 함수는 필요 없을 수 있음 
	// 또는 특별한 경우에만 사용
	if (m_usingBufferA) {
		module.SetBuffers(
			m_particleUAV_A, m_particleSRV_A,  // 입력
			m_particleUAV_B, m_particleSRV_B   // 출력
		);
	}
	else {
		module.SetBuffers(
			m_particleUAV_B, m_particleSRV_B,  // 입력
			m_particleUAV_A, m_particleSRV_A   // 출력
		);
	}
}