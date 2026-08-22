#pragma once
// 프리팹 편집 모드 — Editor 소유 (E3-4에서 ScriptBinder에서 옮겨 왔다).
//
// 임시 씬을 만들어 프리팹 하나를 그 안에 인스턴스화하고, 닫을 때 그 결과를 다시
// 프리팹으로 저장한다. 저작 행위라 런타임에는 존재할 이유가 없다 — Player는 이제
// 이 파일을 컴파일조차 하지 않는다(Academy_4Q.vcxproj 전용).
//
// ⚠ Core에서 이것을 쓰려 하지 말 것. Core(ScriptBinder)는 Editor를 물지 않는다.
//   프리팹 자산을 만지는 런타임 경로는 `PrefabUtility`가 맡는다 —
//   `SceneManager`와 `Prefab.cpp`가 그쪽을 쓰는 것이 그 이유다.
//
// 옛 파일에는 `#ifndef DYNAMICCPP_EXPORTS` 가드가 본문 전체를 감싸고 있었는데,
// C++ 핫리로드(Dynamic_CPP DLL)가 은퇴하면서 그 매크로를 정의하는 곳이 저장소에
// 하나도 남지 않았다 — 솔루션에 그 프로젝트 자체가 없다. 죽은 가드는 "이 헤더는
// 조건부로만 컴파일된다"는 오해를 낳으므로(실제로 `SceneManager.cpp`의 주석이 그렇게
// 적혀 있었다) 옮기면서 걷었다. 이제 층이 그 역할을 한다.
#include "Core.Minimal.h"
#include "Prefab.h"
#include "PrefabUtility.h"
#include "SceneManager.h"
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
    Prefab* m_prefab{ nullptr }; // 비소유 — PrefabUtility(캐시/m_createdPrefabs)가 소유한다.
    std::filesystem::path m_path{};
};

static auto PrefabEditors = PrefabEditor::GetInstance();
