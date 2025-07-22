#include "ParticleSystem.h"

ParticleSystem::ParticleSystem(int maxParticles, ParticleDataType dataType) : m_maxParticles(maxParticles), m_isRunning(false)
{
	SetParticleDatatype(dataType);

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
	// 1. 위치/Transform 업데이트는 항상 수행 (렌더링/실행 상태와 무관)
	UpdateTransformOnly();

	// 2. 파티클 시스템이 실행 중이 아니면 시뮬레이션 중단
	if (!m_isRunning) {
		return;
	}

	// 3. 파티클 시뮬레이션 수행
	UpdateParticleSimulation(delta);


}

void ParticleSystem::Render(RenderScene& scene, Camera& camera)
{
	if (!m_isRunning)
		return;

	if (!RenderPassData::VaildCheck(&camera)) return;
	auto renderData = RenderPassData::GetData(&camera);

	Mathf::Matrix world = XMMatrixIdentity();
	Mathf::Matrix view = XMMatrixTranspose(camera.CalculateView());
	Mathf::Matrix projection = XMMatrixTranspose(camera.CalculateProjection());

	// 최종 처리된 파티클 SRV와 인스턴스 수 가져오기
	ID3D11ShaderResourceView* finalParticleSRV = GetCurrentRenderingSRV();
	UINT instanceCount = m_maxParticles;

	for (auto* renderModule : m_renderModules)
	{
		if (!renderModule) {
			OutputDebugStringA("Error: renderModule is nullptr\n");
			continue;
		}

		// 가상 함수 테이블 확인
		void* vtable = *reinterpret_cast<void**>(renderModule);
		if (!vtable) {
			OutputDebugStringA("Error: vtable is nullptr\n");
			continue;
		}


		renderModule->SaveRenderState();
		renderModule->SetupRenderTarget(renderData);

		// 모든 렌더링 모듈에 파티클 데이터 설정
		renderModule->SetParticleData(finalParticleSRV, instanceCount);

		renderModule->GetPSO()->Apply();
		renderModule->Render(world, view, projection);
		renderModule->CleanupRenderState();
		renderModule->RestoreRenderState();
	}
}

void ParticleSystem::UpdateEffectBasePosition(const Mathf::Vector3& newBasePosition)
{
	m_effectBasePosition = newBasePosition;

	// 실제 emitter 월드 위치 = Effect 기준점 + 이 ParticleSystem의 상대위치
	Mathf::Vector3 finalWorldPosition = m_effectBasePosition + m_position;

	// SpawnModule들에 최종 계산된 위치 전달
	for (auto it = m_moduleList.begin(); it != m_moduleList.end(); ++it) {
		ParticleModule& module = *it;

		SpawnModuleCS* spawnModule = dynamic_cast<SpawnModuleCS*>(&module);
		if (spawnModule) {
			spawnModule->SetEmitterPosition(finalWorldPosition);
			continue;
		}

		MeshSpawnModuleCS* meshSpawnModule = dynamic_cast<MeshSpawnModuleCS*>(&module);
		if (meshSpawnModule) {
			meshSpawnModule->SetEmitterPosition(finalWorldPosition);
			continue;
		}
	}

	for (auto* renderModule : m_renderModules) {
		if (auto* meshModule = dynamic_cast<MeshModuleGPU*>(renderModule)) {
			meshModule->SetPolarCenter(finalWorldPosition);
		}
	}
}

void ParticleSystem::SetPosition(const Mathf::Vector3& position)
{
	m_position = position;  // 상대좌표 저장

	// Effect 기준점이 설정되어 있다면 즉시 업데이트
	UpdateEffectBasePosition(m_effectBasePosition);
}

void ParticleSystem::SetRotation(const Mathf::Vector3& rotation)
{
	m_rotation = rotation;

	// SpawnModule들에 회전값 전달
	for (auto it = m_moduleList.begin(); it != m_moduleList.end(); ++it)
	{
		ParticleModule& module = *it;

		// SpawnModuleCS인지 확인
		if (SpawnModuleCS* spawnModule = dynamic_cast<SpawnModuleCS*>(&module))
		{
			spawnModule->SetEmitterRotation(rotation);
		}
		// MeshSpawnModuleCS인지 확인
		else if (MeshSpawnModuleCS* meshSpawnModule = dynamic_cast<MeshSpawnModuleCS*>(&module))
		{
			meshSpawnModule->SetEmitterRotation(rotation);
		}
	}
}

void ParticleSystem::CreateParticleBuffer(UINT numParticles)
{
	D3D11_BUFFER_DESC bufferDesc = {};
	bufferDesc.ByteWidth = m_particleStructSize * numParticles;
	bufferDesc.Usage = D3D11_USAGE_DEFAULT;
	bufferDesc.BindFlags = D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE;
	bufferDesc.CPUAccessFlags = 0;
	bufferDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
	bufferDesc.StructureByteStride = m_particleStructSize;

	D3D11_SUBRESOURCE_DATA initData = {};

	if (m_particleDataType == ParticleDataType::Mesh)
	{
		// MeshParticleData로 초기화
		std::vector<MeshParticleData> meshInitialData(numParticles);
		for (UINT i = 0; i < numParticles; ++i)
		{
			meshInitialData[i].isActive = 0;
			meshInitialData[i].renderMode = 0;
			meshInitialData[i].age = 0.0f;
			meshInitialData[i].lifeTime = 0.0f;
			meshInitialData[i].position = XMFLOAT3(0.0f, 0.0f, 0.0f);
			meshInitialData[i].velocity = XMFLOAT3(0.0f, 0.0f, 0.0f);
			meshInitialData[i].acceleration = XMFLOAT3(0.0f, 0.0f, 0.0f);
			meshInitialData[i].rotation = XMFLOAT3(0.0f, 0.0f, 0.0f);
			meshInitialData[i].rotationSpeed = XMFLOAT3(0.0f, 0.0f, 0.0f);
			meshInitialData[i].scale = XMFLOAT3(1.0f, 1.0f, 1.0f);
			meshInitialData[i].color = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
			meshInitialData[i].textureIndex = 0;
		}

		initData.pSysMem = meshInitialData.data();
		initData.SysMemPitch = 0;
		initData.SysMemSlicePitch = 0;

		// 버퍼 생성
		HRESULT hr = DeviceState::g_pDevice->CreateBuffer(&bufferDesc, &initData, &m_particleBufferA);
		if (FAILED(hr)) {
			OutputDebugStringA("Failed to create mesh particle buffer A\n");
			return;
		}

		hr = DeviceState::g_pDevice->CreateBuffer(&bufferDesc, &initData, &m_particleBufferB);
		if (FAILED(hr)) {
			OutputDebugStringA("Failed to create mesh particle buffer B\n");
			return;
		}
	}
	else
	{
		// ParticleData로 초기화
		std::vector<ParticleData> standardInitialData(numParticles);
		for (UINT i = 0; i < numParticles; ++i)
		{
			standardInitialData[i].isActive = 0;
			standardInitialData[i].age = 0.0f;
			standardInitialData[i].lifeTime = 0.0f;
			standardInitialData[i].position = Mathf::Vector3(0.0f, 0.0f, 0.0f);
			standardInitialData[i].velocity = Mathf::Vector3(0.0f, 0.0f, 0.0f);
			standardInitialData[i].acceleration = Mathf::Vector3(0.0f, 0.0f, 0.0f);
			standardInitialData[i].rotation = 0.0f;
			standardInitialData[i].rotatespeed = 0.0f;
			standardInitialData[i].size = Mathf::Vector2(1.0f, 1.0f);
			standardInitialData[i].color = Mathf::Vector4(1.0f, 1.0f, 1.0f, 1.0f);
		}

		initData.pSysMem = standardInitialData.data();
		initData.SysMemPitch = 0;
		initData.SysMemSlicePitch = 0;

		// 버퍼 생성
		HRESULT hr = DeviceState::g_pDevice->CreateBuffer(&bufferDesc, &initData, &m_particleBufferA);
		if (FAILED(hr)) {
			OutputDebugStringA("Failed to create standard particle buffer A\n");
			return;
		}

		hr = DeviceState::g_pDevice->CreateBuffer(&bufferDesc, &initData, &m_particleBufferB);
		if (FAILED(hr)) {
			OutputDebugStringA("Failed to create standard particle buffer B\n");
			return;
		}
	}

	// UAV 생성
	D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
	uavDesc.Format = DXGI_FORMAT_UNKNOWN;
	uavDesc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
	uavDesc.Buffer.FirstElement = 0;
	uavDesc.Buffer.NumElements = numParticles;
	uavDesc.Buffer.Flags = 0;

	HRESULT hr = DeviceState::g_pDevice->CreateUnorderedAccessView(m_particleBufferA, &uavDesc, &m_particleUAV_A);
	if (FAILED(hr)) {
		OutputDebugStringA("Failed to create particle UAV A\n");
		return;
	}

	hr = DeviceState::g_pDevice->CreateUnorderedAccessView(m_particleBufferB, &uavDesc, &m_particleUAV_B);
	if (FAILED(hr)) {
		OutputDebugStringA("Failed to create particle UAV B\n");
		return;
	}

	// SRV 생성
	D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.Format = DXGI_FORMAT_UNKNOWN;
	srvDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
	srvDesc.Buffer.FirstElement = 0;
	srvDesc.Buffer.NumElements = numParticles;

	hr = DeviceState::g_pDevice->CreateShaderResourceView(m_particleBufferA, &srvDesc, &m_particleSRV_A);
	if (FAILED(hr)) {
		OutputDebugStringA("Failed to create particle SRV A\n");
		return;
	}

	hr = DeviceState::g_pDevice->CreateShaderResourceView(m_particleBufferB, &srvDesc, &m_particleSRV_B);
	if (FAILED(hr)) {
		OutputDebugStringA("Failed to create particle SRV B\n");
		return;
	}

	m_usingBufferA = true;

	// 디버그 출력
	char debugMsg[256];
	sprintf_s(debugMsg, "ParticleSystem: Created buffers with struct size: %zu bytes (Type: %s)\n",
		m_particleStructSize,
		m_particleDataType == ParticleDataType::Mesh ? "Mesh" : "Standard");
	OutputDebugStringA(debugMsg);
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

void ParticleSystem::SetParticleDatatype(ParticleDataType type)
{
	if (m_particleDataType == type)
		return;

	m_particleDataType = type;

	switch (type)
	{
	case ParticleDataType::Standard:
		m_particleStructSize = sizeof(ParticleData);
		break;
	case ParticleDataType::Mesh:
		m_particleStructSize = sizeof(MeshParticleData);
		break;
	}

	if (m_particleBufferA || m_particleBufferB)
	{
		// 기존 버퍼 해제
		ReleaseParticleBuffers();
		// 새로운 크기로 버퍼 재생성
		CreateParticleBuffer(m_maxParticles);

		// 모든 모듈에게 크기 변경 알림
		for (auto it = m_moduleList.begin(); it != m_moduleList.end(); ++it) {
			ParticleModule& module = *it;
			module.OnSystemResized(m_maxParticles);
		}
	}

}

size_t ParticleSystem::GetParticleStructSize() const
{
	return m_particleStructSize;
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

void ParticleSystem::SetEffectProgress(float progress)
{
	m_effectProgress = progress;

	// 모든 렌더 모듈에 진행률 전달
	for (auto* renderModule : m_renderModules) {
		if (auto* meshModule = dynamic_cast<MeshModuleGPU*>(renderModule)) {
			meshModule->SetEffectProgress(progress);
		}
	}
}

void ParticleSystem::UpdateTransformOnly()
{
	// 최종 월드 위치 계산
	Mathf::Vector3 finalWorldPosition = m_effectBasePosition + m_position;

	// 1. 렌더링 모듈 위치 업데이트
	for (auto* renderModule : m_renderModules) {
		if (auto* meshModule = dynamic_cast<MeshModuleGPU*>(renderModule)) {
			meshModule->SetPolarCenter(finalWorldPosition);
		}
	}

	// 2. SpawnModule들의 emitter 위치 업데이트
	for (auto it = m_moduleList.begin(); it != m_moduleList.end(); ++it) {
		ParticleModule& module = *it;

		if (SpawnModuleCS* spawnModule = dynamic_cast<SpawnModuleCS*>(&module)) {
			spawnModule->SetEmitterPosition(finalWorldPosition);
		}
		else if (MeshSpawnModuleCS* meshSpawnModule = dynamic_cast<MeshSpawnModuleCS*>(&module)) {
			meshSpawnModule->SetEmitterPosition(finalWorldPosition);
		}
	}
}

void ParticleSystem::UpdateParticleSimulation(float delta)
{
	// 현재 읽기/쓰기 버퍼 결정
	ID3D11UnorderedAccessView* inputUAV = m_usingBufferA ? m_particleUAV_A : m_particleUAV_B;
	ID3D11ShaderResourceView* inputSRV = m_usingBufferA ? m_particleSRV_A : m_particleSRV_B;
	ID3D11UnorderedAccessView* outputUAV = m_usingBufferA ? m_particleUAV_B : m_particleUAV_A;
	ID3D11ShaderResourceView* outputSRV = m_usingBufferA ? m_particleSRV_B : m_particleSRV_A;

	// 모든 모듈 실행 (스폰 상태에 따라)
	for (auto it = m_moduleList.begin(); it != m_moduleList.end(); ++it)
	{
		ParticleModule& module = *it;

		// 버퍼 설정
		module.SetBuffers(inputUAV, inputSRV, outputUAV, outputSRV);

		// 스폰 모듈인 경우 스폰 가능 여부 체크
		if (auto* spawnModule = dynamic_cast<SpawnModuleCS*>(&module)) {
			if (m_shouldSpawn) {
				spawnModule->Update(delta);
			}
			else {
				// 새로운 파티클 생성 없이 기존 파티클만 업데이트하는 로직
				// spawnRate를 임시로 0으로 설정
				float originalSpawnRate = spawnModule->GetSpawnRate();
				spawnModule->SetSpawnRate(0.0f);
				spawnModule->Update(delta);
				spawnModule->SetSpawnRate(originalSpawnRate);
			}
		}
		else if (auto* meshSpawnModule = dynamic_cast<MeshSpawnModuleCS*>(&module)) {
			if (m_shouldSpawn) {
				meshSpawnModule->Update(delta);
			}
			else {
				// 새로운 파티클 생성 없이 기존 파티클만 업데이트하는 로직
				float originalSpawnRate = meshSpawnModule->GetSpawnRate();
				meshSpawnModule->SetSpawnRate(0.0f);
				meshSpawnModule->Update(delta);
				meshSpawnModule->SetSpawnRate(originalSpawnRate);
			}
		}
		else {
			// 다른 모듈들은 항상 업데이트
			module.Update(delta);
		}

		// 다음 모듈을 위해 입력↔출력 스왑
		std::swap(inputUAV, outputUAV);
		std::swap(inputSRV, outputSRV);
	}

	// 최종 버퍼 스왑 (모듈 개수가 홀수면 결과적으로 버퍼가 바뀜)
	if (m_moduleList.size() % 2 == 1) {
		m_usingBufferA = !m_usingBufferA;
	}

	DeviceState::g_pDeviceContext->Flush();
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