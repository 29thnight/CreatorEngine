#include "RenderModules.h"
#include "../ShaderSystem.h"
#include "ParticleSystem.h"
#include "DataSystem.h"

// 관리
void RenderModules::CleanupRenderState()
{
	ID3D11ShaderResourceView* nullSRV = nullptr;
	DirectX11::DeviceStates->g_pDeviceContext->PSSetShaderResources(0, 1, &nullSRV);

	DirectX11::DeviceStates->g_pDeviceContext->GSSetShader(nullptr, nullptr, 0);
}

void RenderModules::SaveRenderState()
{
	auto& deviceContext = DirectX11::DeviceStates->g_pDeviceContext;

	if (m_prevDepthState) m_prevDepthState->Release();
	if (m_prevBlendState) m_prevBlendState->Release();
	if (m_prevRasterizerState) m_prevRasterizerState->Release();

	deviceContext->OMGetDepthStencilState(&m_prevDepthState, &m_prevStencilRef);
	deviceContext->OMGetBlendState(&m_prevBlendState, m_prevBlendFactor, &m_prevSampleMask);
	deviceContext->RSGetState(&m_prevRasterizerState);
}

void RenderModules::RestoreRenderState()
{
	auto& deviceContext = DirectX11::DeviceStates->g_pDeviceContext;

	deviceContext->OMSetDepthStencilState(m_prevDepthState, m_prevStencilRef);
	deviceContext->OMSetBlendState(m_prevBlendState, m_prevBlendFactor, m_prevSampleMask);
	deviceContext->RSSetState(m_prevRasterizerState);

	DirectX11::UnbindRenderTargets();
}

bool RenderModules::IsSystemRunning() const
{
	return m_ownerSystem ? m_ownerSystem->IsRunning() : false;
}

// pso 설정
void RenderModules::SetVertexShader(const std::string& shaderName)
{
	if (ShaderSystem->VertexShaders.find(shaderName) != ShaderSystem->VertexShaders.end()) {
		m_vertexShaderName = shaderName;
		UpdatePSOShaders();
		OnShadersChanged();
	}
}

void RenderModules::SetPixelShader(const std::string& shaderName)
{
	if (ShaderSystem->PixelShaders.find(shaderName) != ShaderSystem->PixelShaders.end()) {
		m_pixelShaderName = shaderName;
		UpdatePSOShaders();
		OnShadersChanged();
	}
}

void RenderModules::SetShaders(const std::string& vertexShader, const std::string& pixelShader)
{
	bool vsExists = ShaderSystem->VertexShaders.find(vertexShader) != ShaderSystem->VertexShaders.end();
	bool psExists = ShaderSystem->PixelShaders.find(pixelShader) != ShaderSystem->PixelShaders.end();

	if (vsExists && psExists) {
		m_vertexShaderName = vertexShader;
		m_pixelShaderName = pixelShader;
		UpdatePSOShaders();
		OnShadersChanged();
	}
}

void RenderModules::SetBlendPreset(BlendPreset preset)
{
	m_blendPreset = preset;
	CreateBlendState(preset);
	OnRenderStatesChanged();
}

void RenderModules::SetCustomBlendState(const D3D11_BLEND_DESC& desc)
{
	m_blendPreset = BlendPreset::Custom;
	m_customBlendDesc = desc;

	if (m_pso) {
		m_pso->m_blendState = nullptr;
		DirectX11::ThrowIfFailed(
			DirectX11::DeviceStates->g_pDevice->CreateBlendState(&desc, &m_pso->m_blendState)
		);
	}
	OnRenderStatesChanged();
}

void RenderModules::SetDepthPreset(DepthPreset preset)
{
	m_depthPreset = preset;
	CreateDepthStencilState(preset);
	OnRenderStatesChanged();
}

void RenderModules::SetCustomDepthStencilState(const D3D11_DEPTH_STENCIL_DESC& desc)
{
	m_depthPreset = DepthPreset::Custom;
	m_customDepthDesc = desc;

	if (m_pso) {
		m_pso->m_depthStencilState = nullptr;
		DirectX11::ThrowIfFailed(
			DirectX11::DeviceStates->g_pDevice->CreateDepthStencilState(&desc, &m_pso->m_depthStencilState)
		);
	}
	OnRenderStatesChanged();
}

void RenderModules::SetRasterizerPreset(RasterizerPreset preset)
{
	m_rasterizerPreset = preset;
	CreateRasterizerState(preset);
	OnRenderStatesChanged();
}

void RenderModules::SetCustomRasterizerState(const D3D11_RASTERIZER_DESC& desc)
{
	m_rasterizerPreset = RasterizerPreset::Custom;
	m_customRasterizerDesc = desc;

	if (m_pso) {
		m_pso->m_rasterizerState = nullptr;
		DirectX11::ThrowIfFailed(
			DirectX11::DeviceStates->g_pDevice->CreateRasterizerState(&desc, &m_pso->m_rasterizerState)
		);
	}
	OnRenderStatesChanged();
}

void RenderModules::UpdatePSOShaders()
{
	if (!m_pso || m_vertexShaderName == "None" || m_pixelShaderName == "None") {
		return;
	}

	auto vsIter = ShaderSystem->VertexShaders.find(m_vertexShaderName);
	auto psIter = ShaderSystem->PixelShaders.find(m_pixelShaderName);

	if (vsIter != ShaderSystem->VertexShaders.end() &&
		psIter != ShaderSystem->PixelShaders.end()) {

		m_pso->m_vertexShader = &vsIter->second;
		m_pso->m_pixelShader = &psIter->second;
	}
}

void RenderModules::UpdatePSORenderStates()
{
	if (!m_pso) return;

	CreateBlendState(m_blendPreset);
	CreateDepthStencilState(m_depthPreset);
	CreateRasterizerState(m_rasterizerPreset);
}


void RenderModules::CreateBlendState(BlendPreset preset)
{
	if (!m_pso) return;

	D3D11_BLEND_DESC desc = (preset == BlendPreset::Custom) ?
		m_customBlendDesc : GetBlendDesc(preset);

	m_pso->m_blendState = nullptr;
	DirectX11::ThrowIfFailed(
		DirectX11::DeviceStates->g_pDevice->CreateBlendState(&desc, &m_pso->m_blendState)
	);
}

void RenderModules::CreateDepthStencilState(DepthPreset preset)
{
	if (!m_pso) return;

	D3D11_DEPTH_STENCIL_DESC desc = (preset == DepthPreset::Custom) ?
		m_customDepthDesc : GetDepthStencilDesc(preset);

	m_pso->m_depthStencilState = nullptr;
	DirectX11::ThrowIfFailed(
		DirectX11::DeviceStates->g_pDevice->CreateDepthStencilState(&desc, &m_pso->m_depthStencilState)
	);
}

void RenderModules::CreateRasterizerState(RasterizerPreset preset)
{
	if (!m_pso) return;

	D3D11_RASTERIZER_DESC desc = (preset == RasterizerPreset::Custom) ?
		m_customRasterizerDesc : GetRasterizerDesc(preset);

	m_pso->m_rasterizerState= nullptr;
	DirectX11::ThrowIfFailed(
		DirectX11::DeviceStates->g_pDevice->CreateRasterizerState(&desc, &m_pso->m_rasterizerState)
	);
}

D3D11_BLEND_DESC RenderModules::GetBlendDesc(BlendPreset preset) const
{
	D3D11_BLEND_DESC desc = {};
	desc.AlphaToCoverageEnable = FALSE;
	desc.IndependentBlendEnable = FALSE;

	switch (preset) {
	case BlendPreset::None:
		desc.RenderTarget[0].BlendEnable = FALSE;
		break;

	case BlendPreset::Alpha:
		desc.RenderTarget[0].BlendEnable = TRUE;
		desc.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
		desc.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
		desc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
		desc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
		desc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ZERO;
		desc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
		break;

	case BlendPreset::Additive:
		desc.RenderTarget[0].BlendEnable = TRUE;
		desc.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
		desc.RenderTarget[0].DestBlend = D3D11_BLEND_ONE;
		desc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
		desc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
		desc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ZERO;
		desc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
		break;

	case BlendPreset::Multiply:
		desc.RenderTarget[0].BlendEnable = TRUE;
		desc.RenderTarget[0].SrcBlend = D3D11_BLEND_DEST_COLOR;
		desc.RenderTarget[0].DestBlend = D3D11_BLEND_ZERO;
		desc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
		desc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
		desc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
		desc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
		break;

	case BlendPreset::Subtractive:
		desc.RenderTarget[0].BlendEnable = TRUE;
		desc.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
		desc.RenderTarget[0].DestBlend = D3D11_BLEND_ONE;
		desc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_REV_SUBTRACT;
		desc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
		desc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ZERO;
		desc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
		break;
	}

	desc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
	return desc;
}

D3D11_DEPTH_STENCIL_DESC RenderModules::GetDepthStencilDesc(DepthPreset preset) const
{
	D3D11_DEPTH_STENCIL_DESC desc = {};

	switch (preset) {
	case DepthPreset::Default:
		desc.DepthEnable = TRUE;
		desc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
		desc.DepthFunc = D3D11_COMPARISON_LESS;
		break;

	case DepthPreset::ReadOnly:
		desc.DepthEnable = TRUE;
		desc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
		desc.DepthFunc = D3D11_COMPARISON_LESS;
		break;

	case DepthPreset::WriteOnly:
		desc.DepthEnable = FALSE;
		desc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
		desc.DepthFunc = D3D11_COMPARISON_ALWAYS;
		break;

	case DepthPreset::Disabled:
		desc.DepthEnable = FALSE;
		desc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
		desc.DepthFunc = D3D11_COMPARISON_ALWAYS;
		break;
	}

	desc.StencilEnable = FALSE;
	return desc;
}

D3D11_RASTERIZER_DESC RenderModules::GetRasterizerDesc(RasterizerPreset preset) const
{
	D3D11_RASTERIZER_DESC desc = {};
	desc.AntialiasedLineEnable = FALSE;
	desc.DepthBias = 0;
	desc.DepthBiasClamp = 0.0f;
	desc.DepthClipEnable = TRUE;
	desc.FrontCounterClockwise = FALSE;
	desc.MultisampleEnable = FALSE;
	desc.ScissorEnable = FALSE;
	desc.SlopeScaledDepthBias = 0.0f;

	switch (preset) {
	case RasterizerPreset::Default:
		desc.FillMode = D3D11_FILL_SOLID;
		desc.CullMode = D3D11_CULL_BACK;
		break;

	case RasterizerPreset::NoCull:
		desc.FillMode = D3D11_FILL_SOLID;
		desc.CullMode = D3D11_CULL_NONE;
		break;

	case RasterizerPreset::Wireframe:
		desc.FillMode = D3D11_FILL_WIREFRAME;
		desc.CullMode = D3D11_CULL_BACK;
		break;

	case RasterizerPreset::WireframeNoCull:
		desc.FillMode = D3D11_FILL_WIREFRAME;
		desc.CullMode = D3D11_CULL_NONE;
		break;
	}

	return desc;
}

std::vector<std::string> RenderModules::GetAvailableVertexShaders()
{
	std::vector<std::string> shaderNames;
	for (const auto& pair : ShaderSystem->VertexShaders) {
		shaderNames.push_back(pair.first);
	}
	return shaderNames;
}

std::vector<std::string> RenderModules::GetAvailablePixelShaders()
{
	std::vector<std::string> shaderNames;
	for (const auto& pair : ShaderSystem->PixelShaders) {
		shaderNames.push_back(pair.first);
	}
	return shaderNames;
}

nlohmann::json RenderModules::SerializeRenderStates() const
{
	nlohmann::json json;

	json["shaders"] = {
		{"vertexShader", m_vertexShaderName},
		{"pixelShader", m_pixelShaderName}
	};

	json["renderStates"] = {
		{"blendPreset", static_cast<int>(m_blendPreset)},
		{"depthPreset", static_cast<int>(m_depthPreset)},
		{"rasterizerPreset", static_cast<int>(m_rasterizerPreset)}
	};

	return json;
}

void RenderModules::DeserializeRenderStates(const nlohmann::json& json)
{
	if (json.contains("shaders")) {
		const auto& shaderJson = json["shaders"];

		if (shaderJson.contains("vertexShader")) {
			m_vertexShaderName = shaderJson["vertexShader"];
		}

		if (shaderJson.contains("pixelShader")) {
			m_pixelShaderName = shaderJson["pixelShader"];
		}
	}

	if (json.contains("renderStates")) {
		const auto& stateJson = json["renderStates"];

		if (stateJson.contains("blendPreset")) {
			m_blendPreset = static_cast<BlendPreset>(stateJson["blendPreset"]);
		}

		if (stateJson.contains("depthPreset")) {
			m_depthPreset = static_cast<DepthPreset>(stateJson["depthPreset"]);
		}

		if (stateJson.contains("rasterizerPreset")) {
			m_rasterizerPreset = static_cast<RasterizerPreset>(stateJson["rasterizerPreset"]);
		}
	}

	if (m_pso) {
		UpdatePSOShaders();
		UpdatePSORenderStates();
	}
}

// 모르겠음
void RenderModules::EnableClipping(bool enable)
{
	if (!SupportsClipping()) return;

	m_clippingEnabled = enable;
	m_clippingParams.polarClippingEnabled = enable ? 1.0f : 0.0f;

	OnClippingStateChanged();
	UpdateClippingBuffer();
}

void RenderModules::SetClippingProgress(float progress)
{
	if (!SupportsClipping()) return;
	// -1.0 ~ 1.0 범위로 확장 (음수는 역방향)
	m_clippingParams.polarClippingEnabled = std::clamp(progress, -1.0f, 1.0f);
	UpdateClippingBuffer();
}

void RenderModules::SetClippingAxis(const Mathf::Vector3& axis)
{
	if (!SupportsClipping()) return;

	// 벡터 정규화
	Mathf::Vector3 normalizedAxis = axis;
	float length = sqrt(axis.x * axis.x + axis.y * axis.y + axis.z * axis.z);

	if (length > 0.0001f) {
		normalizedAxis.x /= length;
		normalizedAxis.y /= length;
		normalizedAxis.z /= length;
	}
	else {
		normalizedAxis = Mathf::Vector3(1.0f, 0.0f, 0.0f);
	}

	m_clippingParams.polarReferenceDir = normalizedAxis;

	UpdateClippingBuffer();
}

void RenderModules::SetTexture(int slot, Texture* texture)
{
	EnsureTextureSlots(slot + 1);
	m_textures[slot] = texture;
}

void RenderModules::AddTexture(Texture* texture)
{
	m_textures.push_back(texture);
}

void RenderModules::RemoveTexture(int slot)
{
	if (slot >= 0 && slot < m_textures.size()) {
		m_textures[slot] = nullptr;
	}
}

void RenderModules::ClearTextures()
{
	m_textures.clear();
}

Texture* RenderModules::GetTexture(int slot) const
{
	if (slot >= 0 && slot < m_textures.size()) {
		return m_textures[slot];
	}
	return nullptr;
}

void RenderModules::EnsureTextureSlots(size_t count)
{
	if (m_textures.size() < count) {
		m_textures.resize(count, nullptr);
	}
}

void RenderModules::BindTextures()
{
	auto& deviceContext = DirectX11::DeviceStates->g_pDeviceContext;

	std::vector<ID3D11ShaderResourceView*> srvs(m_textures.size(), nullptr);

	for (size_t i = 0; i < m_textures.size(); ++i) {
		if (m_textures[i] && m_textures[i]->m_pSRV) {
			srvs[i] = m_textures[i]->m_pSRV;
		}
	}

	if (!srvs.empty()) {
		deviceContext->PSSetShaderResources(0, srvs.size(), srvs.data());
	}
}

nlohmann::json RenderModules::SerializeTextures() const
{
	nlohmann::json texturesJson = nlohmann::json::array();

	for (size_t i = 0; i < m_textures.size(); ++i) {
		nlohmann::json textureSlot;
		textureSlot["slot"] = i;

		if (m_textures[i]) {
			textureSlot["hasTexture"] = true;
			textureSlot["name"] = m_textures[i]->m_name;
		}
		else {
			textureSlot["hasTexture"] = false;
		}

		texturesJson.push_back(textureSlot);
	}

	return texturesJson;
}

void RenderModules::DeserializeTextures(const nlohmann::json& json)
{
	if (json.contains("textures") && json["textures"].is_array()) {
		ClearTextures();

		for (const auto& textureSlot : json["textures"]) {
			if (textureSlot.contains("slot") && textureSlot.contains("hasTexture")) {
				int slot = textureSlot["slot"];
				bool hasTexture = textureSlot["hasTexture"];

				if (hasTexture && textureSlot.contains("name")) {
					std::string textureName = textureSlot["name"];

					if (textureName.find('.') == std::string::npos) {
						textureName += ".png";
					}

					Texture* texture = DataSystems->LoadTexture(textureName);
					if (texture) {
						std::string nameWithoutExtension = file::path(textureName).stem().string();
						texture->m_name = nameWithoutExtension;
						SetTexture(slot, texture);
					}
				}
			}
		}
	}
}
