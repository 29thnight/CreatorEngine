"""Source-only Player/Editor presentation boundary check; never builds or launches code.

Project conditions are inspected conservatively as a union (Debug/Release and
Shipping/non-Shipping). MSBuild's evaluated dependency gate remains authoritative
at build time. Passing this scan does not prove compilation or zero link symbols.
"""
from pathlib import Path
import re
import sys
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[2]
ERRORS = []


def require(condition, message):
    if not condition:
        ERRORS.append(message)


def relative(path):
    return path.relative_to(ROOT).as_posix()


def forbidden_path(value):
    parts = value.replace("$(SolutionDir)", "/").replace("\\", "/").lower().split("/")
    return "editor" in parts or any("hostimgui" in part or "imguihelper" in part for part in parts)


def read(path):
    return path.read_text(encoding="utf-8-sig")


def project_items(path):
    return list(ET.parse(path).getroot().iter())


def tag(element):
    return element.tag.rsplit("}", 1)[-1]


def resolved_item(project, value):
    value = value.replace("$(SolutionDir)", str(ROOT) + "/").replace("\\", "/")
    return (project.parent / value).resolve()


def inspect_runtime_graph():
    pending = [ROOT / "Player/Player.vcxproj"]
    visited = set()
    while pending:
        project = pending.pop()
        if project in visited:
            continue
        visited.add(project)
        require(project.is_file(), f"Missing runtime project: {project}")
        if not project.is_file():
            continue
        require(not forbidden_path(relative(project)), f"Player graph reaches Editor: {relative(project)}")
        for element in project_items(project):
            kind = tag(element)
            value = element.attrib.get("Include", "")
            if kind in ("ProjectReference", "ReflgenReference", "ClCompile", "ClInclude") and value:
                require(not forbidden_path(value), f"{relative(project)}: forbidden {kind}: {value}")
            if kind in ("AdditionalIncludeDirectories", "IncludePath", "ExternalIncludePath"):
                require(not forbidden_path(element.text or ""),
                        f"{relative(project)}: Editor include directory in {kind}")
            if kind == "ProjectReference" and value:
                pending.append(resolved_item(project, value))
    return visited


def inspect_sources():
    # Preserve include operands while stripping comments. This is a source guard,
    # not a C++ preprocessor; deliberate macro/absolute-path evasion is not covered.
    comment = re.compile(r'/\*.*?\*/|//[^\n]*', re.S)
    includes = re.compile(r'^\s*#\s*include\s*[<"]([^">]+)[">]', re.M)
    for folder in (ROOT / "Player", ROOT / "Engine"):
        for path in folder.rglob("*"):
            if path.suffix not in (".h", ".hpp", ".cpp", ".inl") or "ForbiddenIncludes" in path.parts:
                continue
            body = comment.sub(" ", path.read_bytes().decode("utf-8-sig", errors="surrogateescape"))
            for include in includes.findall(body):
                require("imgui" not in include.lower() and not forbidden_path(include),
                        f"{relative(path)}: forbidden include {include}")
            require(not re.search(r'\b(?:ImGui\s*::|GetImGuiHost\s*\(|ImGui_Impl\w*\s*\()', body),
                    f"{relative(path)}: ImGui implementation reference")


def inspect_host():
    host = ROOT / "Editor/HostImGuiPresentation"
    project = host / "HostImGuiPresentation.vcxproj"
    items = [e.attrib.get("Include", "") for e in project_items(project)
             if tag(e) in ("ClCompile", "ClInclude")]
    require(any("ImGuiDx12Shell.cpp" in item for item in items), "Host is missing its concrete DX12 shell")
    require(not any("Vulkan" in item or "IImGuiRendererBackend" in item for item in items),
            "Host still compiles a Vulkan shell or abstract renderer backend")
    source = read(host / "RHI/ImGuiHost.cpp")
    require("std::unique_ptr<ImGuiDx12Shell>" in source, "Host renderer storage is not concrete DX12")
    require("std::make_unique<ImGuiDx12Shell>()" in source, "Host does not construct its DX12 shell")
    require("RuntimeSettings" not in source and "ImGuiVulkanShell" not in source,
            "Host still has a runtime backend selector")
    for name in ("RHI/IImGuiHost.h", "RHI/DX12/ImGuiDx12Shell.h", "RHI/ImGuiHostPresentationSink.h"):
        text = read(host / name)
        require("defined(CE_PLAYER)" in text and "#error" in text, f"{name}: missing Player compile guard")
    require(not (host / "RHI/IImGuiRendererBackend.h").exists(), "Obsolete ImGui backend interface remains")
    require(not (host / "RHI/Vulkan/ImGuiVulkanShell.cpp").exists(), "Obsolete Vulkan ImGui shell remains")


def inspect_editor_policy():
    app = read(ROOT / "Editor/EngineEntry/App.cpp")
    main = read(ROOT / "Editor/EngineEntry/EditorMain.cpp")
    launch = read(ROOT / "Engine/RuntimeHost/EngineLaunchConfig.h")
    bootstrap = read(ROOT / "Engine/RuntimeHost/EngineBootstrap.h")
    settings = read(ROOT / "Engine/Utility_Framework/RuntimeSettings.cpp")
    require("RuntimeRenderBackendPolicy::FixedDX12" in app, "Editor does not select its build-fixed policy")
    require("GetRenderBackend" not in app and "GetRenderBackend" not in main,
            "Editor bootstrap still reads/checks a runtime backend")
    require("constexpr EnhancedLiveBackend startupBackend = EnhancedLiveBackend::DX12" in main,
            "Editor scene renderer is not build-fixed DX12")
    require("const RuntimeRenderBackendPolicy renderBackendPolicy" in launch,
            "Host backend policy is mutable")
    require("RuntimeSettings::Initialize(config.renderBackendPolicy)" in bootstrap,
            "Common bootstrap drops the explicit host backend policy")
    require("RuntimeRenderBackendPolicy::ProjectSettings == m_backendPolicy" in settings or
            "m_backendPolicy == RuntimeRenderBackendPolicy::ProjectSettings" in settings,
            "Player-only backend parsing policy is missing")


def inspect_guards():
    directory_targets = read(ROOT / "Directory.Build.targets")
    require('Import Project="$(MSBuildThisFileDirectory)Player\\PlayerBoundary.targets"' in directory_targets,
            "Player boundary targets are not imported")
    targets = read(ROOT / "Player/PlayerBoundary.targets")
    require("CE_PLAYER=1" in targets and "ForbiddenIncludes" in targets,
            "Player compile guard/poison include path is absent")
    for name in ("imgui.h", "imgui_internal.h"):
        path = ROOT / "Player/ForbiddenIncludes" / name
        require(path.is_file() and "#error" in read(path), f"Missing poison include: {name}")
    common = read(ROOT / "Engine/RenderEngine/RenderEngine.vcxproj")
    require("VulkanDeviceResources.cpp" in common, "Common Vulkan RHI was removed")
    require((ROOT / "Player/PlayerVulkanPresentation.cpp").is_file(), "Native Player Vulkan implementation is missing")
    require((ROOT / "Player/PlayerDX12Presentation.cpp").is_file(), "Native Player DX12 implementation is missing")


def main():
    projects = inspect_runtime_graph()
    inspect_sources()
    inspect_host()
    inspect_editor_policy()
    inspect_guards()
    print(f"Source-only presentation boundary: {len(projects)} runtime projects inspected")
    for error in ERRORS:
        print("ERROR: " + error)
    if ERRORS:
        return 1
    print("PASS: declared source/project boundary; no compile, link-symbol, GPU or runtime claim")
    return 0


if __name__ == "__main__":
    sys.exit(main())
