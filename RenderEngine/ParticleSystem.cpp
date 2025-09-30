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
	m_isPaused = false;
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

	// 모듈 연결
	if (!m_modulesConnected) {
		AutoConnectModules();
	}

	SyncEmitterTransform();

	UpdateGenerateModule(delta);

	// 기존 시뮬레이션 모듈들 실행
	ExecuteSimulationModules(delta);



//#ifdef _DEBUG
//	static int frameCount = 0;
//	if (++frameCount % 60 == 0) {
//		std::cout << "Executed modules: " << executedModuleCount
//			<< ", Using Buffer A: " << (m_usingBufferA ? "true" : "false") << std::endl;
//	}
//#endif
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

		if (renderModule->GetPSO() == nullptr)
		{
			std::cout << " " << std::endl;
		}

		renderModule->GetPSO()->Apply();
		renderModule->Render(world, view, projection);
		renderModule->CleanupRenderState();
		renderModule->RestoreRenderState();
	}
}

void ParticleSystem::StopSpawning()
{
	for (auto it = m_moduleList.begin(); it != m_moduleList.end(); ++it) {
		ParticleModule& module = *it;

		// SpawnModuleCS만 비활성화
		if (SpawnModuleCS* spawnModule = dynamic_cast<SpawnModuleCS*>(&module)) {
			spawnModule->SetAllowNewSpawn(false);
		}
		// MeshSpawnModuleCS도 비활성화
		else if (MeshSpawnModuleCS* meshSpawnModule = dynamic_cast<MeshSpawnModuleCS*>(&module)) {
			meshSpawnModule->SetAllowNewSpawn(false);
		}
		// TrailGenerateModule도 비활성화 (필요시)
		//else if (TrailGenerateModule* trailModule = dynamic_cast<TrailGenerateModule*>(&module)) {
		//	trailModule->SetEnabled(false);
		//}
	}
}

void ParticleSystem::ResumeSpawning()
{
	for (auto it = m_moduleList.begin(); it != m_moduleList.end(); ++it) {
		ParticleModule& module = *it;

		if (SpawnModuleCS* spawnModule = dynamic_cast<SpawnModuleCS*>(&module)) {
			spawnModule->SetAllowNewSpawn(true);
		}
		else if (MeshSpawnModuleCS* meshSpawnModule = dynamic_cast<MeshSpawnModuleCS*>(&module)) {
			meshSpawnModule->SetAllowNewSpawn(true);
		}
		//else if (TrailGenerateModule* trailModule = dynamic_cast<TrailGenerateModule*>(&module)) {
		//	trailModule->SetEnabled(true);
		//}
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

		MovementModuleCS* movementModule = dynamic_cast<MovementModuleCS*>(&module);
		if (movementModule) {
			movementModule->SetOrbitalCenter(finalWorldPosition);
		}

		MeshMovementModuleCS* meshMovementModule = dynamic_cast<MeshMovementModuleCS*>(&module);
		if (meshMovementModule) {
			meshMovementModule->SetOrbitalCenter(finalWorldPosition);
		}

		TrailGenerateModule* trailModule = dynamic_cast<TrailGenerateModule*>(&module);
		if (trailModule) {
			trailModule->SetEmitterPosition(finalWorldPosition);
			continue;
		}
	}

	for (auto* renderModule : m_renderModules) {
		if (auto* meshModule = dynamic_cast<MeshModuleGPU*>(renderModule)) {
			meshModule->SetPolarCenter(finalWorldPosition);
		}
	}
}

void ParticleSystem::UpdateEffectBaseRotation(const Mathf::Vector3& newBaseRotation)
{
	m_effectBaseRotation = newBaseRotation;

	// 실제 emitter 월드 회전 = Effect 기준 회전 + 이 ParticleSystem의 상대회전
	Mathf::Vector3 finalWorldRotation = m_effectBaseRotation + m_rotation;

	// SpawnModule들에 최종 계산된 회전값 전달
	for (auto it = m_moduleList.begin(); it != m_moduleList.end(); ++it) {
		ParticleModule& module = *it;

		if (SpawnModuleCS* spawnModule = dynamic_cast<SpawnModuleCS*>(&module)) {
			spawnModule->SetEmitterRotation(-finalWorldRotation);
		}
		if (MeshSpawnModuleCS* meshSpawnModule = dynamic_cast<MeshSpawnModuleCS*>(&module)) {
			meshSpawnModule->SetEmitterRotation(finalWorldRotation);
		}
		//if (TrailGenerateModule* trailModule = dynamic_cast<TrailGenerateModule*>(&module)) {
		//	trailModule->SetEmitterRotation(finalWorldRotation);
		//	continue;
		//}
	}
}

void ParticleSystem::UpdateGenerateModule(float delta)
{
	for (auto it = m_moduleList.begin(); it != m_moduleList.end(); ++it)
	{
		ParticleModule& module = *it;
		TrailGenerateModule* trailModule = dynamic_cast<TrailGenerateModule*>(&module);
		if (trailModule)
		{
			//trailModule->SetPosition(m_position);
			trailModule->Update(delta);
		}
	}
}

void ParticleSystem::ExecuteSimulationModules(float delta)
{
	// 현재 읽기/쓰기 버퍼 결정
	ID3D11UnorderedAccessView* inputUAV = m_usingBufferA ? m_particleUAV_A : m_particleUAV_B;
	ID3D11ShaderResourceView* inputSRV = m_usingBufferA ? m_particleSRV_A : m_particleSRV_B;
	ID3D11UnorderedAccessView* outputUAV = m_usingBufferA ? m_particleUAV_B : m_particleUAV_A;
	ID3D11ShaderResourceView* outputSRV = m_usingBufferA ? m_particleSRV_B : m_particleSRV_A;

	int executedModuleCount = 0;

	// 활성화된 모듈만 실행
	for (auto it = m_moduleList.begin(); it != m_moduleList.end(); ++it)
	{
		ParticleModule& module = *it;

		// 비활성화된 모듈은 건너뛰기 (버퍼 스왑도 하지 않음)
		if (!module.IsEnabled()) {
			continue;
		}

		if (module.IsGenerateModule())
			continue;

		// 버퍼 설정 및 실행
		module.SetBuffers(inputUAV, inputSRV, outputUAV, outputSRV);
		module.Update(delta);

		executedModuleCount++;

		// 실제로 실행된 모듈에 대해서만 버퍼 스왑
		std::swap(inputUAV, outputUAV);
		std::swap(inputSRV, outputSRV);
	}

	// 실제 실행된 모듈 개수가 홀수면 최종 버퍼 상태 변경
	if (executedModuleCount % 2 == 1) {
		m_usingBufferA = !m_usingBufferA;
	}

	DirectX11::DeviceStates->g_pDeviceContext->Flush();
}

void ParticleSystem::SyncEmitterTransform()
{
	// 최종 월드 위치/회전/스케일 계산
	Mathf::Vector3 finalWorldPosition = m_effectBasePosition + m_position;
	Mathf::Vector3 finalWorldRotation = m_effectBaseRotation + m_rotation;
	Mathf::Vector3 finalWorldScale = m_scale;

	// Movement 모듈들에 이미터 변환 정보 전달
	for (auto it = m_moduleList.begin(); it != m_moduleList.end(); ++it)
	{
		ParticleModule& module = *it;

		// MeshMovementModuleCS에 이미터 변환 전달
		if (MeshMovementModuleCS* movementModule = dynamic_cast<MeshMovementModuleCS*>(&module))
		{
			movementModule->SetEmitterTransform(
				finalWorldPosition,
				finalWorldRotation
			);
		}

		// 일반 MovementModuleCS에도 필요시 전달
		if (MovementModuleCS* movementModule = dynamic_cast<MovementModuleCS*>(&module))
		{
			movementModule->SetEmitterTransform(
				finalWorldPosition,
				finalWorldRotation
			);
		}

		if (MeshSizeModuleCS* sizeModule = dynamic_cast<MeshSizeModuleCS*>(&module))
		{
			sizeModule->SetEmitterTransform(finalWorldScale);
		}

		if (SizeModuleCS* sizeModule = dynamic_cast<SizeModuleCS*>(&module))
		{
			sizeModule->SetEmitterTransform(finalWorldScale);
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
	m_rotation = rotation;  // 상대회전 저장

	// Effect 기준 회전이 설정되어 있다면 즉시 업데이트
	UpdateEffectBaseRotation(m_effectBaseRotation);
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
		HRESULT hr = DirectX11::DeviceStates->g_pDevice->CreateBuffer(&bufferDesc, &initData, &m_particleBufferA);
		if (FAILED(hr)) {
			OutputDebugStringA("Failed to create mesh particle buffer A\n");
			return;
		}

		hr = DirectX11::DeviceStates->g_pDevice->CreateBuffer(&bufferDesc, &initData, &m_particleBufferB);
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
		HRESULT hr = DirectX11::DeviceStates->g_pDevice->CreateBuffer(&bufferDesc, &initData, &m_particleBufferA);
		if (FAILED(hr)) {
			OutputDebugStringA("Failed to create standard particle buffer A\n");
			return;
		}

		hr = DirectX11::DeviceStates->g_pDevice->CreateBuffer(&bufferDesc, &initData, &m_particleBufferB);
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

	HRESULT hr = DirectX11::DeviceStates->g_pDevice->CreateUnorderedAccessView(m_particleBufferA, &uavDesc, &m_particleUAV_A);
	if (FAILED(hr)) {
		OutputDebugStringA("Failed to create particle UAV A\n");
		return;
	}

	hr = DirectX11::DeviceStates->g_pDevice->CreateUnorderedAccessView(m_particleBufferB, &uavDesc, &m_particleUAV_B);
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

	hr = DirectX11::DeviceStates->g_pDevice->CreateShaderResourceView(m_particleBufferA, &srvDesc, &m_particleSRV_A);
	if (FAILED(hr)) {
		OutputDebugStringA("Failed to create particle SRV A\n");
		return;
	}

	hr = DirectX11::DeviceStates->g_pDevice->CreateShaderResourceView(m_particleBufferB, &srvDesc, &m_particleSRV_B);
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
	DirectX11::DeviceStates->g_pDeviceContext->Flush();
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

void ParticleSystem::SetScale(const Mathf::Vector3& scale)
{
	m_scale = scale;

	// SpawnModule들에 회전값 전달
	for (auto it = m_moduleList.begin(); it != m_moduleList.end(); ++it)
	{
		ParticleModule& module = *it;

		// SpawnModuleCS인지 확인
		if (SpawnModuleCS* spawnModule = dynamic_cast<SpawnModuleCS*>(&module))
		{

			spawnModule->SetEmitterScale(scale);
		}
		// MeshSpawnModuleCS인지 확인
		else if (MeshSpawnModuleCS* meshSpawnModule = dynamic_cast<MeshSpawnModuleCS*>(&module))
		{
			meshSpawnModule->SetEmitterScale(scale);
		}
	}
}

void ParticleSystem::ResizeParticleSystem(UINT newMaxParticles)
{
	if (newMaxParticles == m_maxParticles)
		return;

	// 1. 파티클 시스템 일시 정지 및 GPU 작업 완료 대기
	bool wasRunning = m_isRunning;
	m_isRunning = false;
	DirectX11::DeviceStates->g_pDeviceContext->Flush();

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

ID3D11ShaderResourceView* ParticleSystem::GetCurrentRenderingSRV()
{
	// 실제 활성화된 모듈 개수 계산
	int enabledModuleCount = 0;
	for (auto it = m_moduleList.begin(); it != m_moduleList.end(); ++it) {
		const ParticleModule& module = *it;
		if (module.IsEnabled()) {
			enabledModuleCount++;
		}
	}

	bool finalIsBufferA = m_usingBufferA;

	// 활성화된 모듈 개수가 홀수이면 반전
	if (enabledModuleCount % 2 == 1) {
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

// 풀 관련 **************************************************************************************************************************************************

void ParticleSystem::ResetForReuse()
{
	// GPU 작업 완료 대기 제거 - 필요 없음
	// WaitForGPUCompletion(); // 제거

	// 논리적 상태만 리셋
	m_isRunning = false;
	m_activeParticleCount = 0;
	m_effectProgress = 0.0f;
	m_usingBufferA = true;
	m_modulesConnected = false;

	// 모듈들 논리적 리셋 (각 모듈이 스레드 안전하게 처리)
	for (auto it = m_moduleList.begin(); it != m_moduleList.end(); ++it) {
		ParticleModule& module = *it;
		if (module.IsEnabled()) {
			module.ResetForReuse(); // 각 모듈이 뮤텍스로 보호됨
		}
	}

	for (auto* renderModule : m_renderModules) {
		if (renderModule && renderModule->IsEnabled()) {
			renderModule->ResetForReuse(); // 각 모듈이 뮤텍스로 보호됨
		}
	}

	// CPU 데이터만 초기화
	for (auto& particle : m_particleData) {
		particle.isActive = 0;
	}
	m_activeParticleCount = 0;
}

bool ParticleSystem::IsReadyForReuse()
{
	// 실행 중이면 재사용 불가
	if (m_isRunning) {
		return false;
	}

	// GPU 작업 완료 대기 (추가)
	DirectX11::DeviceStates->g_pDeviceContext->Flush();

	// 모든 모듈이 재사용 준비가 되었는지 확인
	for (auto it = m_moduleList.begin(); it != m_moduleList.end(); ++it) {
		const ParticleModule& module = *it;

		// 활성화된 모듈만 확인 (비활성화된 모듈은 스킵)
		if (module.IsEnabled() && !module.IsReadyForReuse()) {
			return false;
		}
	}

	// 렌더 모듈들도 확인
	for (const auto* renderModule : m_renderModules) {
		if (renderModule && renderModule->IsEnabled()) {
			if (!renderModule->IsReadyForReuse()) {
				return false;
			}
		}
	}

	return true;
}

void ParticleSystem::WaitForGPUCompletion()
{
	// 실제 GPU 대기는 필요 없음
	// 각 모듈의 WaitForGPUCompletion도 실제로는 아무 작업 안함
	for (auto it = m_moduleList.begin(); it != m_moduleList.end(); ++it) {
		ParticleModule& module = *it;
		module.WaitForGPUCompletion(); // 빈 함수
	}
}

//***********************************************************************************************************************************************************


void ParticleSystem::AutoConnectModules()
{
	if (m_modulesConnected) return;

	AutoConnectTrailModules();

	m_modulesConnected = true;
}


void ParticleSystem::AutoConnectTrailModules()
{
	TrailGenerateModule* trailGenModule = nullptr;
	TrailRenderModule* trailRenderModule = nullptr;

	// TrailGenerateModule 찾기
	for (auto& module : m_moduleList) {
		if (auto* tgm = dynamic_cast<TrailGenerateModule*>(&module)) {
			trailGenModule = tgm;
			break;
		}
	}

	// TrailRenderModule 찾기
	for (auto* renderModule : m_renderModules) {
		if (auto* trm = dynamic_cast<TrailRenderModule*>(renderModule)) {
			trailRenderModule = trm;
			break;
		}
	}

	// 연결 및 초기화 상태 검증
	if (trailGenModule && trailRenderModule) {
		if (!trailGenModule->IsInitialized()) {
			trailGenModule->Initialize();
		}
		trailRenderModule->SetTrailGenerateModule(trailGenModule);
	}
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