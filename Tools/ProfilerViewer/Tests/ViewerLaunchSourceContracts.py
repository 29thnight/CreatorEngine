"""UNEXECUTED source guards, not build or runtime verification.

Run explicitly with Python 3 when test execution is authorized. These narrow
guards prevent the old layout-to-process launch bridge from being reintroduced;
ViewerLaunchChecklist.md covers the Windows behavior they cannot establish.
"""
from pathlib import Path
import re
import unittest


ROOT = Path(__file__).resolve().parents[3]


def source(path):
    return (ROOT / path).read_text(encoding="utf-8-sig")


class ViewerLaunchSourceContracts(unittest.TestCase):
    def test_startup_and_pump_require_a_request(self):
        launcher = source("Editor/ImGuiHelper/ProfilerLauncher.cpp")
        self.assertIn("requested_page{ 0 }", launcher)
        initializer = launcher.split("void initialize_profiler_viewer()", 1)[1].split(
            "void pump_profiler_viewer()", 1)[0]
        self.assertNotIn("open_or_focus", initializer)
        self.assertNotIn("request_profiler_viewer(", initializer)
        self.assertIn("else if (request && !process.open_or_focus(", launcher)

    def test_restored_legacy_panel_cannot_launch(self):
        menu = source("Editor/EngineGUIWindow/MenuBarWindow.cpp")
        self.assertNotIn("ShowProfilerWindow", menu)
        self.assertNotRegex(menu, r"bind_window_body\(\s*EditorWindowName::kFrameProfiler")
        declaration = source("Editor/EditorWindow/Windows/EditorToolboxWindows.h").split(
            "panel<&windows::draw_frame_profiler>", 1)[1].split(
            ".available(&windows::has_frame_profiler)", 1)[0]
        self.assertIn(".open_by_default(false)", declaration)
        self.assertIn(".persist_open(false)", declaration)
        workspace = source("Editor/EngineGUIWindow/EditorWorkspaceStore.cpp")
        for begin, end in (("void EditorWorkspaceStore::Apply(", "void EditorWorkspaceStore::ApplyPreset("),
                           ("void EditorWorkspaceStore::ApplyPreset(", "void EditorWorkspaceStore::RefreshNames(")):
            section = workspace.split(begin, 1)[1].split(end, 1)[0]
            self.assertIn("if(!entry.persist_open) continue;", section)

    def test_rendering_preset_has_no_profiler_open_override(self):
        presets = source("Editor/EditorWindow/EditorLayoutPreset.cpp")
        self.assertNotIn("EditorWindowName::kFrameProfiler", presets)

    def test_retained_alias_is_explicitly_classified_by_window_audit(self):
        audit = source("Editor/EditorWindow/EditorWindowAudit.cpp")
        alias = audit.split("bool is_external_window_alias(", 1)[1].split("bool contains(", 1)[0]
        self.assertIn("return stable_id == EditorWindowName::kFrameProfiler;", alias)
        self.assertIn("!is_external_window_alias(entry.stable_id)", audit)

    def test_only_explicit_launch_call_sites_remain(self):
        expected = {
            "Editor/EngineGUIWindow/MenuBarWindow.cpp": 2,  # Window menu, Trace
            "Editor/EngineGUIWindow/EnhancedRenderDebugWindow.cpp": 1,  # explicit Rendering - Live
            "Editor/EngineEntry/Commands/CoreCommands.cpp": 1,  # CLI open/focus
            "Editor/ImGuiHelper/ProfilerLauncher.cpp": 1,  # request function definition
        }
        actual = {}
        for path in (ROOT / "Editor").rglob("*.cpp"):
            count = len(re.findall(r"\brequest_profiler_viewer\s*\(", path.read_text(encoding="utf-8-sig")))
            if count:
                actual[path.relative_to(ROOT).as_posix()] = count
        self.assertEqual(expected, actual)
        menu = source("Editor/EngineGUIWindow/MenuBarWindow.cpp")
        trace = menu.split('if (draw_status_tab("##StatusTrace"', 1)[1].split("ImGui::SameLine", 1)[0]
        self.assertIn("editor::request_profiler_viewer();", trace)
        self.assertIn("profiler ? editor::profiler_viewer_running()", menu)
        self.assertIn("editor::request_profiler_viewer_close();", menu)
        cli = source("Editor/EngineEntry/Commands/CoreCommands.cpp").split(
            "static CommandCore::CommandResult Cmd_editor_window(", 1)[1]
        self.assertIn('if ("open" == args[2])', cli)
        self.assertIn('else if ("focus" == args[2])', cli)
        self.assertIn("::editor::request_profiler_viewer();", cli)
        self.assertIn("::editor::request_profiler_viewer_close();", cli)


if __name__ == "__main__":
    unittest.main()
