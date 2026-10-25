export module Creator.Core.Paths;

import std;

export namespace ce::paths
{
    struct roots
    {
        std::filesystem::path executable_root{};
        std::filesystem::path project_root{};
        // Optional ownership capability supplied by a packaged process Host.
        // Editor paths leave these empty; runtime cleanup is unavailable without them.
        std::filesystem::path runtime_owner_root{};
        std::filesystem::path runtime_process_root{};
        std::filesystem::path runtime_content_root{};
        std::filesystem::path runtime_data_root{};
        std::filesystem::path assets_root{};
        // Host-resolved deployment roots. Runtime layers consume these values and do
        // not reconstruct the build/package layout from the current working directory.
        std::filesystem::path managed_root{};
        std::filesystem::path engine_resource_root{};
        std::filesystem::path test_artifact_root{};
        // Host-owned capability. When disabled, runtime code may read packaged assets
        // but must not create source directories, metadata, or file-system watchers.
        bool enable_asset_authoring{false};

        bool is_valid() const noexcept
        {
            return !executable_root.empty() && !project_root.empty() && !runtime_content_root.empty() &&
                   !runtime_data_root.empty() && !assets_root.empty();
        }

        bool has_runtime_ownership_capability() const noexcept
        {
            return !runtime_owner_root.empty() || !runtime_process_root.empty();
        }

        bool has_valid_runtime_ownership() const noexcept;
    };

    std::filesystem::path resolve_process_executable_directory();
    std::filesystem::path resolve_runtime_directory();

    struct state;
    state& get_state();

    // Host/test composition can configure the existing path registry directly.
    // Storage is owned by PathFinder.cpp, without a reflection Singleton dependency.
    struct state
    {

        std::filesystem::path executable_path{};
        std::filesystem::path data_path{};
        std::filesystem::path icon_path{};
        std::filesystem::path shader_source_path{};
        std::filesystem::path model_source_path{};
        std::filesystem::path texture_source_path{};
        std::filesystem::path ui_source_path{};
        std::filesystem::path prefab_source_path{};
        std::filesystem::path material_source_path{};
        std::filesystem::path dynamic_solution_dir{};
        std::filesystem::path base_project_path{};
        std::filesystem::path runtime_content_root{};
        std::filesystem::path runtime_data_root{};
        std::filesystem::path managed_root{};
        std::filesystem::path engine_resource_root{};
        std::filesystem::path cache_root{};
        std::filesystem::path config_root{};
        std::filesystem::path trace_root{};
        std::filesystem::path test_artifact_root{};
        std::filesystem::path project_settings_path{};
        std::filesystem::path terrain_source_path{};
        std::filesystem::path dump_path{};
        std::filesystem::path log_path{};
        std::filesystem::path node_editor_path{};
        std::filesystem::path scene_render_profile_path{};
        std::filesystem::path input_map_path{};
        std::filesystem::path animator_path{};
        bool asset_authoring_enabled{false};

        bool initialize(const roots& paths);
        state(const state&) = delete;
        state& operator=(const state&) = delete;
        state(state&&) = delete;
        state& operator=(state&&) = delete;

      private:
        state() = default;
        ~state() = default;
        friend state& get_state();
    };

    bool initialize(const roots& paths) noexcept;
    std::filesystem::path relative();
    bool is_asset_authoring_enabled() noexcept;
    std::filesystem::path dump_path();
    std::filesystem::path log_path();
    std::filesystem::path relative(std::string_view path);
    std::filesystem::path relative_to_shader();
    std::filesystem::path relative_to_shader(std::string_view path);
    std::filesystem::path relative_to_executable(std::string_view path);
    std::filesystem::path relative_to_material(std::string_view path);
    std::filesystem::path shader_path();
    std::filesystem::path engine_resource_path(std::string_view path = {});
    std::filesystem::path icon_path();
    std::filesystem::path model_source_path();
    std::filesystem::path texture_source_path();
    std::filesystem::path ui_source_path();
    std::filesystem::path prefab_source_path();
    std::filesystem::path material_source_path();
    std::filesystem::path base_project_path();
    std::filesystem::path runtime_content_path();
    std::filesystem::path runtime_data_path(std::string_view path);
    std::filesystem::path cache_path(std::string_view path = {});
    std::filesystem::path config_path(std::string_view path = {});
    std::filesystem::path trace_path(std::string_view path = {});
    std::filesystem::path test_artifact_path(std::string_view path = {});
    std::filesystem::path managed_path(std::string_view path = {});
    std::filesystem::path scene_render_profile_path();
    std::filesystem::path dynamic_solution_path(std::string_view path);
    std::filesystem::path project_setting_path(std::string_view path);
    std::filesystem::path terrain_source_path(std::string_view path);
    std::filesystem::path node_editor_path(std::string_view path);
    std::filesystem::path relative_to_model(std::string_view path);
    std::filesystem::path relative_to_texture(std::string_view path);
    std::filesystem::path relative_to_ui_source(std::string_view path);
    std::filesystem::path relative_to_prefab(std::string_view path);
    std::filesystem::path relative_to_base_project(std::string_view path);
    std::filesystem::path relative_to_scene_render_profile(std::string_view path);
    std::filesystem::path input_map_path();
    std::filesystem::path input_map_path(std::string_view path);
    std::filesystem::path animator_json_path();
    std::filesystem::path animator_json_path(std::string_view path);
} // namespace ce::paths
