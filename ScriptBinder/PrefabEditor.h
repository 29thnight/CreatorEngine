#pragma once
#ifndef DYNAMICCPP_EXPORTS
#include "Core.Minimal.h"
#include "Prefab.h"
#include "PrefabUtility.h"
#include "SceneManager.h"
#include "ImGuiRegister.h"
#include <filesystem>

class PrefabEditor : public Singleton<PrefabEditor>
{
private:
    friend class Singleton;
    PrefabEditor();
    ~PrefabEditor() = default;

public:
    void Open(const std::string& path);
    void Close(bool apply = true);
    bool IsOpened() const { return m_isOpened; }

private:
    bool m_isOpened{ false };
    Scene* m_prevScene{ nullptr };
    size_t m_prevSceneIndex{ 0 };
    Scene* m_editScene{ nullptr };
    Prefab* m_prefab{ nullptr };
    std::filesystem::path m_path{};
};

static auto& PrefabEditors = PrefabEditor::GetInstance();
#endif // !DYNAMICCPP_EXPORTS
