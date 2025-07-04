#ifndef DYNAMICCPP_EXPORTS
#include "ImGuiRenderer.h"
#include "CoreWindow.h"
#include "DeviceState.h"
#include "DataSystem.h"
#include "Profiler.h"
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

	io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;

	static const ImWchar icons_ranges[] = { ICON_MIN_FA, ICON_MAX_FA, 0 };
	ImFontConfig icons_config;
	ImFontConfig font_config;
	icons_config.MergeMode = true; // Merge icon font to the previous font if you want to have both icons and text
	io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\Verdana.ttf", 16.0f);
	io.Fonts->AddFontFromMemoryCompressedTTF(FA_compressed_data, FA_compressed_size, 16.0f, &icons_config, icons_ranges);
	io.Fonts->Build();
    // Setup Dear ImGui style
	ImGuiStyle* style = &ImGui::GetStyle();
#pragma region "ImGuiStyle"
	style->WindowPadding = ImVec2(5, 5);
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
	style->Colors[ImGuiCol_CheckMark] = ImVec4(0.80f, 0.80f, 0.83f, 0.31f);
	style->Colors[ImGuiCol_SliderGrab] = ImVec4(0.80f, 0.80f, 0.83f, 0.31f);
	style->Colors[ImGuiCol_SliderGrabActive] = ImVec4(0.06f, 0.05f, 0.07f, 1.00f);
	style->Colors[ImGuiCol_Button] = ImVec4(0.10f, 0.09f, 0.12f, 1.00f);
	style->Colors[ImGuiCol_ButtonHovered] = ImVec4(0.24f, 0.23f, 0.29f, 1.00f);
	style->Colors[ImGuiCol_ButtonActive] = ImVec4(0.56f, 0.56f, 0.58f, 1.00f);
	style->Colors[ImGuiCol_Header] = ImVec4(0.1569f, 0.1569f, 0.1569f, 1.00f);
	style->Colors[ImGuiCol_HeaderHovered] = ImVec4(0.56f, 0.56f, 0.58f, 1.00f);
	style->Colors[ImGuiCol_HeaderActive] = ImVec4(0.06f, 0.05f, 0.07f, 1.00f);
	style->Colors[ImGuiCol_ResizeGrip] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
	style->Colors[ImGuiCol_ResizeGripHovered] = ImVec4(0.56f, 0.56f, 0.58f, 1.00f);
	style->Colors[ImGuiCol_ResizeGripActive] = ImVec4(0.06f, 0.05f, 0.07f, 1.00f);
	style->Colors[ImGuiCol_PlotLines] = ImVec4(0.40f, 0.39f, 0.38f, 0.63f);
	style->Colors[ImGuiCol_PlotLinesHovered] = ImVec4(0.25f, 1.00f, 0.00f, 1.00f);
	style->Colors[ImGuiCol_PlotHistogram] = ImVec4(0.40f, 0.39f, 0.38f, 0.63f);
	style->Colors[ImGuiCol_PlotHistogramHovered] = ImVec4(0.25f, 0.00f, 0.00f, 1.00f);
	style->Colors[ImGuiCol_TextSelectedBg] = ImVec4(0.25f, 1.00f, 0.00f, 0.43f);
#pragma endregion
    // Setup Platform/Renderer backends
	ImGui_ImplWin32_Init(m_deviceResources->GetWindow()->GetHandle());
    ID3D11Device* device = m_deviceResources->GetD3DDevice();
    ID3D11DeviceContext* deviceContext = m_deviceResources->GetD3DDeviceContext();
    ImGui_ImplDX11_Init(device, deviceContext);

	//RegisterDisplaySizeHandler();
	//ImGui::LoadIniSettingsFromDisk(io.IniFilename);
}

ImGuiRenderer::~ImGuiRenderer()
{
    Shutdown();
}

void ImGuiRenderer::BeginRender()
{
	PROFILE_CPU_BEGIN("ImGuiBeginRender");
    static bool firstLoop = true;
	static bool forceResize = false;
	ImGuiIO& io = ImGui::GetIO();

	DirectX11::OMSetRenderTargets(1, &DeviceState::g_backBufferRTV, nullptr);
	
	RECT rect;
	HWND hWnd = m_deviceResources->GetWindow()->GetHandle();
	GetClientRect(hWnd, &rect);

	ImVec2 newSize = ImVec2((float)(rect.right - rect.left), (float)(rect.bottom - rect.top));
	
	EngineSettingInstance->SetWindowSize({ newSize.x, newSize.y });

	if (io.DisplaySize		!= newSize 
		&& newSize			!= ImVec2(0, 0)
		&& io.DisplaySize	!= ImVec2(0, 0))
	{
		forceResize = true;
		io.DisplaySize = newSize;
		io.DisplayFramebufferScale = ImVec2(1.0f, 1.0f);
	}
	
	ImGui_ImplDX11_NewFrame();
	ImGui_ImplWin32_NewFrame();
	io.WantCaptureKeyboard = io.WantCaptureMouse = io.WantTextInput = true;
	io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;         // Enable Docking
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.BackendFlags |= ImGuiBackendFlags_PlatformHasViewports | ImGuiBackendFlags_HasMouseCursors;

	ImGui::NewFrame();

	float menuBarSize = ImGui::GetFontSize() + ImGui::GetStyle().FramePadding.y * 2.0f;
	ImVec2 workCenter{ ImGui::GetMainViewport()->GetWorkCenter() };
	ImGuiID id = ImGui::GetID("MainWindowGroup");
	ImVec2 size{ ImGui::GetMainViewport()->Size.x, ImGui::GetMainViewport()->Size.y - (menuBarSize * 2.f) };
	ImVec2 nodePos{ workCenter.x - size.x * 0.5f, workCenter.y - size.y * 0.5f };

	ImGui::SetNextWindowPos(nodePos);
	ImGui::SetNextWindowSize(size);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));

	ImGui::Begin("Main DockSpace Window", nullptr,
		ImGuiWindowFlags_NoTitleBar |
		ImGuiWindowFlags_NoCollapse |
		ImGuiWindowFlags_NoResize |
		ImGuiWindowFlags_NoMove |
		ImGuiWindowFlags_NoBringToFrontOnFocus |
		ImGuiWindowFlags_NoNavFocus);


	ImGui::DockSpace(id, ImVec2(0, 0),
		ImGuiDockNodeFlags_PassthruCentralNode);

	ImGui::PopStyleVar(); // 반드시 Begin 이후에 Pop!

	ImGui::End();

	file::path iniPath = PathFinder::RelativeToExecutable("imgui.ini");
	if (!forceResize && file::exists(iniPath))
	{
		PROFILE_CPU_END();
		return;
	}

    if (firstLoop || forceResize)
	{
        ImGui::DockBuilderRemoveNode(id);
        ImGui::DockBuilderAddNode(id);

        // Set the size and position:
        ImGui::DockBuilderSetNodeSize(id, size);
        ImGui::DockBuilderSetNodePos(id, nodePos);

        ImGuiID dock1 = ImGui::DockBuilderSplitNode(id, ImGuiDir_Left, 0.5f, nullptr, &id);
		ImGuiID dock_gameView = ImGui::DockBuilderSplitNode(dock1, ImGuiDir_Down, 0.5f, nullptr, &dock1);
        ImGuiID dock2 = ImGui::DockBuilderSplitNode(id, ImGuiDir_Right, 0.5f, nullptr, &id);
        ImGuiID dock3 = ImGui::DockBuilderSplitNode(dock2, ImGuiDir_Right, 0.5f, nullptr, &dock2);

        ImGui::DockBuilderDockWindow(ICON_FA_USERS_VIEWFINDER "  Scene      ", dock1);
		ImGui::DockBuilderDockWindow("EffectModuleDetail", dock1);
        ImGui::DockBuilderDockWindow(ICON_FA_GAMEPAD "  Game        ", dock_gameView);
        ImGui::DockBuilderDockWindow(ICON_FA_BARS_STAGGERED "  Hierarchy", dock2);
        ImGui::DockBuilderDockWindow(ICON_FA_CIRCLE_INFO "  Inspector", dock3);
        ImGui::DockBuilderFinish(id);

		EngineSettingInstance->SetImGuiInitialized(true);
    }

    if (firstLoop) firstLoop = false;
	if (forceResize) forceResize = false;
	PROFILE_CPU_END();
}

void ImGuiRenderer::Render()
{
	PROFILE_CPU_BEGIN("ImGuiRender");
	static bool isOpened = false;

	auto& directoryQueue = DataSystems->m_LoadTextureAssetQueue;

	if(!directoryQueue.empty())
	{
		DataSystems->SelectTextureType();
	}

    auto& container = ImGuiRegister::GetInstance()->m_contexts;

    for (auto& [name, context] : container)
    {
        context.Render();
    }
	PROFILE_CPU_END();
}

void ImGuiRenderer::EndRender()
{
	PROFILE_CPU_BEGIN("ImGuiEndRender");
	ImGui::Render();
	ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

	ImGuiIO& io = ImGui::GetIO();

	if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
	{
		ImGui::UpdatePlatformWindows();
		ImGui::RenderPlatformWindowsDefault();
	}
	PROFILE_CPU_END();
}

void ImGuiRenderer::Shutdown()
{
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
}
#endif // !DYNAMICCPP_EXPORTS