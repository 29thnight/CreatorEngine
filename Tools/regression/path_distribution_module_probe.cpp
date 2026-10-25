#if defined(CE_CONTRACT_STANDALONE)
#include "../build/ModuleAdapters/Creator.Core.Paths.h"
#include "../build/ModuleAdapters/Creator.Core.Distribution.h"
#else
import std;
import Creator.Core.Paths;
import Creator.Core.Distribution;
#endif

const void* path_state_from_other_unit();
const void* distribution_from_other_unit();
std::filesystem::path assets_from_other_unit();

int main(int argc, char** argv)
{
    unsigned checks{};
    const auto require = [&](bool condition, const char* message)
    {
        ++checks;
        if (!condition)
        {
            throw std::runtime_error(message);
        }
    };
    try
    {
        if (argc != 2)
        {
            throw std::runtime_error("Expected fresh probe work directory");
        }
        const auto work = std::filesystem::absolute(argv[1]);
        require(!std::filesystem::exists(work), "Probe directory must be fresh");
        std::filesystem::create_directories(work);
        require(ce::paths::resolve_process_executable_directory() == std::filesystem::absolute(argv[0]).parent_path(),
                "Executable directory");
        require(path_state_from_other_unit() == &ce::paths::get_state(), "Path registry identity");
        require(distribution_from_other_unit() == &ce::distribution::current_identity(), "Distribution cache identity");
        require(!ce::paths::roots{}.is_valid(), "Empty roots invalid");
        require(!ce::paths::initialize({}), "Empty roots rejected");

        ce::paths::roots paths;
        paths.executable_root = work;
        paths.runtime_owner_root = work / "Owned";
        paths.runtime_process_root = paths.runtime_owner_root / "Process";
        paths.runtime_content_root = paths.runtime_process_root / "RuntimeContent";
        paths.runtime_data_root = paths.runtime_process_root / "RuntimeData";
        paths.project_root = paths.runtime_content_root;
        paths.assets_root = paths.runtime_content_root / "Assets";
        require(paths.is_valid(), "Valid roots");
        require(paths.has_runtime_ownership_capability(), "Ownership capability");
        require(paths.has_valid_runtime_ownership(), "Owned layout");
        auto invalid = paths;
        invalid.assets_root = work / "Outside";
        require(!invalid.has_valid_runtime_ownership(), "Outside assets rejected");
        invalid = paths;
        invalid.runtime_owner_root = work.root_path();
        require(!invalid.has_valid_runtime_ownership(), "Drive root rejected");
        invalid = paths;
        invalid.runtime_data_root = paths.runtime_content_root;
        require(!invalid.has_valid_runtime_ownership(), "Aliased content/data rejected");
        invalid = paths;
        invalid.runtime_process_root = "relative";
        require(!invalid.has_valid_runtime_ownership(), "Relative ownership rejected");
        invalid = paths;
        auto upper = invalid.assets_root.native();
        for (auto& character : upper)
        {
            if (character >= L'a' && character <= L'z')
            {
                character -= L'a' - L'A';
            }
        }
        invalid.assets_root = upper;
        require(invalid.has_valid_runtime_ownership(), "Case-insensitive ownership");

        require(ce::paths::initialize(paths), "Packaged initialization");
        require(!ce::paths::is_asset_authoring_enabled(), "Packaged authoring disabled");
        require(!std::filesystem::exists(paths.assets_root), "Packaged initialization must not create source assets");
        for (const auto& directory :
             {ce::paths::log_path(), ce::paths::dump_path(), ce::paths::cache_path(), ce::paths::config_path(),
              ce::paths::trace_path(), ce::paths::test_artifact_path()})
        {
            require(std::filesystem::is_directory(directory), "Mutable runtime directory prepared");
        }
        require(ce::paths::managed_path() == work / "Managed" / "", "Managed fallback");
        require(ce::paths::engine_resource_path() == work / "Resources" / "", "Resources fallback");
        require(assets_from_other_unit() == paths.assets_root, "Shared state visible across units");
        require(ce::paths::relative("texture.bin") == paths.assets_root / "texture.bin", "Asset relative join");
        require(ce::paths::relative_to_shader("default.hlsl") == paths.assets_root / "Shaders/default.hlsl",
                "Shader join");
        require(ce::paths::runtime_data_path("nested") == paths.runtime_data_root / "nested", "Data join");
        paths.managed_root = work / "HostManaged";
        paths.engine_resource_root = work / "HostResources";
        paths.test_artifact_root = work / "HostTests";
        paths.enable_asset_authoring = true;
        require(ce::paths::initialize(paths), "Authoring initialization");
        require(ce::paths::is_asset_authoring_enabled(), "Authoring capability retained");
        require(std::filesystem::is_directory(paths.assets_root / "Shaders"), "Authoring shader directory");
        require(std::filesystem::is_directory(paths.engine_resource_root / "Icons"), "Authoring icon directory");
        require(ce::paths::managed_path() == paths.managed_root / "", "Managed override");
        require(ce::paths::engine_resource_path() == paths.engine_resource_root / "", "Resources override");
        require(ce::paths::test_artifact_path() == paths.test_artifact_root / "", "Tests override");
        const auto metadata = work / "engine.info";
        const auto write = [&](std::string_view contents)
        {
            std::ofstream stream(metadata, std::ios::binary);
            stream << contents;
        };
        write("channel=preview\nbuildId=probe\n");
        const auto values = ce::distribution::read_metadata_file(metadata);
        require(values.at("channel") == "preview" && values.at("buildId") == "probe", "Metadata values");
        for (const auto& contents : {std::string("key=1\nkey=2\n"), std::string("=empty-key\n"),
                                     std::string("missing-separator\n"), std::string(4097, 'x')})
        {
            write(contents);
            bool rejected = false;
            try
            {
                (void)ce::distribution::read_metadata_file(metadata);
            }
            catch (const std::exception&)
            {
                rejected = true;
            }
            require(rejected, "Malformed or oversized metadata rejected");
        }
        require(ce::distribution::build[0] != '\0', "Generated build value");
        require(ce::distribution::current_identity().channel == "preview" ||
                    ce::distribution::current_identity().channel == "stable",
                "Distribution channel");
        std::cout << "PATH_DISTRIBUTION_OK checks=" << checks << '\n';
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
