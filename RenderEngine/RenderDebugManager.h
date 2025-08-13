#pragma once
#include "DLLAcrossSingleton.h"
#include "DeviceState.h"
#include <wrl/client.h>
#include <vector>
#include <string>
#include <source_location>
#include <cstdint>
#include <array>

class RenderDebugManager : public DLLCore::Singleton<RenderDebugManager>
{
    friend class DLLCore::Singleton<RenderDebugManager>;

    struct CapturedCall
    {
        Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> srv;
        std::string functionName;
        std::uint32_t vertexCount{};
    };

    RenderDebugManager() = default;

public:
	void AddFrame() { m_currentIndex++; }
	void EndFrame() { m_isRecording = false; }
	void StartRecording() { m_isRecording = true; }
	bool IsRecording() const { return m_isRecording.load(); }
	// Capture a draw call with the current state of the deferred context
    void Capture(ID3D11DeviceContext* deferredContext, ID3D11Texture2D* src,
        std::uint32_t vertexCount,
        std::source_location location = std::source_location::current());
	// Capture a render pass result with
    void CaptureRenderPass(ID3D11DeviceContext* deferredContext,
        ID3D11Texture2D* src,
        const std::string& passName);

    void CaptureRenderPass(ID3D11DeviceContext* deferredContext, ID3D11RenderTargetView* rtv, const std::string& passName);

    void RenderCaptureCallImGui();
	void RenderCaptureResultImGui();
    void EraseFrame(std::size_t frameIndex) { m_capturedCallsByFrame.erase(frameIndex); }
    void Clear();

private:
	std::map<size_t, std::vector<CapturedCall>> m_capturedCallsByFrame{};
	std::map<std::string, std::array<CapturedCall, 3>> m_capturedCallsResultRenderPass{};
    std::atomic<std::size_t> m_currentIndex{};
    std::atomic<bool> m_isRecording{ false };
};