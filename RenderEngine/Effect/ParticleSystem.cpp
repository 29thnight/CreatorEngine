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

	// ��� ��ȸ
	bool isFirstModule = true;
	for (auto it = m_moduleList.begin(); it != m_moduleList.end(); ++it) {
		ParticleModule& module = *it;

		// ���� ����� ����� ���� ����
		if (m_usingBufferA) {
			// A�� �Է�, B�� ���
			module.SetBuffers(m_particleUAV_A, m_particleSRV_A, m_particleUAV_B, m_particleSRV_B);
		}
		else {
			// B�� �Է�, A�� ���
			module.SetBuffers(m_particleUAV_B, m_particleSRV_B, m_particleUAV_A, m_particleSRV_A);
		}

		// ���� ���� ����
		if (auto* lifeModule = dynamic_cast<LifeModuleCS*>(&module)) {
			// Life ����� ���� �ε��� ���ۿ� ����
			lifeModule->SetSharedBuffers(m_nextIndicesUAV, m_inactiveCountUAV, m_activeCountUAV);
			module.Update(delta, m_particleData);
			m_activeParticleCount = lifeModule->GetActiveParticleCount();
		}
		else if (auto* spawnModule = dynamic_cast<SpawnModuleCS*>(&module)) {
			// Spawn ����� ���� �ε��� ���ۿ��� �б�
			spawnModule->SetSharedBuffers(m_inactiveIndicesUAV, m_inactiveCountUAV, m_activeCountUAV);
			module.Update(delta, m_particleData);
		}
		else {
			module.Update(delta, m_particleData);
		}

		// �� ��� ó�� �� ���� ����
		m_usingBufferA = !m_usingBufferA;
		isFirstModule = false;
	}

	// ������ ������ ��Ȱ�� �ε��� ���� ����
	SwapIndexBuffer();

	// Ȧ�� ��� ���� ���� ���� (�ʿ��)
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
			// PSO ����
		renderModule->GetPSO()->Apply();

		// ������ ����
		renderModule->Render(world, view, projection);
		//}
	}

}

void ParticleSystem::SetPosition(const Mathf::Vector3& position)
{
	m_position = position;
	// SpawnModule�� ��ġ�� ���� �������� �����Ƿ�, ��� ��ƼŬ�� ��ġ�� �̵�
	for (auto& particle : m_particleData) {
		if (particle.isActive) {
			// ���� ��ġ���� �� ��ġ���� ������ ����
			particle.position += position - m_position;
		}
	}
}

void ParticleSystem::CreateParticleBuffer(UINT numParticles)
{
	// ���� ���� �ʱ�ȭ
	D3D11_BUFFER_DESC bufferDesc = {};
	bufferDesc.ByteWidth = sizeof(ParticleData) * numParticles;
	bufferDesc.Usage = D3D11_USAGE_DEFAULT;
	bufferDesc.BindFlags = D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE;
	bufferDesc.CPUAccessFlags = 0;
	bufferDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
	bufferDesc.StructureByteStride = sizeof(ParticleData);

	// �ʱ� ������ ����
	D3D11_SUBRESOURCE_DATA initialData = {};
	initialData.pSysMem = m_particleData.data();

	// ���� A ����
	HRESULT hr = DeviceState::g_pDevice->CreateBuffer(&bufferDesc, &initialData, &m_particleBufferA);
	if (FAILED(hr)) {
		// ���� ó��
		return;
	}

	// ���� B ����
	hr = DeviceState::g_pDevice->CreateBuffer(&bufferDesc, &initialData, &m_particleBufferB);
	if (FAILED(hr)) {
		// ���� ó��
		return;
	}

	// UAV ���� �ʱ�ȭ
	D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
	uavDesc.Format = DXGI_FORMAT_UNKNOWN;
	uavDesc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
	uavDesc.Buffer.FirstElement = 0;
	uavDesc.Buffer.NumElements = numParticles;
	uavDesc.Buffer.Flags = 0;

	// UAV A ����
	hr = DeviceState::g_pDevice->CreateUnorderedAccessView(m_particleBufferA, &uavDesc, &m_particleUAV_A);
	if (FAILED(hr)) {
		// ���� ó��
		return;
	}

	// UAV B ����
	hr = DeviceState::g_pDevice->CreateUnorderedAccessView(m_particleBufferB, &uavDesc, &m_particleUAV_B);
	if (FAILED(hr)) {
		// ���� ó��
		return;
	}

	// SRV ���� �ʱ�ȭ
	D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.Format = DXGI_FORMAT_UNKNOWN;
	srvDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
	srvDesc.Buffer.FirstElement = 0;
	srvDesc.Buffer.NumElements = numParticles;

	// SRV A ����
	hr = DeviceState::g_pDevice->CreateShaderResourceView(m_particleBufferA, &srvDesc, &m_particleSRV_A);
	if (FAILED(hr)) {
		// ���� ó��
		return;
	}

	// SRV B ����
	hr = DeviceState::g_pDevice->CreateShaderResourceView(m_particleBufferB, &srvDesc, &m_particleSRV_B);
	if (FAILED(hr)) {
		// ���� ó��
		return;
	}

	// �ʱ� ���´� A ���۸� ���
	m_usingBufferA = true;
}

void ParticleSystem::CreateSharedBuffers()
{
	// ��Ȱ�� ��ƼŬ �ε��� ���� (��� ��ƼŬ ����ŭ)
	D3D11_BUFFER_DESC inactiveIndexDesc = {};
	inactiveIndexDesc.ByteWidth = sizeof(UINT) * m_maxParticles;
	inactiveIndexDesc.Usage = D3D11_USAGE_DEFAULT;
	inactiveIndexDesc.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
	inactiveIndexDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
	inactiveIndexDesc.StructureByteStride = sizeof(UINT);

	// �ʱ⿡�� ��� ��ƼŬ�� ��Ȱ�� ���·� ����
	std::vector<UINT> initialIndices(m_maxParticles);
	for (UINT i = 0; i < m_maxParticles; ++i)
		initialIndices[i] = i;

	D3D11_SUBRESOURCE_DATA indexData = {};
	indexData.pSysMem = initialIndices.data();

	HRESULT hr = DeviceState::g_pDevice->CreateBuffer(&inactiveIndexDesc, &indexData, &m_inactiveIndicesBuffer);
	if (FAILED(hr))
		return; // ���� ó��

	// ��Ȱ�� ī���� ����
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
		return; // ���� ó��

	// Ȱ�� ��ƼŬ ī���� ����
	hr = DeviceState::g_pDevice->CreateBuffer(&countDesc, nullptr, &m_activeCountBuffer);
	if (FAILED(hr))
		return; // ���� ó��

	// UAV ����
	D3D11_UNORDERED_ACCESS_VIEW_DESC inactiveIndexUAVDesc = {};
	inactiveIndexUAVDesc.Format = DXGI_FORMAT_UNKNOWN;
	inactiveIndexUAVDesc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
	inactiveIndexUAVDesc.Buffer.FirstElement = 0;
	inactiveIndexUAVDesc.Buffer.NumElements = m_maxParticles;
	inactiveIndexUAVDesc.Buffer.Flags = 0;

	hr = DeviceState::g_pDevice->CreateUnorderedAccessView(m_inactiveIndicesBuffer, &inactiveIndexUAVDesc, &m_inactiveIndicesUAV);
	if (FAILED(hr))
		return; // ���� ó��

	D3D11_UNORDERED_ACCESS_VIEW_DESC countUAVDesc = {};
	countUAVDesc.Format = DXGI_FORMAT_UNKNOWN;
	countUAVDesc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
	countUAVDesc.Buffer.FirstElement = 0;
	countUAVDesc.Buffer.NumElements = 1;
	countUAVDesc.Buffer.Flags = 0;

	hr = DeviceState::g_pDevice->CreateUnorderedAccessView(m_inactiveCountBuffer, &countUAVDesc, &m_inactiveCountUAV);
	if (FAILED(hr))
		return; // ���� ó��

	hr = DeviceState::g_pDevice->CreateUnorderedAccessView(m_activeCountBuffer, &countUAVDesc, &m_activeCountUAV);
	if (FAILED(hr))
		return; // ���� ó��

	// ���� �ε��� ���� (ó������ m_inactiveIndicesBuffer�� ����)
	D3D11_BUFFER_DESC currentIndexDesc = inactiveIndexDesc;
	DeviceState::g_pDevice->CreateBuffer(&currentIndexDesc, &indexData, &m_currentIndicesBuffer);

	// ���� �ε��� ���� (�� ���·� ����)
	D3D11_BUFFER_DESC nextIndexDesc = inactiveIndexDesc;
	DeviceState::g_pDevice->CreateBuffer(&nextIndexDesc, nullptr, &m_nextIndicesBuffer);

	// UAV ����
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
	// ��Ȱ�� �ε��� �迭 �ʱ�ȭ
	std::vector<UINT> indices(m_particleData.size());
	for (size_t i = 0; i < m_particleData.size(); i++)
	{
		indices[i] = static_cast<UINT>(i);
	}

	// GPU ���ۿ� ���ε�
	D3D11_MAPPED_SUBRESOURCE mappedResource;
	if (SUCCEEDED(DeviceState::g_pDeviceContext->Map(m_inactiveIndicesBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedResource)))
	{
		memcpy(mappedResource.pData, indices.data(), indices.size() * sizeof(UINT));
		DeviceState::g_pDeviceContext->Unmap(m_inactiveIndicesBuffer, 0);
	}

	// ī���� �ʱ�ȭ
	UINT count = static_cast<UINT>(m_particleData.size());
	if (SUCCEEDED(DeviceState::g_pDeviceContext->Map(m_inactiveCountBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedResource)))
	{
		memcpy(mappedResource.pData, &count, sizeof(UINT));
		DeviceState::g_pDeviceContext->Unmap(m_inactiveCountBuffer, 0);
	}
}

void ParticleSystem::SwapIndexBuffer()
{
	// ����� ���� �ε��� ���� ��ü
	std::swap(m_currentIndicesBuffer, m_nextIndicesBuffer);
	std::swap(m_currentIndicesUAV, m_nextIndicesUAV);

	// ���� ���� �ʱ�ȭ (�ʿ��)
	UINT zero = 0;
	DeviceState::g_pDeviceContext->ClearUnorderedAccessViewUint(m_nextIndicesUAV, &zero);
}

void ParticleSystem::ResizeParticleSystem(UINT newMaxParticles)
{
	if (newMaxParticles == m_maxParticles)
		return; // ũ�Ⱑ ������ ���� �ʿ� ����

	// 1. ���� ��ƼŬ ������ ��� (�ʿ��)
	std::vector<ParticleData> oldParticleData;
	if (newMaxParticles > m_maxParticles) {
		// Ȯ���ϴ� ���: ���� ������ ����
		oldParticleData = m_particleData;
	}

	// 2. ���� ���ҽ� ����
	ReleaseBuffers();
	ReleaseSharedBuffer();

	// 3. ��ƼŬ ������ ���� ũ�� ����
	m_maxParticles = newMaxParticles;
	m_particleData.resize(newMaxParticles);
	m_instanceData.resize(newMaxParticles);

	// 4. ��ƼŬ ������ �ʱ�ȭ
	for (auto& particle : m_particleData) {
		particle.isActive = false;
	}

	// 5. ���� ������ ���� (ũ�� Ȯ�� ��)
	if (!oldParticleData.empty()) {
		// ���� ��ƼŬ ������ ���� (Ȱ�� ��ƼŬ��)
		size_t copyCount = std::min(oldParticleData.size(), m_particleData.size());
		for (size_t i = 0; i < copyCount; ++i) {
			if (oldParticleData[i].isActive) {
				m_particleData[i] = oldParticleData[i];
			}
		}
	}

	// 6. �� ���� ����
	CreateParticleBuffer(m_maxParticles);
	CreateSharedBuffers();

	// 7. ��⿡ �� ũ�� �˸� (�ʿ��)
	for (auto it = m_moduleList.begin(); it != m_moduleList.end(); ++it) {
		ParticleModule& module = *it;
		module.OnSystemResized(m_maxParticles);
	}

	// 8. Ȱ�� ��ƼŬ �� ������Ʈ
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
		// ��� ������ Ȧ���̸� ����
		finalIsBufferA = !finalIsBufferA;
	}

	return finalIsBufferA ? m_particleSRV_A : m_particleSRV_B;
}

void ParticleSystem::ConfigureModuleBuffers(ParticleModule& module, bool isFirstModule)
{
	// ����ȭ�� ����: ���� ��� ���� ���۰� �Է�, �ٸ� ���۰� ���
	if (m_usingBufferA) {
		// A�� �Է�(�б�), B�� ���(����) ����
		module.SetBuffers(
			m_particleUAV_A, m_particleSRV_A,  // �Է�
			m_particleUAV_B, m_particleSRV_B   // ���
		);
		//std::cout << "  Module using: Buffer A �� Buffer B" << std::endl;
	}
	else {
		// B�� �Է�(�б�), A�� ���(����) ����
		module.SetBuffers(
			m_particleUAV_B, m_particleSRV_B,  // �Է�
			m_particleUAV_A, m_particleSRV_A   // ���
		);
		//std::cout << "  Module using: Buffer B �� Buffer A" << std::endl;
	}
}
