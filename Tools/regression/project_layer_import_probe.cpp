#include "../../Editor/EngineEntry/LegacyProjectLayerImport.h"
#include <iostream>
#include "../../Engine/SceneRuntime/EntityLayerSchema.h"

namespace
{
int checks = 0;

void check(bool value, const char* label)
{
    ++checks;
    if (!value)
    {
        std::cerr << label << '\n';
        std::exit(1);
    }
}

void write(const std::filesystem::path& path, std::string_view text)
{
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file.write(text.data(), static_cast<std::streamsize>(text.size()));
    check(bool(file), "fixture written");
}
} // namespace

int main(int argc, char** argv)
{
    check(argc == 3, "source project and fixture directory required");
    const std::filesystem::path source(argv[1]), fixture(argv[2]);
    const auto imported = Editor::ImportLegacyProjectLayers(source);
    check(bool(imported), "real project legacy settings import");
    std::string parse_error;
    const auto tags = Authoring::ParsedDocument::ParseFile((source / "TagManager.asset").string(), parse_error);
    const auto matrix = Authoring::ParsedDocument::ParseFile((source / "CollisionMatrix.asset").string(), parse_error);
    check(tags && matrix, "independent source documents loaded");
    check(std::ranges::count_if(imported->catalog.definitions, [](const auto& item) { return bool(item); }) ==
              static_cast<std::ptrdiff_t>(tags.Root()["layers"].Size()),
          "migration preserves all source layers");
    std::size_t index = 0;
    for (const auto name : tags.Root()["layers"])
    {
        const auto& item = imported->catalog.definitions[index];
        check(item && item->name == name.AsString() && item->slot.Value() == index &&
                  item->slot.Mask() == (std::uint32_t{1} << index),
              "source layer bit meanings preserved");
        ++index;
    }
    for (std::size_t row = 0; row < 32; ++row)
        for (std::size_t column = 0; column < 32; ++column)
            check(imported->policy.matrix[row * 32 + column] ==
                      static_cast<std::uint8_t>(row < matrix.Root().Size() && column < matrix.Root().Size()
                                                    ? matrix.Root().At(row).At(column).As<bool>()
                                                    : true),
                  "every source collision rule preserved");

    const auto bytes = ce::layers::ProjectLayerSettingsCodec::Encode(*imported);
    check(bool(bytes), "migrated project encodes to CLYR");
    const auto output = fixture / "migrated.celayers";
    write(output, std::string_view(reinterpret_cast<const char*>(bytes->data()), bytes->size()));
    const auto reloaded = ProjectLayerSettingsIO::Read(output);
    check(reloaded && reloaded->catalog.ActiveMask() == imported->catalog.ActiveMask() &&
              reloaded->policy.matrix == imported->policy.matrix,
          "real project migration file round trip");

    std::size_t entity_count = 0, asset_count = 0;
    std::function<void(Authoring::ReadNode)> visit = [&](Authoring::ReadNode node) {
        if (!node.IsMap() && !node.IsSequence())
            return;
        if (node["m_layerId"] || node["m_tag"])
        {
            check(bool(ce::layers::ReadEntityLayer(node, imported->catalog)),
                  "real migrated entity uses registered ID");
            ++entity_count;
        }
        for (const auto child : node)
            visit(child);
    };
    for (const auto& asset : std::filesystem::recursive_directory_iterator(source.parent_path() / "Assets"))
    {
        if (!asset.is_regular_file() ||
            (asset.path().extension() != ".creator" && asset.path().extension() != ".prefab"))
            continue;
        const auto document = Authoring::ParsedDocument::ParseFile(asset.path().string(), parse_error);
        check(bool(document), "native parser accepts migrated authoring asset");
        visit(document.Root());
        ++asset_count;
    }
    check(entity_count > 0 && asset_count > 0, "native migrated corpus is populated");
    for (const auto text : {"m_layer: Default", "m_layerId: 0", "m_layerId: 999", "m_layerId: broken",
                            "m_layerId: 1\nm_collisionType: 0", "m_layerId: 1\nm_layer: Default"})
    {
        const auto document = Authoring::ParsedDocument::ParseText(text, parse_error);
        check(document && !ce::layers::ReadEntityLayer(document.Root(), imported->catalog),
              "entity schema rejects legacy, corrupt or unknown references before mutation");
    }

    const auto invalid = fixture / "invalid-project";
    std::filesystem::create_directories(invalid);
    write(invalid / "TagManager.asset", "layers: [Default, Same, Same]\n");
    check(!Editor::ImportLegacyProjectLayers(invalid), "duplicate legacy names rejected");
    write(invalid / "TagManager.asset", "layers: [Unknown, Layer]\n");
    check(!Editor::ImportLegacyProjectLayers(invalid), "missing Default rejected");
    write(invalid / "TagManager.asset", "layers: [Default, Layer]\n");
    write(invalid / "CollisionMatrix.asset", "[[true]]\n");
    check(!Editor::ImportLegacyProjectLayers(invalid), "truncated matrix rejected");
    std::string asymmetric;
    for (int row = 0; row < 32; ++row)
    {
        asymmetric += "- [";
        for (int column = 0; column < 32; ++column)
        {
            if (column)
                asymmetric += ", ";
            asymmetric += row == 0 && column == 1 ? "false" : "true";
        }
        asymmetric += "]\n";
    }
    write(invalid / "CollisionMatrix.asset", asymmetric);
    check(!Editor::ImportLegacyProjectLayers(invalid), "asymmetric legacy policy rejected");
    asymmetric.replace(asymmetric.find("false"), 5, "not-a-bool");
    write(invalid / "CollisionMatrix.asset", asymmetric);
    check(!Editor::ImportLegacyProjectLayers(invalid), "invalid scalar rejected rather than defaulted");
    std::cout << "{\"result\":\"PROJECT_LAYER_IMPORT_OK\",\"checks\":" << checks << "}\n";
}
