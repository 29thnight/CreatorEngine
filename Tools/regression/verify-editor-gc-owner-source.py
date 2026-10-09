#!/usr/bin/env python3
"""Source-only contracts for editor GC owner dispatch; does not launch the engine.

This is a regression tripwire, not a substitute for an instrumented Windows
owner-thread/lifecycle run. Run explicitly when test execution is authorized.
"""
from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[2]


def source(path):
    return (ROOT / path).read_text(encoding="utf-8-sig")


class EditorGcOwnerContracts(unittest.TestCase):
    def test_command_owner_is_fixed_before_presentation_starts(self):
        header = source("Editor/EngineEntry/ConsoleCommandSystem.h")
        app = source("Editor/EngineEntry/App.cpp")
        self.assertIn("const std::thread::id m_gameThread", header)
        startup = app[app.index("void Core::App::Run()") :]
        self.assertLess(startup.index("(void)ConsoleCommandSystem::Get();"),
                        startup.index("m_main->Initialize();"))
        self.assertIn("cli.Pump([this] { return m_main->LockSceneStructure(); });", app)

    def test_structural_entry_points_dispatch_before_borrowing(self):
        operations = source("Editor/EngineEntry/EditorObjectOperations.cpp")
        for name in ("Create", "Delete", "Duplicate", "Parent", "MoveRelative",
                     "AddComponent", "AddManagedScript", "RemoveComponent", "UndoRedo"):
            with self.subTest(operation=name):
                self.assertRegex(
                    operations,
                    rf"CommandCore::CommandResult {name}\([^\n]*\)\s*\{{\s*"
                    r"if \(!ConsoleCommandSystem::Get\(\).IsGameThread\(\)\)",
                )
        self.assertIn("[parentHandle, name, type]", operations)
        self.assertIn("[guid, name, sceneId]", operations)

    def test_undo_replay_dispatches_without_changing_value_commit_semantics(self):
        undo = source("Editor/EngineEntry/ReflectionUndo.h")
        for name in ("Undo", "Redo"):
            self.assertRegex(undo, rf"void {name}\(\)\s*\{{\s*"
                             r"if \(!ConsoleCommandSystem::Get\(\).IsGameThread\(\)\)")
        execute = undo[undo.index("void Execute(") : undo.index("void Undo()")]
        self.assertNotIn("EnqueueEditorMutation", execute)

    def test_hierarchy_queues_creation_and_configuration_as_one_operation(self):
        hierarchy = source("Editor/EngineGUIWindow/HierarchyWindow.cpp")
        header = source("Editor/EngineGUIWindow/HierarchyWindow.h")
        self.assertIn("std::vector<EntityHandle> m_clipboard", header)
        light = hierarchy[hierarchy.index("const auto create_light") :
                          hierarchy.index('if (ImGui::BeginMenu("Light"))')]
        self.assertLess(light.index("queue_hierarchy_edit("),
                        light.index("EditorObjectOperations::Create("))
        self.assertIn('creation.data.Find("index")', light)
        self.assertIn("const auto root = scene ? scene->HandleOf(0)", hierarchy)
        self.assertNotIn("std::span<Entity* const>(m_clipboard", hierarchy)

    def test_prefab_lifetime_is_managed_on_owner(self):
        prefab = source("Editor/EngineEntry/PrefabEditor.cpp")
        self.assertEqual(prefab.count("!ConsoleCommandSystem::Get().IsGameThread()"), 2)
        self.assertIn("SceneManagers->AdoptScene(", prefab)
        self.assertIn("Scene::CreateNewScene(SceneManagers->ManagedDomain(),", prefab)
        self.assertIn("SceneManagers->RetireScene(editScene)", prefab)
        self.assertNotIn("delete m_editScene", prefab)

    def test_owned_gpu_commands_have_explicit_unfenced_contract(self):
        registrar = source("Editor/EngineEntry/Commands/CommandRegistrar.h")
        commands = source("Editor/EngineEntry/ConsoleCommandSystem.cpp")
        diagnostics = source("Editor/EngineEntry/Commands/DiagnosticsCommands.cpp")
        self.assertIn("SceneAccess access = SceneAccess::BorrowLiveState", registrar)
        self.assertIn("it->second.sceneAccess == ConsoleCmd::SceneAccess::BorrowLiveState", commands)
        self.assertIn("&Cmd_pix_capture, SceneAccess::OwnedState", diagnostics)
        self.assertIn("&Cmd_profile_deep_stop, SceneAccess::OwnedState", diagnostics)

    def test_persistent_ui_observers_are_identity_values(self):
        scene_view = source("Editor/EngineGUIWindow/SceneViewWindow.cpp")
        inspector = source("Editor/EngineGUIWindow/InspectorWindow.cpp")
        inspector_header = source("Editor/EngineGUIWindow/InspectorWindow.h")
        self.assertNotIn("static TerrainComponent*", scene_view)
        self.assertIn("static EntityHandle previousTerrainOwner", scene_view)
        self.assertIn("previousTerrainComponent", scene_view)
        self.assertNotIn("std::unordered_map<Entity*", scene_view)
        self.assertNotIn("static Component* selectedComponent", inspector)
        self.assertIn("static EntityHandle selectedComponentOwner", inspector)
        self.assertIn("EntityHandle m_clipPickerOwner", inspector_header)
        self.assertNotIn("SoundComponent* m_clipPickerTarget", inspector_header)

    def test_queue_is_nonblocking_and_shutdown_closes_admission(self):
        commands = source("Editor/EngineEntry/ConsoleCommandSystem.cpp")
        queue = commands[commands.index("ConsoleCommandSystem::EnqueueEditorMutation(") :
                         commands.index("void ConsoleCommandSystem::Pump(")]
        self.assertIn('result.code = "editor.queued"', queue)
        self.assertIn("!m_acceptEditorMutations", queue)
        self.assertNotRegex(queue, r"\.(?:wait|get)\(")
        self.assertIn("mutations.swap(m_editorMutations)", commands)
        self.assertIn("m_waitNeedsSceneBorrow ? acquireSceneBorrow()", commands)
        self.assertIn("SceneManagers->SceneContextEpoch() != contextEpoch", commands)
        self.assertIn("m_acceptEditorMutations = false;", commands)


if __name__ == "__main__":
    unittest.main()
