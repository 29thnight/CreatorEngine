#include "../../Containers/ContainerAPI.h"
#if !defined(CE_CONTRACT_STANDALONE) || !defined(CE_PATHS_SOURCE_IMPLEMENTED)
#define CE_PATHS_SOURCE_IMPLEMENTED
#if defined(CE_CONTRACT_STANDALONE)
#include "../../../Tools/build/ModuleAdapters/Creator.Core.Paths.h"
#define CE_PATHS_DEFINITION inline
#else
module Creator.Core.Paths;
#define CE_PATHS_DEFINITION
#endif

namespace ce::paths
{
    CE_PATHS_DEFINITION state& get_state()
    {
        // Match the previous Singleton: lazy, thread-safe, process-lifetime storage.
        static auto* instance = new state;
        return *instance;
    }

    CE_PATHS_DEFINITION bool state::initialize(const roots& paths)
    {
        if (!paths.is_valid())
        {
            return false;
        }

        executable_path = paths.executable_root.lexically_normal();
        runtime_content_root = paths.runtime_content_root.lexically_normal();
        runtime_data_root = paths.runtime_data_root.lexically_normal();
        managed_root =
            (paths.managed_root.empty() ? executable_path / L"Managed" : paths.managed_root).lexically_normal();
        engine_resource_root =
            (paths.engine_resource_root.empty() ? executable_path / L"Resources" : paths.engine_resource_root)
                .lexically_normal();
        cache_root = (runtime_data_root / L"Cache").lexically_normal();
        config_root = (runtime_data_root / L"Config").lexically_normal();
        trace_root = (runtime_data_root / L"Traces").lexically_normal();
        test_artifact_root =
            (paths.test_artifact_root.empty() ? runtime_data_root / L"Tests" : paths.test_artifact_root)
                .lexically_normal();
        dump_path = (runtime_data_root / "Dump").lexically_normal();
        log_path = (runtime_data_root / "Log").lexically_normal();
        base_project_path = paths.project_root.lexically_normal();
        asset_authoring_enabled = paths.enable_asset_authoring;

        // Host-owned mutable data is valid in both Editor and packaged Player. It is
        // always prepared independently from the asset-authoring capability.
        for (const std::filesystem::path& runtimeDirectory :
             {log_path, dump_path, cache_root, config_root, trace_root, test_artifact_root})
        {
            std::error_code runtimeDirError{};
            std::filesystem::create_directories(runtimeDirectory, runtimeDirError);
            if (runtimeDirError || !std::filesystem::is_directory(runtimeDirectory, runtimeDirError) || runtimeDirError)
            {
                std::cerr << "Failed to prepare runtime data directory: " << runtimeDirectory.string() << '\n';
                return false;
            }
        }

        // The process host resolves project/package roots before runtime initialization.
        // PathFinder derives only stable subdirectories and never branches on host identity.
        const std::filesystem::path content_root = runtime_content_root;
        const std::filesystem::path assets_root = paths.assets_root.lexically_normal();

        data_path = assets_root;
        model_source_path = assets_root / "Models";
        texture_source_path = assets_root / "Textures";
        material_source_path = assets_root / "Materials";
        ui_source_path = assets_root / "UI";
        prefab_source_path = assets_root / "Prefabs";
        shader_source_path = assets_root / "Shaders";
        dynamic_solution_dir = content_root;
        project_settings_path = (content_root / L"ProjectSetting").lexically_normal();

        icon_path = (engine_resource_root / L"Icons").lexically_normal();
        terrain_source_path = assets_root / "Terrain";
        node_editor_path = assets_root / "NodeEditor";
        scene_render_profile_path = assets_root / "SceneRenderProfile";
        input_map_path = assets_root / "InputMap";
        // GameBuildSlnPath가 여기 있었다 — 게임 빌드가 MSBuild를 부르지
        // 않게 되면서(B0-3, BuildPipelinePlan §2.0) 소비자와 함께 걷었다.
        animator_path = assets_root / "AnimatorController";

        data_path = data_path.lexically_normal();
        model_source_path = model_source_path.lexically_normal();
        texture_source_path = texture_source_path.lexically_normal();
        material_source_path = material_source_path.lexically_normal();
        ui_source_path = ui_source_path.lexically_normal();
        prefab_source_path = prefab_source_path.lexically_normal();
        shader_source_path = shader_source_path.lexically_normal();
        terrain_source_path = terrain_source_path.lexically_normal();
        node_editor_path = node_editor_path.lexically_normal();
        scene_render_profile_path = scene_render_profile_path.lexically_normal();
        input_map_path = input_map_path.lexically_normal();
        animator_path = animator_path.lexically_normal();
        //dir not exist -> create dir

        ce::dynamic_array<std::filesystem::path> directories = {
            data_path,
            shader_source_path,
            model_source_path,
            texture_source_path,
            material_source_path,
            ui_source_path,
            prefab_source_path,
            icon_path,
            dynamic_solution_dir,
            project_settings_path,
            terrain_source_path,
            scene_render_profile_path,
            node_editor_path,
            input_map_path,
            animator_path,
        };

        // Authoring directory creation is an explicit Host capability. Packaged content
        // roots are populated by extraction and must not gain empty source directories.
        if (paths.enable_asset_authoring)
        {
            for (const auto& path : directories)
            {
                if (!std::filesystem::exists(path))
                {
                    std::filesystem::create_directories(path);
                }
            }
        }
        return true;
    }

    CE_PATHS_DEFINITION bool initialize(const roots& paths) noexcept
    {
        return get_state().initialize(paths);
    }

    CE_PATHS_DEFINITION std::filesystem::path relative()
    {
        return get_state().data_path;
    }

    CE_PATHS_DEFINITION bool is_asset_authoring_enabled() noexcept
    {
        return get_state().asset_authoring_enabled;
    }

    CE_PATHS_DEFINITION std::filesystem::path dump_path()
    {
        return get_state().dump_path;
    }

    CE_PATHS_DEFINITION std::filesystem::path log_path()
    {
        return get_state().log_path;
    }

    CE_PATHS_DEFINITION std::filesystem::path relative(std::string_view path)
    {
        return std::filesystem::path(get_state().data_path) / path;
    }

    CE_PATHS_DEFINITION std::filesystem::path relative_to_shader()
    {
        return std::filesystem::path(get_state().shader_source_path);
    }

    CE_PATHS_DEFINITION std::filesystem::path relative_to_shader(std::string_view path)
    {
        return std::filesystem::path(get_state().shader_source_path) / path;
    }

    CE_PATHS_DEFINITION std::filesystem::path relative_to_executable(std::string_view path)
    {
        return std::filesystem::path(get_state().executable_path) / path;
    }

    CE_PATHS_DEFINITION std::filesystem::path relative_to_material(std::string_view path)
    {
        return std::filesystem::path(get_state().material_source_path) / path;
    }

    CE_PATHS_DEFINITION std::filesystem::path shader_path()
    {
        return get_state().shader_source_path;
    }

    CE_PATHS_DEFINITION std::filesystem::path engine_resource_path(std::string_view path)
    {
        return get_state().engine_resource_root / path;
    }

    CE_PATHS_DEFINITION std::filesystem::path icon_path()
    {
        return get_state().icon_path;
    }

    CE_PATHS_DEFINITION std::filesystem::path model_source_path()
    {
        return get_state().model_source_path;
    }

    CE_PATHS_DEFINITION std::filesystem::path texture_source_path()
    {
        return get_state().texture_source_path;
    }

    CE_PATHS_DEFINITION std::filesystem::path ui_source_path()
    {
        return get_state().ui_source_path;
    }

    CE_PATHS_DEFINITION std::filesystem::path prefab_source_path()
    {
        return get_state().prefab_source_path;
    }

    CE_PATHS_DEFINITION std::filesystem::path material_source_path()
    {
        return get_state().material_source_path;
    }

    CE_PATHS_DEFINITION std::filesystem::path base_project_path()
    {
        return get_state().base_project_path;
    }

    CE_PATHS_DEFINITION std::filesystem::path runtime_content_path()
    {
        return get_state().runtime_content_root;
    }

    CE_PATHS_DEFINITION std::filesystem::path runtime_data_path(std::string_view path)
    {
        return std::filesystem::path(get_state().runtime_data_root) / path;
    }

    CE_PATHS_DEFINITION std::filesystem::path cache_path(std::string_view path)
    {
        return std::filesystem::path(get_state().cache_root) / path;
    }

    CE_PATHS_DEFINITION std::filesystem::path config_path(std::string_view path)
    {
        return std::filesystem::path(get_state().config_root) / path;
    }

    CE_PATHS_DEFINITION std::filesystem::path trace_path(std::string_view path)
    {
        return std::filesystem::path(get_state().trace_root) / path;
    }

    CE_PATHS_DEFINITION std::filesystem::path test_artifact_path(std::string_view path)
    {
        return std::filesystem::path(get_state().test_artifact_root) / path;
    }

    CE_PATHS_DEFINITION std::filesystem::path managed_path(std::string_view path)
    {
        return std::filesystem::path(get_state().managed_root) / path;
    }

    CE_PATHS_DEFINITION std::filesystem::path scene_render_profile_path()
    {
        return get_state().scene_render_profile_path;
    }

    CE_PATHS_DEFINITION std::filesystem::path dynamic_solution_path(std::string_view path)
    {
        return std::filesystem::path(get_state().dynamic_solution_dir) / path;
    }

    CE_PATHS_DEFINITION std::filesystem::path project_setting_path(std::string_view path)
    {
        return std::filesystem::path(get_state().project_settings_path) / path;
    }

    CE_PATHS_DEFINITION std::filesystem::path terrain_source_path(std::string_view path)
    {
        return std::filesystem::path(get_state().terrain_source_path) / path;
    }

    CE_PATHS_DEFINITION std::filesystem::path node_editor_path(std::string_view path)
    {
        return std::filesystem::path(get_state().node_editor_path) / path;
    }

    CE_PATHS_DEFINITION std::filesystem::path relative_to_model(std::string_view path)
    {
        return std::filesystem::path(get_state().model_source_path) / path;
    }

    CE_PATHS_DEFINITION std::filesystem::path relative_to_texture(std::string_view path)
    {
        return std::filesystem::path(get_state().texture_source_path) / path;
    }

    CE_PATHS_DEFINITION std::filesystem::path relative_to_ui_source(std::string_view path)
    {
        return std::filesystem::path(get_state().ui_source_path) / path;
    }

    CE_PATHS_DEFINITION std::filesystem::path relative_to_prefab(std::string_view path)
    {
        return std::filesystem::path(get_state().prefab_source_path) / path;
    }

    CE_PATHS_DEFINITION std::filesystem::path relative_to_base_project(std::string_view path)
    {
        return std::filesystem::path(get_state().base_project_path) / path;
    }

    CE_PATHS_DEFINITION std::filesystem::path relative_to_scene_render_profile(std::string_view path)
    {
        return std::filesystem::path(get_state().scene_render_profile_path) / path;
    }

    CE_PATHS_DEFINITION std::filesystem::path input_map_path()
    {
        return get_state().input_map_path;
    }

    CE_PATHS_DEFINITION std::filesystem::path input_map_path(std::string_view path)
    {
        return std::filesystem::path(get_state().input_map_path) / path;
    }

    CE_PATHS_DEFINITION std::filesystem::path animator_json_path()
    {
        return get_state().animator_path;
    }

    CE_PATHS_DEFINITION std::filesystem::path animator_json_path(std::string_view path)
    {
        return std::filesystem::path(get_state().animator_path) / path;
    }

} // namespace ce::paths

#undef CE_PATHS_DEFINITION

#endif
