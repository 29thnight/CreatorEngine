#include "ImGuiRenderer.h"
#include "CoreWindow.h"
#include "DeviceState.h"
#include "imgui_internal.h"
#include "IconsFontAwesome6.h"
#include "fa.h"

ImGuiRenderer::ImGuiRenderer(const std::shared_ptr<DirectX11::DeviceResources>& deviceResources) :
    m_deviceResources(deviceResources)
{
    //아래 렌더러	초기화 코드를 여기에 추가합니다.
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    //io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\malgun.ttf", 15.0f, nullptr, io.Fonts->GetGlyphRangesKorean());
	
	static const ImWchar icons_ranges[] = { ICON_MIN_FA, ICON_MAX_FA, 0 };
	ImFontConfig icons_config;
	ImFontConfig font_config;
	icons_config.MergeMode = true; // Merge icon font to the previous font if you want to have both icons and text
	io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\Verdana.ttf", 16.0f);
	io.Fonts->AddFontFromMemoryCompressedTTF(FA_compressed_data, FA_compressed_size, 16.0f, &icons_config, icons_ranges);

	//If you want change between icons size you will need to create a new font
	//io.Fonts->AddFontFromMemoryCompressedTTF(FA_compressed_data, FA_compressed_size, 12.0f, &icons_config, icons_ranges);
	//io.Fonts->AddFontFromMemoryCompressedTTF(FA_compressed_data, FA_compressed_size, 20.0f, &icons_config, icons_ranges);

	//To use brands icons you need do the same steps but using the brands header
	//If a quotation mark is displayed instead of the icon, probably the Icon header and Font Awesome version are not the same

	io.Fonts->Build();
    // Setup Dear ImGui style
    //ImGui::StyleColorsDark();
    //ImGui::StyleColorsLight();
	ImGuiStyle* style = &ImGui::GetStyle();

	style->WindowPadding = ImVec2(15, 15);
	style->WindowRounding = 5.0f;
	style->WindowBorderSize = 0.01f;
	style->FramePadding = ImVec2(5, 5);
	style->FrameRounding = 4.0f;
	style->ItemSpacing = ImVec2(12, 8);
	style->ItemInnerSpacing = ImVec2(8, 6);
	style->IndentSpacing = 25.0f;
	style->ScrollbarSize = 15.0f;
	style->ScrollbarRounding = 9.0f;
	style->GrabMinSize = 5.0f;
	style->GrabRounding = 3.0f;

	style->Colors[ImGuiCol_Text] = ImVec4(0.80f, 0.80f, 0.83f, 1.00f);
	style->Colors[ImGuiCol_TextDisabled] = ImVec4(0.24f, 0.23f, 0.29f, 1.00f);
	style->Colors[ImGuiCol_WindowBg] = ImVec4(0.2196f, 0.2196f, 0.2196f, 1.00f);
	style->Colors[ImGuiCol_TabDimmedSelected] = ImVec4(0.2353f, 0.2353f, 0.2353f, 1.00f);
	style->Colors[ImGuiCol_PopupBg] = ImVec4(0.07f, 0.07f, 0.09f, 1.00f);
	style->Colors[ImGuiCol_Border] = ImVec4(0.30f, 0.30f, 0.33f, 0.88f);
	style->Colors[ImGuiCol_BorderShadow] = ImVec4(0.92f, 0.91f, 0.88f, 0.00f);
	style->Colors[ImGuiCol_FrameBg] = ImVec4(0.1569f, 0.1569f, 0.1569f, 1.00f);
	style->Colors[ImGuiCol_FrameBgHovered] = ImVec4(0.24f, 0.23f, 0.29f, 1.00f);
	style->Colors[ImGuiCol_FrameBgActive] = ImVec4(0.56f, 0.56f, 0.58f, 1.00f);
	style->Colors[ImGuiCol_TitleBg] = ImVec4(0.10f, 0.09f, 0.12f, 1.00f);
	style->Colors[ImGuiCol_TitleBgCollapsed] = ImVec4(1.00f, 0.98f, 0.95f, 0.75f);
	style->Colors[ImGuiCol_TitleBgActive] = ImVec4(0.07f, 0.07f, 0.09f, 1.00f);
	style->Colors[ImGuiCol_MenuBarBg] = ImVec4(0.10f, 0.09f, 0.12f, 1.00f);
	style->Colors[ImGuiCol_ScrollbarBg] = ImVec4(0.10f, 0.09f, 0.12f, 1.00f);
	style->Colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.80f, 0.80f, 0.83f, 0.31f);
	style->Colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.56f, 0.56f, 0.58f, 1.00f);
	style->Colors[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.06f, 0.05f, 0.07f, 1.00f);
	//style->Colors[ImGuiCol_ComboBg] = ImVec4(0.19f, 0.18f, 0.21f, 1.00f);
	style->Colors[ImGuiCol_CheckMark] = ImVec4(0.80f, 0.80f, 0.83f, 0.31f);
	style->Colors[ImGuiCol_SliderGrab] = ImVec4(0.80f, 0.80f, 0.83f, 0.31f);
	style->Colors[ImGuiCol_SliderGrabActive] = ImVec4(0.06f, 0.05f, 0.07f, 1.00f);
	style->Colors[ImGuiCol_Button] = ImVec4(0.10f, 0.09f, 0.12f, 1.00f);
	style->Colors[ImGuiCol_ButtonHovered] = ImVec4(0.24f, 0.23f, 0.29f, 1.00f);
	style->Colors[ImGuiCol_ButtonActive] = ImVec4(0.56f, 0.56f, 0.58f, 1.00f);
	style->Colors[ImGuiCol_Header] = ImVec4(0.1569f, 0.1569f, 0.1569f, 1.00f);
	style->Colors[ImGuiCol_HeaderHovered] = ImVec4(0.56f, 0.56f, 0.58f, 1.00f);
	style->Colors[ImGuiCol_HeaderActive] = ImVec4(0.06f, 0.05f, 0.07f, 1.00f);
	//style->Colors[ImGuiCol_Column] = ImVec4(0.56f, 0.56f, 0.58f, 1.00f);
	//style->Colors[ImGuiCol_ColumnHovered] = ImVec4(0.24f, 0.23f, 0.29f, 1.00f);
	//style->Colors[ImGuiCol_ColumnActive] = ImVec4(0.56f, 0.56f, 0.58f, 1.00f);
	style->Colors[ImGuiCol_ResizeGrip] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
	style->Colors[ImGuiCol_ResizeGripHovered] = ImVec4(0.56f, 0.56f, 0.58f, 1.00f);
	style->Colors[ImGuiCol_ResizeGripActive] = ImVec4(0.06f, 0.05f, 0.07f, 1.00f);
	style->Colors[ImGuiCol_PlotLines] = ImVec4(0.40f, 0.39f, 0.38f, 0.63f);
	style->Colors[ImGuiCol_PlotLinesHovered] = ImVec4(0.25f, 1.00f, 0.00f, 1.00f);
	style->Colors[ImGuiCol_PlotHistogram] = ImVec4(0.40f, 0.39f, 0.38f, 0.63f);
	style->Colors[ImGuiCol_PlotHistogramHovered] = ImVec4(0.25f, 0.00f, 0.00f, 1.00f);
	style->Colors[ImGuiCol_TextSelectedBg] = ImVec4(0.25f, 1.00f, 0.00f, 0.43f);
	//style->Colors[ImGuiCol_ModalWindowDarkening] = ImVec4(1.00f, 0.98f, 0.95f, 0.73f);

    // Setup Platform/Renderer backends
	ImGui_ImplWin32_Init(m_deviceResources->GetWindow()->GetHandle());
	//ImGui_ImplWin32_EnableDpiAwareness();
	//ImGui::GetMainViewport()->Pos = ImVec2(0.0f, 0.0f);
    ID3D11Device* device = m_deviceResources->GetD3DDevice();
    ID3D11DeviceContext* deviceContext = m_deviceResources->GetD3DDeviceContext();
    ImGui_ImplDX11_Init(device, deviceContext);
}

ImGuiRenderer::~ImGuiRenderer()
{
    Shutdown();
}

void ImGuiRenderer::BeginRender()
{
	DirectX11::OMSetRenderTargets(1, &DeviceState::g_backBufferRTV, nullptr);
	ImGui_ImplDX11_NewFrame();
	ImGui_ImplWin32_NewFrame();
	ImGuiIO& io = ImGui::GetIO(); (void)io;
	io.WantCaptureKeyboard = io.WantCaptureMouse = io.WantTextInput = true;
	io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;         // Enable Docking
	//io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.BackendFlags |= ImGuiBackendFlags_PlatformHasViewports | ImGuiBackendFlags_HasMouseCursors;

	ImGui::NewFrame();

    static bool firstLoop = true;
    if (firstLoop) 
	{
        ImVec2 workCenter{ ImGui::GetMainViewport()->GetWorkCenter() };
        ImGuiID id = ImGui::GetID("MainWindowGroup");
        ImGui::DockBuilderRemoveNode(id);
        ImGui::DockBuilderAddNode(id);

        ImVec2 size{ ImGui::GetMainViewport()->Size.x, ImGui::GetMainViewport()->Size.y - 50.f };
        ImVec2 nodePos{ workCenter.x - size.x * 0.5f, workCenter.y - size.y * 0.5f };

        // Set the size and position:
        ImGui::DockBuilderSetNodeSize(id, size);
        ImGui::DockBuilderSetNodePos(id, nodePos);

        ImGuiID dock1 = ImGui::DockBuilderSplitNode(id, ImGuiDir_Left, 0.5f, nullptr, &id);
		ImGuiID dock_gameView = ImGui::DockBuilderSplitNode(dock1, ImGuiDir_Down, 0.5f, nullptr, &dock1);
        ImGuiID dock2 = ImGui::DockBuilderSplitNode(id, ImGuiDir_Right, 0.5f, nullptr, &id);
        ImGuiID dock3 = ImGui::DockBuilderSplitNode(dock2, ImGuiDir_Right, 0.5f, nullptr, &dock2);

        ImGui::DockBuilderDockWindow(ICON_FA_USERS_VIEWFINDER "  Scene      ", dock1);
        ImGui::DockBuilderDockWindow(ICON_FA_GAMEPAD "  Game        ", dock_gameView);
        ImGui::DockBuilderDockWindow("GameObject Hierarchy", dock2);
        ImGui::DockBuilderDockWindow("GameObject Inspector", dock3);
        ImGui::DockBuilderFinish(id);
    }

    if (firstLoop) firstLoop = false;
}

void ImGuiRenderer::Render()
{
    auto& container = ImGuiRegister::GetInstance()->m_contexts;

    for (auto& [name, context] : container)
    {
        context.Render();
    }
}

void ImGuiRenderer::EndRender()
{
	ImGui::Render();
	ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

	ImGuiIO& io = ImGui::GetIO();

	if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
	{
		ImGui::UpdatePlatformWindows();
		ImGui::RenderPlatformWindowsDefault();
	}

}

void ImGuiRenderer::Shutdown()
{
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
}
