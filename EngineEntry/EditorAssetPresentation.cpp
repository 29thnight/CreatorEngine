#include "EditorAssetPresentation.h"

#include "EditorAssetDatabase.h"
#include "EditorImGuiTexture.h"
#include "DataSystem.h"
#include "EnhancedGizmoSceneBinding.h"
#include "Render/Scene/EnhancedSceneRenderer.h"
#include "Material.h"
#include "PathFinder.h"
#include "Texture.h"
#include "ImGuiRegister.h"
#include "IconsFontAwesome6.h"
#include "fa.h"

#include <imgui.h>
#include <algorithm>
#include <cctype>
#include <utility>

namespace
{
	constexpr const char* kTextureImportSelector = "TextureType Selector";
	constexpr const char* kMaterialPicker = "SelectMaterial";

	constexpr size_t FileTypeIndex(EditorAssetPresentation::FileType type) noexcept
	{
		return static_cast<size_t>(type);
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

	ImGui::ContextRegister(kTextureImportSelector, true, [this]()
	{
		RenderTextureImportSelector();
	}, ImGuiWindowFlags_AlwaysAutoResize);
	ImGui::GetContext(kTextureImportSelector).Close();

	ImGui::ContextRegister(kMaterialPicker, true, [this]()
	{
		RenderMaterialPicker();
	}, ImGuiWindowFlags_NoScrollbar);
	ImGui::GetContext(kMaterialPicker).Close();
	m_initialized = true;
}

void EditorAssetPresentation::Shutdown() noexcept
{
	if (!m_initialized) return;

	ImGui::ContextUnregister(kMaterialPicker);
	ImGui::ContextUnregister(kTextureImportSelector);
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

	auto& context = ImGui::GetContext(kTextureImportSelector);
	if (!context.IsOpened()) context.Open();
}

void EditorAssetPresentation::OpenMaterialPicker()
{
	if (!m_initialized) return;
	m_previewedMaterial.reset();
	ImGui::GetContext(kMaterialPicker).Open();
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

	return { type, m_fileIcons[FileTypeIndex(type)].get() };
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
		ImGui::GetContext(kTextureImportSelector).Close();
	}
}

void EditorAssetPresentation::RenderMaterialPicker()
{
	static ImGuiTextFilter searchFilter;
	const float availableWidth = ImGui::GetContentRegionAvail().x;
	searchFilter.Draw(ICON_FA_MARKER "Search", availableWidth);

	const ImTextureID modelIcon = (ImTextureID)EditorImGuiTexture::From(
		m_fileIcons[FileTypeIndex(FileType::Model)].get());
	ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.1f, 0.1f, 0.1f, 1.0f));
	if (ImGui::BeginChild("MaterialTiles", ImVec2(0, 300),
		ImGuiChildFlags_AlwaysUseWindowPadding, 0))
	{
		constexpr float tileSize = 100.0f;
		int columns = static_cast<int>(ImGui::GetContentRegionAvail().x / tileSize);
		columns = (std::max)(columns, 1);

		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(10, 10));
		int count = 0;
		for (const auto& [name, material] : DataSystems->SnapshotMaterials())
		{
			if (!searchFilter.PassFilter(name.c_str())) continue;
			if (count % columns != 0) ImGui::SameLine();

			ImGui::BeginGroup();
			const char* displayName = name.empty() ? "None" : name.c_str();
			const bool clicked = ImGui::ImageButton(
				displayName, modelIcon, ImVec2(70, 70));
			const bool doubleClicked = ImGui::IsItemHovered() &&
				ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);
			if (clicked) m_previewedMaterial = material;

			ImGui::PushID(displayName);
			ImGui::Button(displayName, ImVec2(80, 30));
			ImGui::PopID();
			ImGui::EndGroup();

			if (doubleClicked)
			{
				m_previewedMaterial = material;
				m_selectedMaterial = material;
				ImGui::GetContext(kMaterialPicker).Close();
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
	const auto setFileIcon = [this, &loadIcon](FileType type, const wchar_t* filename)
	{
		m_fileIcons[FileTypeIndex(type)] = loadIcon(filename);
	};

	setFileIcon(FileType::Unknown, L"Unknown.png");
	setFileIcon(FileType::Model, L"Model.png");
	setFileIcon(FileType::Texture, L"Texture.png");
	setFileIcon(FileType::Shader, L"Shader.png");
	setFileIcon(FileType::CppScript, L"Code.png");
	setFileIcon(FileType::Prefab, L"Assets.png");
	// 같은 그림을 쓰는 분류는 Texture를 다시 읽지 않고 안정 신원을 공유한다.
	m_fileIcons[FileTypeIndex(FileType::MaterialTexture)] =
		m_fileIcons[FileTypeIndex(FileType::Texture)];
	m_fileIcons[FileTypeIndex(FileType::TerrainTexture)] =
		m_fileIcons[FileTypeIndex(FileType::Texture)];
	m_fileIcons[FileTypeIndex(FileType::HDR)] =
		m_fileIcons[FileTypeIndex(FileType::Texture)];
	m_fileIcons[FileTypeIndex(FileType::CSharpScript)] =
		m_fileIcons[FileTypeIndex(FileType::CppScript)];
	m_fileIcons[FileTypeIndex(FileType::VolumeProfile)] =
		m_fileIcons[FileTypeIndex(FileType::Prefab)];
	m_fileIcons[FileTypeIndex(FileType::Font)] =
		m_fileIcons[FileTypeIndex(FileType::Prefab)];
	m_fileIcons[FileTypeIndex(FileType::Sound)] =
		m_fileIcons[FileTypeIndex(FileType::Unknown)];

	m_extensionTypes = {
		{ ".fbx", FileType::Model }, { ".gltf", FileType::Model },
		{ ".obj", FileType::Model }, { ".glb", FileType::Model },
		{ ".png", FileType::Texture }, { ".dds", FileType::Texture },
		{ ".hdr", FileType::HDR }, { ".hlsl", FileType::Shader },
		{ ".shader", FileType::Shader }, { ".cpp", FileType::CppScript },
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

	m_smallFont = ImGui::GetIO().Fonts->AddFontFromFileTTF(
		"C:\\Windows\\Fonts\\Verdana.ttf", 12.0f);
	m_extraSmallFont = ImGui::GetIO().Fonts->AddFontFromFileTTF(
		"C:\\Windows\\Fonts\\Verdana.ttf", 10.0f);
}

void EditorAssetPresentation::ReleasePresentationResources() noexcept
{
	// 새 packet에서 먼저 끊는다. 이미 발행된 packet은 iconTextures shared_ptr을
	// 들고 있어 RenderThread가 소비를 끝낼 때까지 CPU 픽셀이 살아 있다.
	EnhancedSceneRenderer::SetGizmoIconTextures({});
	m_gizmoIconTextures.reset();
	m_extensionTypes.clear();
	for (auto& icon : m_fileIcons) icon.reset();
	m_smallFont = nullptr;
	m_extraSmallFont = nullptr;
}
