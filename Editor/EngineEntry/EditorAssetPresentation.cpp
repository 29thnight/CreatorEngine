#include "EditorAssetPresentation.h"
#include "EditorWindowRegistry.h"
#include "Windows/EditorStandardWindows.h"

#include "EditorAssetDatabase.h"
#include "EditorImGuiTexture.h"
#include "DataSystem.h"
#include "EnhancedGizmoSceneBinding.h"
#include "Render/Scene/EnhancedSceneRenderer.h"
#include "Material.h"
#include "PathFinder.h"
#include "Texture.h"
#include "ImGui.h"
#include "EditorIcons.h"
#include "EditorFontResources.h"
#include "EditorTheme.h"
#include "EditorWindowNames.h"

#include <imgui.h>
#include <algorithm>
#include <cctype>
#include <utility>

namespace
{
	// W3 이 안정 식별자를 `###Editor.*` 로 옮기면서 여기 두 줄이 남았다. 값이
	// 옛 표시 이름이라 `bind_window_body` 는 고아 본문 둘을, 선언 표는 본문
	// 없는 창 둘을 들고 있었고 `open_window`/`close_window` 는 아무 창도 찾지
	// 못한 채 조용히 돌았다. 이름은 한 곳에서만 든다.
	constexpr const char* kTextureImportSelector = EditorWindowName::kTextureImportSelector;
	constexpr const char* kMaterialPicker = EditorWindowName::kMaterialPicker;

    const char* TypeIcon(EditorAssetPresentation::FileType type) noexcept
    {
        using Type = EditorAssetPresentation::FileType;
        switch (type)
        {
        case Type::Model: return EditorIcon::Model;
        case Type::Texture: return EditorIcon::Texture;
        case Type::MaterialTexture: return EditorIcon::Material;
        case Type::TerrainTexture: return EditorIcon::Terrain;
        case Type::Shader: return EditorIcon::Shader;
        case Type::CppScript:
        case Type::CSharpScript: return EditorIcon::Script;
        case Type::Prefab: return EditorIcon::Prefab;
        case Type::Sound: return EditorIcon::Audio;
        case Type::HDR: return EditorIcon::HDR;
        case Type::VolumeProfile: return EditorIcon::Volume;
        case Type::Font: return EditorIcon::Font;
        default: return EditorIcon::Unknown;
        }
    }

	EditorAssetDatabase::ImportKind ToImportKind(int selected) noexcept
	{
		switch (selected)
		{
		case 1:
			return EditorAssetDatabase::ImportKind::MaterialTexture;
		case 2:
			return EditorAssetDatabase::ImportKind::TerrainTexture;
		case 3:
			return EditorAssetDatabase::ImportKind::HDR;
		default:
			return EditorAssetDatabase::ImportKind::Texture;
		}
	}
}

EditorAssetPresentation& EditorAssetPresentation::Get() noexcept
{
	static EditorAssetPresentation instance;
	return instance;
}

void EditorAssetPresentation::Initialize()
{
	if (m_initialized) return;
	LoadPresentationResources();

	// PHASE 21 M4 2단계: 프레임은 셸이 연다. 처음 닫혀 있다는 사실도
	// 선언이 든다(open_by_default(false)) — 여기서 다시 닫지 않는다.
	m_textureImportBody = editor::windows::bind_window_body(
		kTextureImportSelector, [this]() { RenderTextureImportSelector(); });

	m_materialPickerBody = editor::windows::bind_window_body(
		kMaterialPicker, [this]() { RenderMaterialPicker(); });
	m_initialized = true;
}

void EditorAssetPresentation::Shutdown() noexcept
{
	if (!m_initialized) return;

	// `Shutdown` 뒤에 `Initialize` 가 다시 올 수 있는 싱글톤이라 객체는
	// 남고 본문만 내린다. 핸들을 비우는 것이 그 일이다(PHASE 21 W3).
	m_materialPickerBody.reset();
	m_textureImportBody.reset();
	m_pendingTexturePaths.clear();
	file::path discarded;
	while (m_textureImportQueue.try_pop(discarded)) {}
	m_previewedMaterial.reset();
	m_selectedMaterial.reset();
	ReleasePresentationResources();
	m_initialized = false;
}

void EditorAssetPresentation::QueueTextureImport(const file::path& source)
{
	if (!source.empty()) m_textureImportQueue.push(source);
}

void EditorAssetPresentation::OpenPendingTextureImportSelector()
{
	if (!m_initialized ||
		(m_textureImportQueue.empty() && m_pendingTexturePaths.empty())) return;

	if (!editor::is_window_open(kTextureImportSelector))
		editor::open_window(kTextureImportSelector);
}

void EditorAssetPresentation::OpenMaterialPicker()
{
	if (!m_initialized) return;
	m_previewedMaterial.reset();
	editor::open_window(kMaterialPicker);
}

std::shared_ptr<Material> EditorAssetPresentation::TakeSelectedMaterial() noexcept
{
	return std::exchange(m_selectedMaterial, {});
}

EditorAssetPresentation::FilePresentation
EditorAssetPresentation::ResolveFilePresentation(std::string_view extension) const
{
	FileType type = FileType::Unknown;
	std::string normalizedExtension(extension);
	std::ranges::transform(normalizedExtension, normalizedExtension.begin(),
		[](unsigned char value) { return static_cast<char>(std::tolower(value)); });
	if (const auto it = m_extensionTypes.find(normalizedExtension);
		it != m_extensionTypes.end())
	{
		type = it->second;
	}

	return { type, TypeIcon(type), GetFileIcon(type) };
}

void EditorAssetPresentation::RenderTextureImportSelector()
{
	file::path queuedPath;
	while (m_textureImportQueue.try_pop(queuedPath))
	{
		if (!queuedPath.empty()) m_pendingTexturePaths.push_back(std::move(queuedPath));
	}

	for (const file::path& path : m_pendingTexturePaths)
		ImGui::Text("Selected Texture: %s", path.filename().string().c_str());

	constexpr const char* textureTypeNames[]{
		"Texture",
		"Material Texture",
		"Terrain Texture",
		"HDR",
	};

	if (ImGui::BeginCombo("Texture Type", textureTypeNames[m_selectedTextureKind]))
	{
		for (int index = 0; index < IM_ARRAYSIZE(textureTypeNames); ++index)
		{
			const bool isSelected = m_selectedTextureKind == index;
			if (ImGui::Selectable(textureTypeNames[index], isSelected))
				m_selectedTextureKind = index;
			if (isSelected) ImGui::SetItemDefaultFocus();
		}
		ImGui::EndCombo();
	}

	if (ImGui::Button("Select"))
	{
		const EditorAssetDatabase::ImportKind kind = ToImportKind(m_selectedTextureKind);
		for (const file::path& source : m_pendingTexturePaths)
			EditorAssetDatabase::Get().ImportSourceAsset(source, kind);

		m_pendingTexturePaths.clear();
		editor::close_window(kTextureImportSelector);
	}
}

void EditorAssetPresentation::RenderMaterialPicker()
{
	static ImGuiTextFilter searchFilter;
	const float availableWidth = ImGui::GetContentRegionAvail().x;
	searchFilter.Draw(EditorIcon::Label<EditorIcon::Search, " Search">, availableWidth);

	ImGui::PushStyleColor(ImGuiCol_ChildBg, ::editor::ThemeColorValue(::editor::ThemeColor::Canvas));
	if (ImGui::BeginChild("MaterialTiles", ImVec2(0, 300),
		ImGuiChildFlags_AlwaysUseWindowPadding, 0))
	{
		const float tileSize = editor::ThemePixels(100.0f);
		int columns = static_cast<int>(ImGui::GetContentRegionAvail().x / tileSize);
		columns = (std::max)(columns, 1);

		const float assetGap = ::editor::ThemePixels(::editor::EditorThemeTokens::ItemGapX);
		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(assetGap, assetGap));
		int count = 0;
		for (const auto& [name, material] : DataSystems->SnapshotMaterials())
		{
			if (!searchFilter.PassFilter(name.c_str())) continue;
			if (count % columns != 0) ImGui::SameLine();

			ImGui::BeginGroup();
			const char* displayName = name.empty() ? "None" : name.c_str();
            ImGui::PushID(displayName);
            const bool clicked = ImGui::Button(EditorIcon::Material,
                ImVec2(editor::ThemePixels(70.0f), editor::ThemePixels(70.0f)));
            ImGui::PopID();
			const bool doubleClicked = ImGui::IsItemHovered() &&
				ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);
			if (clicked) m_previewedMaterial = material;

			ImGui::PushID(displayName);
			ImGui::Button(displayName, ImVec2(editor::ThemePixels(80.0f), editor::ThemePixels(30.0f)));
			ImGui::PopID();
			ImGui::EndGroup();

			if (doubleClicked)
			{
				m_previewedMaterial = material;
				m_selectedMaterial = material;
				editor::close_window(kMaterialPicker);
			}
			++count;
		}
		ImGui::PopStyleVar();
	}
	ImGui::EndChild();

	ImGui::BeginChild("MaterialSelection", ImVec2(0, 50), false);
	if (m_previewedMaterial)
	{
		ImGui::TextUnformatted(m_previewedMaterial->m_name.c_str());
		ImGui::TextUnformatted(m_previewedMaterial->m_fileGuid.ToString().c_str());
	}
	else
	{
		ImGui::TextUnformatted("No Material Selected");
	}
	ImGui::EndChild();
	ImGui::PopStyleColor();
}

void EditorAssetPresentation::LoadPresentationResources()
{
	const file::path iconPath = PathFinder::IconPath();
	const auto loadIcon = [&iconPath](const wchar_t* filename)
	{
		return Texture::LoadSharedFromPath(iconPath / filename);
	};

	m_extensionTypes = {
		{ ".fbx", FileType::Model }, { ".gltf", FileType::Model },
		{ ".obj", FileType::Model }, { ".glb", FileType::Model },
		{ ".png", FileType::Texture }, { ".dds", FileType::Texture },
		{ ".jpg", FileType::Texture }, { ".jpeg", FileType::Texture },
		{ ".mat", FileType::MaterialTexture }, { ".fx", FileType::Shader },
		{ ".hdr", FileType::HDR }, { ".hlsl", FileType::Shader },
		{ ".slang", FileType::Shader },
		{ ".shadermeta", FileType::Shader }, { ".shader", FileType::Shader },
		{ ".cpp", FileType::CppScript }, { ".h", FileType::CppScript },
		{ ".cs", FileType::CSharpScript }, { ".wav", FileType::Sound },
		{ ".mp3", FileType::Sound }, { ".terrain", FileType::TerrainTexture },
		{ ".prefab", FileType::Prefab }, { ".volume", FileType::VolumeProfile },
		{ ".spritefont", FileType::Font },
	};

	auto gizmoIcons = std::make_shared<EnhancedGizmoIconTextures>();
	gizmoIcons->camera = loadIcon(L"CameraGizmo.png");
	gizmoIcons->mainLight = loadIcon(L"MainLightGizmo.png");
	gizmoIcons->directionalLight = loadIcon(L"DirectionalLightGizmo.png");
	gizmoIcons->pointLight = loadIcon(L"PointLightGizmo.png");
	gizmoIcons->spotLight = loadIcon(L"SpotLightGizmo.png");
	m_gizmoIconTextures = std::move(gizmoIcons);
	EnhancedSceneRenderer::SetGizmoIconTextures(m_gizmoIconTextures);

    // Fixed editor artwork, independent of the planned asynchronous asset previews.
    m_directoryIcons[0] = loadIcon(L"DirectoryClosed.png");
    m_directoryIcons[1] = loadIcon(L"DirectoryOpen.png");
    m_engineIcon = loadIcon(L"Engine.png");
    for (size_t i = 0; i < editor::EntityIconPresets.size(); ++i)
        m_entityIcons[i] = loadIcon(editor::EntityIconPresets[i].filename);
    m_projectIcon = loadIcon(L"Assets.png");
    // FileType order; one cached texture per type, never loaded during tile drawing.
    constexpr std::array fileIconNames{
        L"Unknown.png", L"Model.png", L"Texture.png", L"Material.png", L"Terrain.png",
        L"Shader.png", L"Code.png", L"Code.png", L"EntityPrefab.png", L"Audio.png",
        L"HDR.png", L"VolumeProfile.png", L"Font.png"
    };
    static_assert(fileIconNames.size() == static_cast<size_t>(FileType::End));
    for (size_t i = 0; i < fileIconNames.size(); ++i)
        m_fileIcons[i] = loadIcon(fileIconNames[i]);

	// 작은 글씨용 둘은 **선택**이다(PHASE 21 W1). 비어도 쓰는 자리가
	// `PushFont(nullptr, 0.0f)` 으로 받으므로 글자 크기만 기본으로 간다.
	m_smallFont = ::editor::fonts::add_optional_font(
		"small", ::editor::fonts::body_candidates(), ::editor::EditorThemeTokens::SmallFontSize).font;
	m_extraSmallFont = ::editor::fonts::add_optional_font(
		"extra_small", ::editor::fonts::body_candidates(), ::editor::EditorThemeTokens::ExtraSmallFontSize).font;
}

void EditorAssetPresentation::ReleasePresentationResources() noexcept
{
	// 새 packet에서 먼저 끊는다. 이미 발행된 packet은 iconTextures shared_ptr을
	// 들고 있어 RenderThread가 소비를 끝낼 때까지 CPU 픽셀이 살아 있다.
	EnhancedSceneRenderer::SetGizmoIconTextures({});
	m_gizmoIconTextures.reset();
    for (auto& icon : m_directoryIcons) icon.reset();
    m_engineIcon.reset();
    for (auto& icon : m_entityIcons) icon.reset();
    m_projectIcon.reset();
    for (auto& icon : m_fileIcons) icon.reset();
	m_extensionTypes.clear();
	m_smallFont = nullptr;
	m_extraSmallFont = nullptr;
}
