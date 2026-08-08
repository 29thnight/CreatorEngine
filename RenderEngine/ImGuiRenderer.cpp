#ifndef DYNAMICCPP_EXPORTS
#include "ImGuiRenderer.h"
#include "CoreWindow.h"
#include "RHI/DX12/ImGuiDx12Shell.h"
#include <imgui_impl_dx12.h>
#include "DataSystem.h"
#include "Profiler.h"
#include "EngineSetting.h"
#include "imgui_internal.h"
#include "IconsFontAwesome6.h"
#include "GlobalImGuiContext.h"
#include "fa.h"

static float g_LastAppliedScale = 0.8f;

namespace ImGuiBootstrap
{
	void ApplyStyle(ImGuiStyle* _style)
	{
		_style->WindowPadding = ImVec2(5, 5);
		_style->WindowRounding = 5.0f;
		_style->WindowBorderSize = 0.01f;
		_style->FramePadding = ImVec2(5, 5);
		_style->FrameRounding = 4.0f;
		_style->ItemSpacing = ImVec2(12, 8);
		_style->ItemInnerSpacing = ImVec2(8, 6);
		_style->IndentSpacing = 25.0f;
		_style->ScrollbarSize = 15.0f;
		_style->ScrollbarRounding = 9.0f;
		_style->GrabMinSize = 5.0f;
		_style->GrabRounding = 3.0f;

		_style->Colors[ImGuiCol_Text] = ImVec4(0.80f, 0.80f, 0.83f, 1.00f);
		_style->Colors[ImGuiCol_TextDisabled] = ImVec4(0.24f, 0.23f, 0.29f, 1.00f);
		_style->Colors[ImGuiCol_WindowBg] = ImVec4(0.2196f, 0.2196f, 0.2196f, 1.00f);
		_style->Colors[ImGuiCol_TabDimmedSelected] = ImVec4(0.2353f, 0.2353f, 0.2353f, 1.00f);
		_style->Colors[ImGuiCol_PopupBg] = ImVec4(0.07f, 0.07f, 0.09f, 1.00f);
		_style->Colors[ImGuiCol_Border] = ImVec4(0.30f, 0.30f, 0.33f, 0.88f);
		_style->Colors[ImGuiCol_BorderShadow] = ImVec4(0.92f, 0.91f, 0.88f, 0.00f);
		_style->Colors[ImGuiCol_FrameBg] = ImVec4(0.1569f, 0.1569f, 0.1569f, 1.00f);
		_style->Colors[ImGuiCol_FrameBgHovered] = ImVec4(0.24f, 0.23f, 0.29f, 1.00f);
		_style->Colors[ImGuiCol_FrameBgActive] = ImVec4(0.56f, 0.56f, 0.58f, 1.00f);
		_style->Colors[ImGuiCol_TitleBg] = ImVec4(0.10f, 0.09f, 0.12f, 1.00f);
		_style->Colors[ImGuiCol_TitleBgCollapsed] = ImVec4(1.00f, 0.98f, 0.95f, 0.75f);
		_style->Colors[ImGuiCol_TitleBgActive] = ImVec4(0.07f, 0.07f, 0.09f, 1.00f);
		_style->Colors[ImGuiCol_MenuBarBg] = ImVec4(0.10f, 0.09f, 0.12f, 1.00f);
		_style->Colors[ImGuiCol_ScrollbarBg] = ImVec4(0.10f, 0.09f, 0.12f, 1.00f);
		_style->Colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.80f, 0.80f, 0.83f, 0.31f);
		_style->Colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.56f, 0.56f, 0.58f, 1.00f);
		_style->Colors[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.06f, 0.05f, 0.07f, 1.00f);
		_style->Colors[ImGuiCol_CheckMark] = ImVec4(0.80f, 0.80f, 0.83f, 0.31f);
		_style->Colors[ImGuiCol_SliderGrab] = ImVec4(0.80f, 0.80f, 0.83f, 0.31f);
		_style->Colors[ImGuiCol_SliderGrabActive] = ImVec4(0.06f, 0.05f, 0.07f, 1.00f);
		_style->Colors[ImGuiCol_Button] = ImVec4(0.10f, 0.09f, 0.12f, 1.00f);
		_style->Colors[ImGuiCol_ButtonHovered] = ImVec4(0.24f, 0.23f, 0.29f, 1.00f);
		_style->Colors[ImGuiCol_ButtonActive] = ImVec4(0.56f, 0.56f, 0.58f, 1.00f);
		_style->Colors[ImGuiCol_Header] = ImVec4(0.1569f, 0.1569f, 0.1569f, 1.00f);
		_style->Colors[ImGuiCol_HeaderHovered] = ImVec4(0.56f, 0.56f, 0.58f, 1.00f);
		_style->Colors[ImGuiCol_HeaderActive] = ImVec4(0.06f, 0.05f, 0.07f, 1.00f);
		_style->Colors[ImGuiCol_ResizeGrip] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
		_style->Colors[ImGuiCol_ResizeGripHovered] = ImVec4(0.56f, 0.56f, 0.58f, 1.00f);
		_style->Colors[ImGuiCol_ResizeGripActive] = ImVec4(0.06f, 0.05f, 0.07f, 1.00f);
		_style->Colors[ImGuiCol_PlotLines] = ImVec4(0.40f, 0.39f, 0.38f, 0.63f);
		_style->Colors[ImGuiCol_PlotLinesHovered] = ImVec4(0.25f, 1.00f, 0.00f, 1.00f);
		_style->Colors[ImGuiCol_PlotHistogram] = ImVec4(0.40f, 0.39f, 0.38f, 0.63f);
		_style->Colors[ImGuiCol_PlotHistogramHovered] = ImVec4(0.25f, 0.00f, 0.00f, 1.00f);
		_style->Colors[ImGuiCol_TextSelectedBg] = ImVec4(0.25f, 1.00f, 0.00f, 0.43f);
	}
}

ImGuiRenderer::ImGuiRenderer(const std::shared_ptr<DirectX11::DeviceResources>& deviceResources) :
    m_deviceResources(deviceResources)
{
    //아래 렌더러	초기화 코드를 여기에 추가합니다.
    IMGUI_CHECKVERSION();
	GlobalImGuiContext::GetInstance()->SetContext(ImGui::CreateContext());
    ImGuiIO& io = ImGui::GetIO(); (void)io;

	ImGui::GetAllocatorFunctions(
		&GlobalImGuiContext::GetInstance()->p_alloc_func, 
		&GlobalImGuiContext::GetInstance()->p_free_func, 
		&GlobalImGuiContext::GetInstance()->p_user_data
	);

	io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;

	static const ImWchar icons_ranges[] = { ICON_MIN_FA, ICON_MAX_FA, 0 };
	ImFontConfig icons_config;
	ImFontConfig font_config;
	icons_config.MergeMode = true; // Merge icon font to the previous font if you want to have both icons and text
	io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\Verdana.ttf", 16.0f);
	io.Fonts->AddFontFromMemoryCompressedTTF(FA_compressed_data, FA_compressed_size, 16.0f, &icons_config, icons_ranges);
	io.Fonts->Build();
	g_LastAppliedScale = EngineSettingInstance->GetImGuiScale();
	io.FontGlobalScale = g_LastAppliedScale;
    // Setup Dear ImGui style
	ImGuiStyle* style = &ImGui::GetStyle();

	ImGuiBootstrap::ApplyStyle(style);
	style->ScaleAllSizes(g_LastAppliedScale);

    // Setup Platform/Renderer backends
	auto deviceResource = m_deviceResources.lock();
	HWND windowHandle = deviceResource->GetWindow()->GetHandle();
	ImGui_ImplWin32_Init(windowHandle);

	// ── 렌더 백엔드 분기(3-9 임시 승격) ──
	//
	// ImGui 출력은 DX12 셸(자체 스왑체인) 전용이다.
	//
	// ★ DX11 폴백을 걷었다 (D4, 2026-08-09). 셸 초기화가 실패하면 예전에는
	//   DX11 ImGui로 되돌아갔는데("빈 화면보다 낫다"), 그 폴백 하나 때문에
	//   DX11 디바이스·스왑체인·백버퍼 RTV를 끝까지 들고 있어야 했다. D4는
	//   그것을 걷어내는 작업이라 폴백과 양립하지 않는다.
	//
	//   T6(EditorImGuiTexture)과 D4 관문(라이브 뷰 표시)에서 같은 성격의
	//   폴백을 이미 걷었다. 셋 다 "셸이 없으면 그림이 없다"로 수렴하고,
	//   지금은 그것이 선택이 아니라 사실이다.
	//
	// 실패를 조용히 넘기지 않는다. 창이 검은 것과 초기화가 실패한 것이
	// 로그에서 구분되어야 한다.
	{
		RECT clientRect{};
		GetClientRect(windowHandle, &clientRect);
		const uint32_t clientWidth =
			static_cast<uint32_t>(clientRect.right - clientRect.left);
		const uint32_t clientHeight =
			static_cast<uint32_t>(clientRect.bottom - clientRect.top);

		std::string shellError;
		if (!ImGuiDx12Shell::Get().Initialize(
			windowHandle, clientWidth, clientHeight, shellError))
		{
			std::printf("[ImGui] DX12 셸 초기화 실패 - 에디터 UI가 표시되지 않는다: %s\n",
				shellError.c_str());
			Debug->LogError("[ImGui] DX12 셸 초기화 실패: " + shellError);
		}
	}

	//RegisterDisplaySizeHandler();
	//ImGui::LoadIniSettingsFromDisk(io.IniFilename);
}

ImGuiRenderer::~ImGuiRenderer()
{
    Shutdown();
	m_deviceResources.reset();
}

void ApplyImGuiScaleDynamically(float newScale, bool rebuildFonts = false)
{
	ImGuiIO& io = ImGui::GetIO();
	ImGuiStyle& style = ImGui::GetStyle();

	// 1) 스타일 사이즈를 '증분 비율'로 스케일 (누적 방지)
	//float ratio = (g_LastAppliedScale > 0.0f) ? (newScale / g_LastAppliedScale) : newScale;
	ImGuiBootstrap::ApplyStyle(&style);
	style.ScaleAllSizes(newScale);

	// 2) 렌더링 폰트 스케일 (즉시 적용/가벼움)
	io.FontGlobalScale = newScale;

	g_LastAppliedScale = newScale;

	// 3) 더 선명하게 만들고 싶으면 폰트 아틀라스 재빌드(옵션)
	if (rebuildFonts)
	{
		// 기존 폰트 정리 후 원하는 크기로 다시 추가
		io.Fonts->Clear();

		static const ImWchar icons_ranges[] = { ICON_MIN_FA, ICON_MAX_FA, 0 };
		ImFontConfig icons_config;
		ImFontConfig font_config;
		icons_config.MergeMode = true; // Merge icon font to the previous font if you want to have both icons and text
		io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\Verdana.ttf", 16.0f);
		io.Fonts->AddFontFromMemoryCompressedTTF(FA_compressed_data, FA_compressed_size, 16.0f, &icons_config, icons_ranges);
		io.Fonts->Build();

		ImGui_ImplDX12_InvalidateDeviceObjects();
		ImGui_ImplDX12_CreateDeviceObjects();
	}
}

void ImGuiRenderer::BeginRender()
{
	PROFILE_CPU_BEGIN("ImGuiBeginRender");
    static bool firstLoop = true;
	ImGuiIO& io = ImGui::GetIO();

	// 출력은 셸 스왑체인이다. 예전에는 셸이 꺼진 경우를 위해 DX11 백버퍼를
	// 걸어 두었는데, 그 경로가 D4에서 사라졌다.

	RECT rect;
	HWND hWnd = m_deviceResources.lock()->GetWindow()->GetHandle();
	GetClientRect(hWnd, &rect);

	ImVec2 newSize = ImVec2((float)(rect.right - rect.left), (float)(rect.bottom - rect.top));
	
	EngineSettingInstance->SetWindowSize({ newSize.x, newSize.y });

	if (io.DisplaySize		!= newSize 
		&& newSize			!= ImVec2(0, 0)
		&& io.DisplaySize	!= ImVec2(0, 0))
	{
		io.DisplaySize = newSize;
		io.DisplayFramebufferScale = ImVec2(1.0f, 1.0f);
	}

	//io.FontGlobalScale = EngineSettingInstance->GetImGuiScale();
	//ImGuiStyle* style = &ImGui::GetStyle();
	//style->ScaleAllSizes(EngineSettingInstance->GetImGuiScale());
	float targetScale = EngineSettingInstance->GetImGuiScale();
	static float lastRequested = -1.f;

	if (lastRequested != targetScale)
	{
		// 필요시 두 번째 인자를 true로 주면 폰트 재빌드(더 선명)
		ApplyImGuiScaleDynamically(targetScale, /*rebuildFonts=*/false);
		lastRequested = targetScale;
	}
	
	ImGuiDx12Shell::Get().Resize(static_cast<uint32_t>(newSize.x),
		static_cast<uint32_t>(newSize.y));
	ImGuiDx12Shell::Get().NewFrame();

	ImGui_ImplWin32_NewFrame();
	io.WantCaptureKeyboard = io.WantCaptureMouse = io.WantTextInput = true;
	io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;         // Enable Docking
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.BackendFlags |= ImGuiBackendFlags_PlatformHasViewports | ImGuiBackendFlags_HasMouseCursors;

	ImGui::NewFrame();
#ifndef BUILD_FLAG
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
	bool isExists = file::exists(iniPath);
    if (firstLoop && !isExists)
	{
		ImGuiID dock1;
		ImGuiID dock_gameView;
		ImGuiID dock2;
		ImGuiID dock3;
		ImGuiID dock4;

		switch (EngineSettingInstance->GetContentsBrowserStyle())
		{
		case ContentsBrowserStyle::Tree:
			ImGui::DockBuilderRemoveNode(id);
			ImGui::DockBuilderAddNode(id);

			// Set the size and position:
			ImGui::DockBuilderSetNodeSize(id, size);
			ImGui::DockBuilderSetNodePos(id, nodePos);

			dock1 = ImGui::DockBuilderSplitNode(id, ImGuiDir_Left, 0.5f, nullptr, &id);
			dock_gameView = ImGui::DockBuilderSplitNode(dock1, ImGuiDir_Down, 0.5f, nullptr, &dock1);
			dock2 = ImGui::DockBuilderSplitNode(id, ImGuiDir_Right, 0.33f, nullptr, &id);
			dock3 = ImGui::DockBuilderSplitNode(dock2, ImGuiDir_Right, 0.5f, nullptr, &dock2);
			dock4 = ImGui::DockBuilderSplitNode(dock3, ImGuiDir_Down, 0.5f, nullptr, &dock3);

			ImGui::DockBuilderDockWindow(ICON_FA_USERS_VIEWFINDER "  Scene      ", dock1);
			ImGui::DockBuilderDockWindow("Behavior Tree Editor", dock1);
			ImGui::DockBuilderDockWindow("BlackBoard Editor", dock1);
			ImGui::DockBuilderDockWindow(ICON_FA_GAMEPAD "  Game        ", dock_gameView);
			ImGui::DockBuilderDockWindow(ICON_FA_BARS_STAGGERED "  Hierarchy", dock2);
			ImGui::DockBuilderDockWindow(ICON_FA_DIAGRAM_PROJECT "  AssetBundle", dock3);
			ImGui::DockBuilderDockWindow(ICON_FA_HARD_DRIVE "  Content Browser", dock3);
			ImGui::DockBuilderDockWindow(ICON_FA_CIRCLE_INFO "  Inspector", dock4);
			ImGui::DockBuilderFinish(id);

			EngineSettingInstance->SetImGuiInitialized(true);
			break;
		case ContentsBrowserStyle::Tile:
			ImGui::DockBuilderRemoveNode(id);
			ImGui::DockBuilderAddNode(id);

			// Set the size and position:
			ImGui::DockBuilderSetNodeSize(id, size);
			ImGui::DockBuilderSetNodePos(id, nodePos);

			dock1 = ImGui::DockBuilderSplitNode(id, ImGuiDir_Left, 0.5f, nullptr, &id);
			dock_gameView = ImGui::DockBuilderSplitNode(dock1, ImGuiDir_Down, 0.5f, nullptr, &dock1);
			dock2 = ImGui::DockBuilderSplitNode(id, ImGuiDir_Right, 0.5f, nullptr, &id);
			dock3 = ImGui::DockBuilderSplitNode(dock2, ImGuiDir_Right, 0.5f, nullptr, &dock2);

			ImGui::DockBuilderDockWindow(ICON_FA_USERS_VIEWFINDER "  Scene      ", dock1);
			ImGui::DockBuilderDockWindow("Behavior Tree Editor", dock1);
			ImGui::DockBuilderDockWindow("BlackBoard Editor", dock1);
			ImGui::DockBuilderDockWindow(ICON_FA_GAMEPAD "  Game        ", dock_gameView);
			ImGui::DockBuilderDockWindow(ICON_FA_BARS_STAGGERED "  Hierarchy", dock2);
			ImGui::DockBuilderDockWindow(ICON_FA_CIRCLE_INFO "  Inspector", dock3);
			ImGui::DockBuilderFinish(id);

			EngineSettingInstance->SetImGuiInitialized(true);
			break;
		}
    }

    if (firstLoop) firstLoop = false;
	PROFILE_CPU_END();
#endif
}

void ImGuiRenderer::Render()
{
	PROFILE_CPU_BEGIN("ImGuiRender");
	static bool isOpened = false;

	auto& directoryQueue = DataSystems->m_LoadTextureAssetQueue;

#ifndef BUILD_FLAG
	if (!directoryQueue.empty())
	{
		DataSystems->SelectTextureType();
	}
#endif // !BUILD_FLAG
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
	std::string presentError;
	if (!ImGuiDx12Shell::Get().RenderAndPresent(presentError))
	{
		std::printf("[ImGui] DX12 셸 렌더 실패: %s\n", presentError.c_str());
	}

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
    ImGuiDx12Shell::Get().Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();

	m_deviceResources.reset();
}
#endif // !DYNAMICCPP_EXPORTS