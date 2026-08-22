#include "EditorAssetPresentation.h"

#include "EditorAssetDatabase.h"
#include "ImGuiRegister.h"

#include <imgui.h>

namespace
{
	constexpr const char* kTextureImportSelector = "TextureType Selector";

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

	ImGui::ContextRegister(kTextureImportSelector, true, [this]()
	{
		RenderTextureImportSelector();
	}, ImGuiWindowFlags_AlwaysAutoResize);
	ImGui::GetContext(kTextureImportSelector).Close();
	m_initialized = true;
}

void EditorAssetPresentation::Shutdown() noexcept
{
	if (!m_initialized) return;

	ImGui::ContextUnregister(kTextureImportSelector);
	m_pendingTexturePaths.clear();
	file::path discarded;
	while (m_textureImportQueue.try_pop(discarded)) {}
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
