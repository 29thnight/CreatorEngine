#include "RenderDebugManager.h"
#include <imgui.h>

void RenderDebugManager::Capture(ID3D11DeviceContext* deferredContext, ID3D11Texture2D* src,
    std::uint32_t vertexCount,
    std::source_location location)
{
    if (!deferredContext || !src || !m_isRecording.load())
        return;

    D3D11_TEXTURE2D_DESC desc{};
    src->GetDesc(&desc);

    Microsoft::WRL::ComPtr<ID3D11Texture2D> copyTex;
    if (FAILED(DeviceState::g_pDevice->CreateTexture2D(&desc, nullptr, copyTex.GetAddressOf())))
        return;

    DirectX11::CopyResource(deferredContext, copyTex.Get(), src);

    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> srv;
    if (FAILED(DeviceState::g_pDevice->CreateShaderResourceView(copyTex.Get(), nullptr, srv.GetAddressOf())))
        return;

    m_capturedCallsByFrame[m_currentIndex].push_back(CapturedCall{
        std::move(copyTex),
        std::move(srv),
        location.function_name(),
        vertexCount
        });
}

void RenderDebugManager::CaptureRenderPass(ID3D11DeviceContext* deferredContext, ID3D11Texture2D* src, const std::string& passName)
{
    if (!deferredContext || !src)
		return;

    D3D11_TEXTURE2D_DESC desc{};
    src->GetDesc(&desc);

	Microsoft::WRL::ComPtr<ID3D11Texture2D> copyTex;
    if (FAILED(DeviceState::g_pDevice->CreateTexture2D(&desc, nullptr, copyTex.GetAddressOf())))
        return;
    DirectX11::CopyResource(deferredContext, copyTex.Get(), src);
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> srv;
    if (FAILED(DeviceState::g_pDevice->CreateShaderResourceView(copyTex.Get(), nullptr, srv.GetAddressOf())))
        return;

    size_t currentIndex = m_currentIndex.load(std::memory_order_relaxed) % 3;

    m_capturedCallsResultRenderPass[passName][currentIndex] = CapturedCall{
        std::move(copyTex),
        std::move(srv),
        passName,
		0 // Vertex count is not applicable for render pass capture
	};
}

void RenderDebugManager::CaptureRenderPass(
    ID3D11DeviceContext* deferredContext,
    ID3D11RenderTargetView* rtv,
    const std::string& passName)
{
    if (!deferredContext || !rtv)
        return;

    // RTV → Texture2D 얻기
    Microsoft::WRL::ComPtr<ID3D11Resource> rtvRes;
    rtv->GetResource(&rtvRes);
    if (!rtvRes) return;

    Microsoft::WRL::ComPtr<ID3D11Texture2D> srcTex;
    if (FAILED(rtvRes.As(&srcTex)) || !srcTex)
        return;

    // 원본 텍스처 desc
    D3D11_TEXTURE2D_DESC desc{};
    srcTex->GetDesc(&desc);

    // SRV 생성 가능하도록 보장
    desc.BindFlags |= D3D11_BIND_SHADER_RESOURCE;

    // 동일한 크기/포맷으로 캡처용 텍스처 생성
    Microsoft::WRL::ComPtr<ID3D11Texture2D> copyTex;
    if (FAILED(DeviceState::g_pDevice->CreateTexture2D(&desc, nullptr, copyTex.GetAddressOf())))
        return;

    // GPU copy (크기/포맷 동일해야 함)
    deferredContext->CopyResource(copyTex.Get(), srcTex.Get());

    // RTV 포맷을 사용해 SRV 생성 (typeless 대비)
    D3D11_RENDER_TARGET_VIEW_DESC rtvDesc{};
    rtv->GetDesc(&rtvDesc);

    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
    srvDesc.Format = rtvDesc.Format;

    if (desc.SampleDesc.Count > 1)
    {
        // 멀티샘플 텍스처 SRV
        srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DMS;
    }
    else
    {
        // 싱글샘플 텍스처 SRV
        srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Texture2D.MostDetailedMip = 0;
        srvDesc.Texture2D.MipLevels = 1;
    }

    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> srv;
    if (FAILED(DeviceState::g_pDevice->CreateShaderResourceView(copyTex.Get(), &srvDesc, srv.GetAddressOf())))
        return;

    size_t currentIndex = m_currentIndex.load(std::memory_order_relaxed) % 3;

    m_capturedCallsResultRenderPass[passName][currentIndex] = CapturedCall{
        std::move(copyTex),
        std::move(srv),
        passName,
        0 // Render pass 캡처에서는 vertex count 미사용
    };
}

void RenderDebugManager::RenderCaptureCallImGui()
{
	static size_t currentFrame = 0;

    if (ImGui::Begin("Render Debugger"))
    {
        if (ImGui::Button("Start Recording"))
        {
            StartRecording();
		}
        ImGui::SameLine();
        if (ImGui::Button("Clear"))
        {
            Clear();
        }
		ImGui::SameLine();
        if (ImGui::Button("Erase Current Frame"))
        {
            EraseFrame(currentFrame);
            currentFrame = 0; // Reset to the first frame after erasing
		}

        if (ImGui::BeginCombo("Frame", std::to_string(currentFrame).c_str()))
        {
            for (const auto& [index, calls] : m_capturedCallsByFrame)
            {
                if (ImGui::Selectable(std::to_string(index).c_str(), index == currentFrame))
                {
                    currentFrame = index;
                }
            }
			ImGui::EndCombo();
        }

        if (m_capturedCallsByFrame.find(currentFrame) != m_capturedCallsByFrame.end())
        {
            const auto& calls = m_capturedCallsByFrame[currentFrame];
            for (const auto& call : calls)
            {
                ImGui::PushID(&call);
                ImGui::Text("Call: %s", call.functionName.c_str());
                ImGui::Text("Vertices: %u", call.vertexCount);
                ImGui::Image((ImTextureID)call.srv.Get(), ImVec2{ 200.f, 200.f });
                ImGui::PopID();
            }
		}
    }
    ImGui::End();
}

void RenderDebugManager::RenderCaptureResultImGui()
{
    if (ImGui::Button("Clear"))
    {
        m_capturedCallsResultRenderPass.clear();
    }
    for (const auto& [passName, call] : m_capturedCallsResultRenderPass)
    {
        ImGui::PushID(&call);
        if (ImGui::CollapsingHeader(passName.c_str(), ImGuiTreeNodeFlags_DefaultOpen))
        {
            size_t prevIndex = (m_currentIndex.load(std::memory_order_relaxed) + 1) % 3;
            ImGui::Image((ImTextureID)call[prevIndex].srv.Get(), ImVec2{ 350.f, 350.f });
        }
        ImGui::PopID();
    }
}

void RenderDebugManager::Clear()
{
    m_capturedCallsByFrame.clear();
	m_capturedCallsResultRenderPass.clear();
    m_currentIndex = 0;
}