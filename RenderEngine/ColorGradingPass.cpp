#include "ColorGradingPass.h"
#include "ShaderSystem.h"
#include "../EngineEntry/RenderPassSettings.h"
#include "TimeSystem.h"

struct alignas(16) CBData {
	float lerpValue;
	float time;
	bool32 useLUTEdit;
};

struct alignas(16) ColorGradingEdit {
	float4 shadows;
	float4 midtones;
	float4 highlights;
	float exposure;
	float contrast;
	float saturation;
	float temperature;
	float tint;
};
std::wstring UTF8ToWString(const std::string& str)
{
	if (str.empty()) return std::wstring();
	int size_needed = MultiByteToWideChar(CP_UTF8, 0, &str[0], (int)str.size(), NULL, 0);
	std::wstring wstrTo(size_needed, 0);
	MultiByteToWideChar(CP_UTF8, 0, &str[0], (int)str.size(), &wstrTo[0], size_needed);
	return wstrTo;
}
ColorGradingPass::ColorGradingPass()
{
	m_pso = std::make_unique<PipelineStateObject>();

	m_pColorGradingLUTShader = &ShaderSystem->ComputeShaders["GenerateColorGradingLUT"];
	m_pso->m_vertexShader = &ShaderSystem->VertexShaders["Fullscreen"];
	m_pso->m_pixelShader = &ShaderSystem->PixelShaders["ColorGrading"];
	m_pso->m_primitiveTopology = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP;

	InputLayOutContainer vertexLayoutDesc = {
		{ "POSITION",     0, DXGI_FORMAT_R32G32B32_FLOAT,    0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
		{ "NORMAL",       0, DXGI_FORMAT_R32G32B32_FLOAT,    0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
		{ "TEXCOORD",     0, DXGI_FORMAT_R32G32_FLOAT,       0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
		{ "TEXCOORD",     1, DXGI_FORMAT_R32G32_FLOAT,       0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
		{ "TANGENT",      0, DXGI_FORMAT_R32G32B32_FLOAT,    0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
		{ "BINORMAL",     0, DXGI_FORMAT_R32G32B32_FLOAT,    0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
		{ "BLENDINDICES", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
		{ "BLENDWEIGHT",  0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
	};
	m_pso->CreateInputLayout(std::move(vertexLayoutDesc));

	auto linearSampler = std::make_shared<Sampler>(D3D11_FILTER_MIN_MAG_MIP_LINEAR, D3D11_TEXTURE_ADDRESS_CLAMP);
	auto pointSampler = std::make_shared<Sampler>(D3D11_FILTER_MIN_MAG_MIP_POINT, D3D11_TEXTURE_ADDRESS_CLAMP);
	m_pso->m_samplers.push_back(linearSampler);
	m_pso->m_samplers.push_back(pointSampler);
	m_Buffer = DirectX11::CreateBuffer(sizeof(CBData), D3D11_BIND_CONSTANT_BUFFER, nullptr);
	m_EditBuffer = DirectX11::CreateBuffer(sizeof(ColorGradingEdit), D3D11_BIND_CONSTANT_BUFFER, nullptr);

	m_pCopiedTexture = Texture::Create(
		DeviceState::g_ClientRect.width,
		DeviceState::g_ClientRect.height,
		"CopiedTexture",
		DXGI_FORMAT_R16G16B16A16_FLOAT,
		D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET
	);
	m_pCopiedTexture->CreateRTV(DXGI_FORMAT_R16G16B16A16_FLOAT);
	m_pCopiedTexture->CreateSRV(DXGI_FORMAT_R16G16B16A16_FLOAT);

	m_pColorGradingLUTTexture = Texture::Create(
		64,
		64,
		"TempColorGradingLUTTexture",
		DXGI_FORMAT_R8G8B8A8_UNORM,
		D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS
	);
	m_pColorGradingLUTTexture->CreateSRV(DXGI_FORMAT_R8G8B8A8_UNORM);
	m_pColorGradingLUTTexture->CreateUAV(DXGI_FORMAT_R8G8B8A8_UNORM);

	m_pDefaultLUTTexture = Texture::Create(
		64,
		64,
		"DefaultColorGradingLUTTexture",
		DXGI_FORMAT_R8G8B8A8_UNORM,
		D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS
	);
	m_pDefaultLUTTexture->CreateSRV(DXGI_FORMAT_R8G8B8A8_UNORM);
	m_pDefaultLUTTexture->CreateUAV(DXGI_FORMAT_R8G8B8A8_UNORM);
	DirectX11::CSSetShader(m_pColorGradingLUTShader->GetShader(), nullptr, 0);
	ColorGradingEdit editData;
	editData.exposure = 1;
	editData.contrast = 1;
	editData.saturation = 1;
	editData.temperature = 0;
	editData.tint = 0;
	editData.shadows = { 1,1,1,0 };
	editData.midtones = { 1,1,1,1 };
	editData.highlights = { 1,1,1,1 };
	DirectX11::UpdateBuffer(m_EditBuffer.Get(), &editData);
	DirectX11::CSSetConstantBuffer(0, 1, m_EditBuffer.GetAddressOf());
	DirectX11::CSSetUnorderedAccessViews(0, 1, &m_pDefaultLUTTexture->m_pUAV, nullptr);
	DirectX11::Dispatch(64 / 8, 64 / 8, 1);

	ID3D11UnorderedAccessView* nullUAV = nullptr;
	DirectX11::CSSetShader(nullptr, nullptr, 0);
	DirectX11::CSSetUnorderedAccessViews(0, 1, &nullUAV, nullptr);

}

ColorGradingPass::~ColorGradingPass()
{
}

void ColorGradingPass::Initialize()
{

	/*if (m_pColorGradingTexture)
	{
		m_pColorGradingTexture.release();
	}

	file::path path = file::path(fileName);
	if (file::exists(path))
	{
		m_pColorGradingTexture = Texture::LoadManagedFromPath(fileName);
	}*/
}

void ColorGradingPass::Execute(RenderScene& scene, Camera& camera)
{
	if (!isOn) return;
	if (!RenderPassData::VaildCheck(&camera)) return;
	auto renderData = RenderPassData::GetData(&camera);

	m_pso->Apply();
	ID3D11RenderTargetView* view = renderData->m_renderTarget->GetRTV();
	DirectX11::OMSetRenderTargets(1, &view, nullptr);

	timer += DirectX11::TimeSystem::TimeSysInstance->GetElapsedSeconds();
	camera.UpdateBuffer();
	CBData cbData;
	cbData.lerpValue = lerp;
	cbData.time = timer * 0.2f;
	cbData.useLUTEdit = useLUTEdit;
	DirectX11::UpdateBuffer(m_Buffer.Get(), &cbData);
	DirectX11::PSSetConstantBuffer(0, 1, m_Buffer.GetAddressOf());

	ColorGradingEdit editData;
	editData.exposure = exposure;
	editData.contrast = contrast;
	editData.saturation = saturation;
	editData.temperature = temperature;
	editData.tint = tint;
	editData.shadows = shadows;
	editData.midtones = midtones;
	editData.highlights = highlights;
	DirectX11::UpdateBuffer(m_EditBuffer.Get(), &editData);
	DirectX11::PSSetConstantBuffer(1, 1, m_EditBuffer.GetAddressOf());

	DirectX11::CopyResource(m_pCopiedTexture->m_pTexture, renderData->m_renderTarget->m_pTexture);

	DirectX11::PSSetShaderResources(0, 1, &m_pCopiedTexture->m_pSRV);
	DirectX11::PSSetShaderResources(1, 1, m_pColorGradingTexture ? &m_pColorGradingTexture->m_pSRV : &m_pDefaultLUTTexture->m_pSRV);

	DirectX11::Draw(4, 0);

	ID3D11ShaderResourceView* nullSRV = nullptr;
	DirectX11::PSSetShaderResources(0, 1, &nullSRV);
}

void ColorGradingPass::ControlPanel()
{
    ImGui::PushID(this);
    auto& setting = EngineSettingInstance->GetRenderPassSettingsRW().colorGrading;
    if (ImGui::Checkbox("ColorGrading", &isOn))
    {
            setting.isOn = isOn;
    }
    if (ImGui::SliderFloat("Lerp", &lerp, 0.0f, 1.0f))
    {
            setting.lerp = lerp;
    }
	if(ImGui::Button("Timer Zero"))
	timer = 0.0f;

	ImGui::Checkbox("Use LUT Edit", &useLUTEdit);
	if (useLUTEdit)
	{
		ImGui::SliderFloat("Exposure", &exposure, -2.0f, 2.0f);
		ImGui::SliderFloat("Contrast", &contrast, 0.0f, 2.0f);
		ImGui::SliderFloat("Saturation", &saturation, 0.0f, 2.0f);
		ImGui::SliderFloat("Temperature", &temperature, -10.0f, 10.0f);
		ImGui::SliderFloat("Tint", &tint, -10.0f, 10.0f);
		ImGui::ColorEdit3("Shadows", (float*)&shadows);
		ImGui::SliderFloat("Shadows Brightness Offset", &shadows.w, -1.0f, 1.0f);
		ImGui::ColorEdit3("Midtones", (float*)&midtones);
		ImGui::SliderFloat("Midtones Brightness Power", &midtones.w, 0.0f, 2.0f);
		ImGui::ColorEdit3("Highlights", (float*)&highlights);
		ImGui::SliderFloat("Highlights Brightness Scale", &highlights.w, 0.0f, 2.0f);

		ImGui::InputText("##Save LUT Name", &m_tempLUTName[0], sizeof(m_tempLUTName.capacity()));
		// 항상 false로 init
		if (ImGui::Button("Save LUT")) {
			// 해당 설정값으로 LUT 제작.
			DirectX11::CSSetShader(m_pColorGradingLUTShader->GetShader(), nullptr, 0);
			DirectX11::CSSetConstantBuffer(0, 1, m_EditBuffer.GetAddressOf());
			DirectX11::CSSetUnorderedAccessViews(0, 1, &m_pColorGradingLUTTexture->m_pUAV, nullptr);
			DirectX11::Dispatch(64 / 8, 64 / 8, 1);

			ID3D11UnorderedAccessView* nullUAV = nullptr;
			DirectX11::CSSetShader(nullptr, nullptr, 0);
			DirectX11::CSSetUnorderedAccessViews(0, 1, &nullUAV, nullptr);

			DirectX::ScratchImage image;
			HRESULT hr = DirectX::CaptureTexture(DeviceState::g_pDevice, DeviceState::g_pDeviceContext, m_pColorGradingLUTTexture->m_pTexture, image);

			std::wstring path = PathFinder::Relative("ColorGrading\\");
			if (!file::exists(path)) {
				file::create_directories(path);
			}
			path += UTF8ToWString(m_tempLUTName);
			path += L".png";
			DirectX::SaveToWICFile(*image.GetImage(0, 0, 0), DirectX::WIC_FLAGS_NONE, GUID_ContainerFormatPng, path.data());
		}
	}
    if (ImGui::Button("Reset")) {
            lerp = 0.0f;
            timer = 0.0f;
            setting.lerp = lerp;
    }

	ImGui::Button(m_textureFilePath.ToString().c_str());
	ImVec2 minRect = ImGui::GetItemRectMin();
	ImVec2 maxRect = ImGui::GetItemRectMax();
	ImRect bb(minRect, maxRect);

	if(ImGui::BeginDragDropTargetCustom(bb, ImGui::GetID("MyDropTarget"))) {
		if(const ImGuiPayload* colorGradingPayload = ImGui::AcceptDragDropPayload("Texture"))
		{
			const char* droppedFilePath = (const char*)colorGradingPayload->Data;
			file::path filename = droppedFilePath;
			file::path filepath = PathFinder::Relative("ColorGrading\\") / filename.filename();
			if (!filename.filename().empty())
				SetColorGradingTexture(filepath.string());
			else {
				Debug->Log("Empty Color Grading File Name");
			}
		}
		ImGui::EndDragDropTarget();
	}


	ImGui::PopID();
}

void ColorGradingPass::ApplySettings(const ColorGradingPassSetting& setting)
{
    isOn = setting.isOn;
    lerp = setting.lerp;
	m_textureFilePath = setting.textureFilePath;
	SetColorGradingTexture(m_textureFilePath.ToString());
}

void ColorGradingPass::SetColorGradingTexture(std::string_view filename)
{
	m_textureFilePath = filename;
	if (m_pColorGradingTexture)
	{
		m_pColorGradingTexture.release();
	}
	if (!m_textureFilePath.ToString().empty())
	{
		m_pColorGradingTexture = Texture::LoadManagedFromPath(m_textureFilePath.ToString());
	}
}

