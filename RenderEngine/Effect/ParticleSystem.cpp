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
	CreateSharedBuffers();
	InitializeParticleIndices();
}

ParticleSystem::~ParticleSystem()
{
	//auto it = m_moduleList.begin();
	//ParticleModule* current = nullptr;
	//
	//while (it != m_moduleList.end())
	//{
	//	current = &(*it);
	//	++it;
	//	delete current;
	//}
	//
	//m_moduleList.ClearLink();

	ReleaseBuffers();
	ReleaseSharedBuffer();

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
}

void ParticleSystem::Update(float delta)
{
	if (!m_isRunning || m_isPaused)
		return;

	// 모듈 순회
	bool isFirstModule = true;
	for (auto it = m_moduleList.begin(); it != m_moduleList.end(); ++it) {
		ParticleModule& module = *it;

		// 현재 모듈의 입출력 버퍼 설정
		if (m_usingBufferA) {
			// A가 입력, B가 출력
			module.SetBuffers(m_particleUAV_A, m_particleSRV_A, m_particleUAV_B, m_particleSRV_B);
		}
		else {
			// B가 입력, A가 출력
			module.SetBuffers(m_particleUAV_B, m_particleSRV_B, m_particleUAV_A, m_particleSRV_A);
		}

		// 공유 버퍼 설정
		if (auto* lifeModule = dynamic_cast<LifeModuleCS*>(&module)) {
			// Life 모듈은 다음 인덱스 버퍼에 쓰기
			lifeModule->SetSharedBuffers(m_nextIndicesUAV, m_inactiveCountUAV, m_activeCountUAV);
			module.Update(delta, m_particleData);
			m_activeParticleCount = lifeModule->GetActiveParticleCount();
		}
		else if (auto* spawnModule = dynamic_cast<SpawnModuleCS*>(&module)) {
			// Spawn 모듈은 현재 인덱스 버퍼에서 읽기
			spawnModule->SetSharedBuffers(m_inactiveIndicesUAV, m_inactiveCountUAV, m_activeCountUAV);
			module.Update(delta, m_particleData);
		}
		else {
			module.Update(delta, m_particleData);
		}

		// 각 모듈 처리 후 버퍼 스왑
		m_usingBufferA = !m_usingBufferA;
		isFirstModule = false;
	}

	// 프레임 끝에서 비활성 인덱스 버퍼 스왑
	SwapIndexBuffer();

	// 홀수 모듈 수에 대한 조정 (필요시)
	if (m_moduleList.size() % 2 == 1) {
		m_usingBufferA = !m_usingBufferA;
	}
}
void ParticleSystem::Render(RenderScene& scene, Camera& camera)
{
	if (!m_isRunning) //|| m_activeParticleCount == 0)
		return;

	auto& deviceContext = DeviceState::g_pDeviceContext;

	//SaveRenderState();

	Mathf::Matrix world = XMMatrixIdentity();
	Mathf::Matrix view = XMMatrixTranspose(camera.CalculateView());
	Mathf::Matrix projection = XMMatrixTranspose(camera.CalculateProjection());

	//for (auto* renderModule : m_renderModules)
	//{
	//	if (auto* billboardModule = dynamic_cast<BillboardModule*>(renderModule))
	//	{
	//		billboardModule->SetupInstancing(m_instanceData.data(), m_activeParticleCount);
	//	}
	//}

	for (auto* renderModule : m_renderModules)
	{
		//if (m_activeParticleCount > 0)
		//{
			// PSO 적용
		renderModule->GetPSO()->Apply();

		// 렌더링 수행
		renderModule->Render(world, view, projection);
		//}
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

void ParticleSystem::CreateParticleBuffer(UINT numParticles)
{
	// 버퍼 설명 초기화
	D3D11_BUFFER_DESC bufferDesc = {};
	bufferDesc.ByteWidth = sizeof(ParticleData) * numParticles;
	bufferDesc.Usage = D3D11_USAGE_DEFAULT;
	bufferDesc.BindFlags = D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE;
	bufferDesc.CPUAccessFlags = 0;
	bufferDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
	bufferDesc.StructureByteStride = sizeof(ParticleData);

	// 초기 데이터 설정
	D3D11_SUBRESOURCE_DATA initialData = {};
	initialData.pSysMem = m_particleData.data();

	// 버퍼 A 생성
	HRESULT hr = DeviceState::g_pDevice->CreateBuffer(&bufferDesc, &initialData, &m_particleBufferA);
	if (FAILED(hr)) {
		// 오류 처리
		return;
	}

	// 버퍼 B 생성
	hr = DeviceState::g_pDevice->CreateBuffer(&bufferDesc, &initialData, &m_particleBufferB);
	if (FAILED(hr)) {
		// 오류 처리
		return;
	}

	// UAV 설명 초기화
	D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
	uavDesc.Format = DXGI_FORMAT_UNKNOWN;
	uavDesc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
	uavDesc.Buffer.FirstElement = 0;
	uavDesc.Buffer.NumElements = numParticles;
	uavDesc.Buffer.Flags = 0;

	// UAV A 생성
	hr = DeviceState::g_pDevice->CreateUnorderedAccessView(m_particleBufferA, &uavDesc, &m_particleUAV_A);
	if (FAILED(hr)) {
		// 오류 처리
		return;
	}

	// UAV B 생성
	hr = DeviceState::g_pDevice->CreateUnorderedAccessView(m_particleBufferB, &uavDesc, &m_particleUAV_B);
	if (FAILED(hr)) {
		// 오류 처리
		return;
	}

	// SRV 설명 초기화
	D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.Format = DXGI_FORMAT_UNKNOWN;
	srvDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
	srvDesc.Buffer.FirstElement = 0;
	srvDesc.Buffer.NumElements = numParticles;

	// SRV A 생성
	hr = DeviceState::g_pDevice->CreateShaderResourceView(m_particleBufferA, &srvDesc, &m_particleSRV_A);
	if (FAILED(hr)) {
		// 오류 처리
		return;
	}

	// SRV B 생성
	hr = DeviceState::g_pDevice->CreateShaderResourceView(m_particleBufferB, &srvDesc, &m_particleSRV_B);
	if (FAILED(hr)) {
		// 오류 처리
		return;
	}

	// 초기 상태는 A 버퍼를 사용
	m_usingBufferA = true;
}

void ParticleSystem::CreateSharedBuffers()
{
	// 비활성 파티클 인덱스 버퍼 (모든 파티클 수만큼)
	D3D11_BUFFER_DESC inactiveIndexDesc = {};
	inactiveIndexDesc.ByteWidth = sizeof(UINT) * m_maxParticles;
	inactiveIndexDesc.Usage = D3D11_USAGE_DEFAULT;
	inactiveIndexDesc.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
	inactiveIndexDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
	inactiveIndexDesc.StructureByteStride = sizeof(UINT);

	// 초기에는 모든 파티클이 비활성 상태로 가정
	std::vector<UINT> initialIndices(m_maxParticles);
	for (UINT i = 0; i < m_maxParticles; ++i)
		initialIndices[i] = i;

	D3D11_SUBRESOURCE_DATA indexData = {};
	indexData.pSysMem = initialIndices.data();

	HRESULT hr = DeviceState::g_pDevice->CreateBuffer(&inactiveIndexDesc, &indexData, &m_inactiveIndicesBuffer);
	if (FAILED(hr))
		return; // 오류 처리

	// 비활성 카운터 버퍼
	D3D11_BUFFER_DESC countDesc = {};
	countDesc.ByteWidth = sizeof(UINT);
	countDesc.Usage = D3D11_USAGE_DEFAULT;
	countDesc.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
	countDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
	countDesc.StructureByteStride = sizeof(UINT);

	UINT initialCount = m_maxParticles;
	D3D11_SUBRESOURCE_DATA countData = {};
	countData.pSysMem = &initialCount;

	hr = DeviceState::g_pDevice->CreateBuffer(&countDesc, &countData, &m_inactiveCountBuffer);
	if (FAILED(hr))
		return; // 오류 처리

	// 활성 파티클 카운터 버퍼
	hr = DeviceState::g_pDevice->CreateBuffer(&countDesc, nullptr, &m_activeCountBuffer);
	if (FAILED(hr))
		return; // 오류 처리

	// UAV 생성
	D3D11_UNORDERED_ACCESS_VIEW_DESC inactiveIndexUAVDesc = {};
	inactiveIndexUAVDesc.Format = DXGI_FORMAT_UNKNOWN;
	inactiveIndexUAVDesc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
	inactiveIndexUAVDesc.Buffer.FirstElement = 0;
	inactiveIndexUAVDesc.Buffer.NumElements = m_maxParticles;
	inactiveIndexUAVDesc.Buffer.Flags = 0;

	hr = DeviceState::g_pDevice->CreateUnorderedAccessView(m_inactiveIndicesBuffer, &inactiveIndexUAVDesc, &m_inactiveIndicesUAV);
	if (FAILED(hr))
		return; // 오류 처리

	D3D11_UNORDERED_ACCESS_VIEW_DESC countUAVDesc = {};
	countUAVDesc.Format = DXGI_FORMAT_UNKNOWN;
	countUAVDesc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
	countUAVDesc.Buffer.FirstElement = 0;
	countUAVDesc.Buffer.NumElements = 1;
	countUAVDesc.Buffer.Flags = 0;

	hr = DeviceState::g_pDevice->CreateUnorderedAccessView(m_inactiveCountBuffer, &countUAVDesc, &m_inactiveCountUAV);
	if (FAILED(hr))
		return; // 오류 처리

	hr = DeviceState::g_pDevice->CreateUnorderedAccessView(m_activeCountBuffer, &countUAVDesc, &m_activeCountUAV);
	if (FAILED(hr))
		return; // 오류 처리

	// 현재 인덱스 버퍼 (처음에는 m_inactiveIndicesBuffer와 동일)
	D3D11_BUFFER_DESC currentIndexDesc = inactiveIndexDesc;
	DeviceState::g_pDevice->CreateBuffer(&currentIndexDesc, &indexData, &m_currentIndicesBuffer);

	// 다음 인덱스 버퍼 (빈 상태로 시작)
	D3D11_BUFFER_DESC nextIndexDesc = inactiveIndexDesc;
	DeviceState::g_pDevice->CreateBuffer(&nextIndexDesc, nullptr, &m_nextIndicesBuffer);

	// UAV 생성
	D3D11_UNORDERED_ACCESS_VIEW_DESC indexUAVDesc = inactiveIndexUAVDesc;
	DeviceState::g_pDevice->CreateUnorderedAccessView(m_currentIndicesBuffer, &indexUAVDesc, &m_currentIndicesUAV);
	DeviceState::g_pDevice->CreateUnorderedAccessView(m_nextIndicesBuffer, &indexUAVDesc, &m_nextIndicesUAV);


}

void ParticleSystem::ReleaseSharedBuffer()
{
	if (m_inactiveIndicesBuffer) { m_inactiveIndicesBuffer->Release(); m_inactiveIndicesBuffer = nullptr; }
	if (m_inactiveCountBuffer) { m_inactiveCountBuffer->Release(); m_inactiveCountBuffer = nullptr; }
	if (m_activeCountBuffer) { m_activeCountBuffer->Release(); m_activeCountBuffer = nullptr; }

	if (m_inactiveIndicesUAV) { m_inactiveIndicesUAV->Release(); m_inactiveIndicesUAV = nullptr; }
	if (m_inactiveCountUAV) { m_inactiveCountUAV->Release(); m_inactiveCountUAV = nullptr; }
	if (m_activeCountUAV) { m_activeCountUAV->Release(); m_activeCountUAV = nullptr; }
}

void ParticleSystem::InitializeParticleIndices()
{
	// 비활성 인덱스 배열 초기화
	std::vector<UINT> indices(m_particleData.size());
	for (size_t i = 0; i < m_particleData.size(); i++)
	{
		indices[i] = static_cast<UINT>(i);
	}

	// GPU 버퍼에 업로드
	D3D11_MAPPED_SUBRESOURCE mappedResource;
	if (SUCCEEDED(DeviceState::g_pDeviceContext->Map(m_inactiveIndicesBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedResource)))
	{
		memcpy(mappedResource.pData, indices.data(), indices.size() * sizeof(UINT));
		DeviceState::g_pDeviceContext->Unmap(m_inactiveIndicesBuffer, 0);
	}

	// 카운터 초기화
	UINT count = static_cast<UINT>(m_particleData.size());
	if (SUCCEEDED(DeviceState::g_pDeviceContext->Map(m_inactiveCountBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedResource)))
	{
		memcpy(mappedResource.pData, &count, sizeof(UINT));
		DeviceState::g_pDeviceContext->Unmap(m_inactiveCountBuffer, 0);
	}
}

void ParticleSystem::SwapIndexBuffer()
{
	// 현재와 다음 인덱스 버퍼 교체
	std::swap(m_currentIndicesBuffer, m_nextIndicesBuffer);
	std::swap(m_currentIndicesUAV, m_nextIndicesUAV);

	// 다음 버퍼 초기화 (필요시)
	UINT zero = 0;
	DeviceState::g_pDeviceContext->ClearUnorderedAccessViewUint(m_nextIndicesUAV, &zero);
}

void ParticleSystem::ResizeParticleSystem(UINT newMaxParticles)
{
	if (newMaxParticles == m_maxParticles)
		return; // 크기가 같으면 변경 필요 없음

	// 1. 기존 파티클 데이터 백업 (필요시)
	std::vector<ParticleData> oldParticleData;
	if (newMaxParticles > m_maxParticles) {
		// 확장하는 경우: 기존 데이터 보존
		oldParticleData = m_particleData;
	}

	// 2. 기존 리소스 정리
	ReleaseBuffers();
	ReleaseSharedBuffer();

	// 3. 파티클 데이터 벡터 크기 조정
	m_maxParticles = newMaxParticles;
	m_particleData.resize(newMaxParticles);
	m_instanceData.resize(newMaxParticles);

	// 4. 파티클 데이터 초기화
	for (auto& particle : m_particleData) {
		particle.isActive = false;
	}

	// 5. 기존 데이터 복원 (크기 확장 시)
	if (!oldParticleData.empty()) {
		// 기존 파티클 데이터 복사 (활성 파티클만)
		size_t copyCount = std::min(oldParticleData.size(), m_particleData.size());
		for (size_t i = 0; i < copyCount; ++i) {
			if (oldParticleData[i].isActive) {
				m_particleData[i] = oldParticleData[i];
			}
		}
	}

	// 6. 새 버퍼 생성
	CreateParticleBuffer(m_maxParticles);
	CreateSharedBuffers();

	// 7. 모듈에 새 크기 알림 (필요시)
	for (auto it = m_moduleList.begin(); it != m_moduleList.end(); ++it) {
		ParticleModule& module = *it;
		module.OnSystemResized(m_maxParticles);
	}

	// 8. 활성 파티클 수 업데이트
	m_activeParticleCount = 0;
	for (const auto& particle : m_particleData) {
		if (particle.isActive) {
			m_activeParticleCount++;
		}
	}
}

void ParticleSystem::ReleaseBuffers()
{
	if (m_inactiveIndicesBuffer) { m_inactiveIndicesBuffer->Release(); m_inactiveIndicesBuffer = nullptr; }
	if (m_inactiveCountBuffer) { m_inactiveCountBuffer->Release(); m_inactiveCountBuffer = nullptr; }
	if (m_activeCountBuffer) { m_activeCountBuffer->Release(); m_activeCountBuffer = nullptr; }

	if (m_inactiveIndicesUAV) { m_inactiveIndicesUAV->Release(); m_inactiveIndicesUAV = nullptr; }
	if (m_inactiveCountUAV) { m_inactiveCountUAV->Release(); m_inactiveCountUAV = nullptr; }
	if (m_activeCountUAV) { m_activeCountUAV->Release(); m_activeCountUAV = nullptr; }

	if (m_currentIndicesBuffer) { m_currentIndicesBuffer->Release(); m_currentIndicesBuffer = nullptr; }
	if (m_nextIndicesBuffer) { m_nextIndicesBuffer->Release(); m_nextIndicesBuffer = nullptr; }
	if (m_currentIndicesUAV) { m_currentIndicesUAV->Release(); m_currentIndicesUAV = nullptr; }
	if (m_nextIndicesUAV) { m_nextIndicesUAV->Release(); m_nextIndicesUAV = nullptr; }
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
	// 간소화된 로직: 현재 사용 중인 버퍼가 입력, 다른 버퍼가 출력
	if (m_usingBufferA) {
		// A가 입력(읽기), B가 출력(쓰기) 버퍼
		module.SetBuffers(
			m_particleUAV_A, m_particleSRV_A,  // 입력
			m_particleUAV_B, m_particleSRV_B   // 출력
		);
		//std::cout << "  Module using: Buffer A → Buffer B" << std::endl;
	}
	else {
		// B가 입력(읽기), A가 출력(쓰기) 버퍼
		module.SetBuffers(
			m_particleUAV_B, m_particleSRV_B,  // 입력
			m_particleUAV_A, m_particleSRV_A   // 출력
		);
		//std::cout << "  Module using: Buffer B → Buffer A" << std::endl;
	}
}
