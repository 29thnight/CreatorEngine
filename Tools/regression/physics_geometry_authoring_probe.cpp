#include "../../Editor/EngineEntry/CollisionGeometryAuthoring.h"
#include "../../Editor/EngineEntry/UndoHistoryTransaction.h"
#include "../../Engine/SceneRuntime/CollisionGeometryIO.h"
#include "../../Engine/SceneRuntime/CollisionGeometryLibrary.h"
#include <iostream>
#include <stdexcept>
#include <stack>

struct HistoryCommand
{
    std::function<void()> undo, redo;
    void Undo() { undo(); }
    void Redo() { redo(); }
};

struct HistoryStack : std::stack<std::unique_ptr<HistoryCommand>>
{
    bool failAllocation = false;
    void push(std::unique_ptr<HistoryCommand> value)
    {
        if (failAllocation)
        {
            failAllocation = false;
            throw std::bad_alloc{};
        }
        std::stack<std::unique_ptr<HistoryCommand>>::push(std::move(value));
    }
};

int main(int argc, char** argv)
{
    using namespace ce::physics;
    std::size_t checks = 0;
    const auto check = [&](bool value) {
        ++checks;
        if (!value)
            throw std::runtime_error("Geometry authoring check " + std::to_string(checks));
    };

    try
    {
        if (argc != 2)
            return 2;
        const std::filesystem::path root(argv[1]);
        std::filesystem::create_directories(root);
        const geometry_asset_key key{Uuid::Parse("91ea9b44-13a6-4aec-99ec-0963eef7eaa1"), 1};
        constexpr std::string_view tetra = "4 0 0 0 1 0 0 0 1 0 0 0 1";
        auto source = Editor::ParseCollisionGeometry(tetra, "convex", key);
        check(bool(source));
        check(bool(Editor::ParseCollisionGeometry("3 0 0 0 1 0 0 0 1 0 1 0 1 2", "mesh", key)));
        check(bool(Editor::ParseCollisionGeometry("2 2 -32768 32767 0 1", "heightfield", key)));
        for (const auto text :
             {"", "4", "4 0 0 0 1 0 0 0 1 0 0 0 nan", "4294967295", "-1", "4 0 0 0 1 0 0 0 1 0 0 0 1 extra"})
            check(!Editor::ParseCollisionGeometry(text, "convex", key));

        check(!Editor::ParseCollisionGeometry("2 2 32768 0 0 0", "heightfield", key));
        check(!Editor::ParseCollisionGeometry("2 2 -32769 0 0 0", "heightfield", key));
        check(!Editor::ParseCollisionGeometry("3 0 0 0 1 0 0 0 1 0 1 0 1 3", "mesh", key));
        check(!Editor::ParseCollisionGeometry(tetra, "unknown", key));
        check(!Editor::CollisionGeometryAuthoring::Create(root, root.parent_path() / "escape.cegeometry", *source));
        check(!Editor::CollisionGeometryAuthoring::Create(root, root / "bad.txt", *source));

        CollisionGeometryLibrary library;
        const auto target = root / "tetra.cegeometry";
        std::filesystem::remove(target);
        std::filesystem::remove(target.string() + ".meta");
        auto published = library.Publish(
            *source, [&](auto) { return bool(Editor::CollisionGeometryAuthoring::Create(root, target, *source)); });
        check(bool(published));
        check(bool(CollisionGeometryIO::Read(target, key)));
        check(Editor::ReadCollisionGeometrySource(target)->key == key);
        check(!Editor::ReadCollisionGeometrySource(root / "missing.cegeometry"));
        check(std::filesystem::exists(target.string() + ".meta"));
        check(!Editor::CollisionGeometryAuthoring::Create(root, target, *source));
        check(bool(CollisionGeometryIO::Read(target, key)));

        for (const auto suffix : {".meta", ".geometry.tmp", ".meta.geometry.tmp"})
        {
            const auto orphan = root / ("orphan" + std::to_string(checks) + ".cegeometry");
            const auto blocker = std::filesystem::path(orphan.string() + suffix);
            std::ofstream(blocker) << "existing";
            check(!Editor::CollisionGeometryAuthoring::Create(root, orphan, *source));
            check(std::filesystem::file_size(blocker) == 8);
            check(!std::filesystem::exists(orphan));
        }

        for (const int failAt : {1, 2})
        {
            const auto failed = root / ("failed" + std::to_string(failAt) + ".cegeometry");
            int moves = 0;
            const auto result = Editor::CollisionGeometryAuthoring::Create(
                root, failed, *source, [&](const auto& from, const auto& to) {
                    return ++moves != failAt && Editor::CollisionGeometryAuthoring::Move(from, to);
                });
            check(!result);
            check(!std::filesystem::exists(failed));
            check(!std::filesystem::exists(failed.string() + ".meta"));
            check(!std::filesystem::exists(failed.string() + ".geometry.tmp"));
            check(!std::filesystem::exists(failed.string() + ".meta.geometry.tmp"));
        }

        auto second = *source;
        second.key.asset = Uuid::Parse("c53b60ef-d00c-47b7-bd12-dcd3433d6476");
        const auto before = *library.Stats();
        check(!library.Publish(
            second, [&](auto) { return bool(Editor::CollisionGeometryAuthoring::Create(root, target, second)); }));
        check(library.Stats()->assets == before.assets);
        check(bool(CollisionGeometryIO::Read(target, key)));

        auto degenerate = second;
        degenerate.form = convex_source{std::vector<math::vector3>(4, {0, 0, 0})};
        bool called = false;
        check(!library.Publish(degenerate, [&](auto) {
            called = true;
            return true;
        }));
        check(!called);

        auto revision = *source;
        revision.key.revision = 2;
        check(!Editor::CollisionGeometryAuthoring::Create(root, root / "revision.cegeometry", revision));

        std::get<convex_source>(revision.form).points[1].x = 2;
        Editor::CollisionGeometryRevisionEdit edit(*source, revision);
        const auto apply = [&](const auto& expected, const auto& next) {
            return library.Publish(next, [&](auto) {
                const auto meta = "guid: " + Uuid::ToString(next.key.asset) +
                                  "\ngeometryRevision: " + std::to_string(next.key.revision) + "\ncustom: retained\n";
                return bool(Editor::CollisionGeometryAuthoring::Replace(root, target, expected, next, meta));
            });
        };
        check(bool(edit.Redo(apply)));
        check(bool(CollisionGeometryIO::Read(target, revision.key)));
        check(bool(CollisionGeometryIO::Read(CollisionGeometryIO::RevisionPath(root, key), key)));
        check(bool(CollisionGeometryIO::Read(CollisionGeometryIO::RevisionPath(root, revision.key), revision.key)));
        check(bool(edit.Undo(apply)));
        check(bool(CollisionGeometryIO::Read(target, key)));
        check(*Editor::CollisionGeometryAuthoring::NextRevision(root, key) == 3);
        check(bool(edit.Redo(apply)));
        check(!edit.Redo(apply));

        const auto readBytes = [](const auto& path) {
            std::ifstream input(path, std::ios::binary);
            return std::string(std::istreambuf_iterator<char>(input), {});
        };
        const auto originalMeta = readBytes(std::filesystem::path(target.string() + ".meta"));
        auto third = revision;
        third.key.revision = 3;
        std::get<convex_source>(third.form).points[1].x = 3;
        for (int failAt : {1, 2, 3, 4})
        {
            int moves = 0;
            check(!Editor::CollisionGeometryAuthoring::Replace(
                root, target, revision, third, "guid: valid\ngeometryRevision: 3\n",
                [&](const auto& from, const auto& to) {
                    return ++moves != failAt && Editor::CollisionGeometryAuthoring::Move(from, to);
                }));
            check(bool(CollisionGeometryIO::Read(target, revision.key)));
            check(readBytes(std::filesystem::path(target.string() + ".meta")) == originalMeta);
            for (const auto suffix : {".geometry-update.tmp", ".meta.geometry-update.tmp", ".geometry-backup.tmp",
                                      ".meta.geometry-backup.tmp"})
                check(!std::filesystem::exists(target.string() + suffix));
        }

        check(*Editor::CollisionGeometryAuthoring::NextRevision(root, revision.key) == 4);
        auto conflict = third;
        std::get<convex_source>(conflict.form).points[1].x = 9;
        check(!Editor::CollisionGeometryAuthoring::Replace(root, target, revision, conflict, "invalid"));
        check(bool(CollisionGeometryIO::Read(target, revision.key)));

        check(!Editor::CollisionGeometryAuthoring::Replace(root, target, *source, third, "invalid"));
        check(bool(CollisionGeometryIO::Read(target, revision.key)));
        check(bool(edit.Undo(apply)));
        check(*Editor::CollisionGeometryAuthoring::NextRevision(root, key) == 4);

        CollisionGeometryLibrary fresh;
        check(bool(fresh.Resolve(revision.key, [&](auto expected) {
            return CollisionGeometryIO::Read(CollisionGeometryIO::RevisionPath(root, expected), expected);
        })));

        HistoryStack undo, redo;
        const auto command = [&] {
            return std::make_unique<HistoryCommand>(HistoryCommand{[&] {
                                                                       if (!edit.Undo(apply))
                                                                           throw std::runtime_error("Undo failed");
                                                                   },
                                                                   [&] {
                                                                       if (!edit.Redo(apply))
                                                                           throw std::runtime_error("Redo failed");
                                                                   }});
        };
        undo.failAllocation = true;
        try
        {
            Editor::ExecuteUndoHistory(undo, redo, command());
            check(false);
        }
        catch (const std::bad_alloc&)
        {
            check(true);
        }
        check(undo.empty() && redo.empty());
        check(bool(CollisionGeometryIO::Read(target, key)));

        Editor::ExecuteUndoHistory(undo, redo, command());
        check(undo.size() == 1 && redo.empty());
        redo.failAllocation = true;
        try
        {
            Editor::TransferUndoHistory(undo, redo, true);
            check(false);
        }
        catch (const std::bad_alloc&)
        {
            check(true);
        }
        check(undo.size() == 1 && redo.empty());
        check(bool(CollisionGeometryIO::Read(target, revision.key)));

        Editor::TransferUndoHistory(undo, redo, true);
        check(undo.empty() && redo.size() == 1);
        check(bool(CollisionGeometryIO::Read(target, key)));
        undo.failAllocation = true;
        try
        {
            Editor::TransferUndoHistory(redo, undo, false);
            check(false);
        }
        catch (const std::bad_alloc&)
        {
            check(true);
        }
        check(undo.empty() && redo.size() == 1);
        check(bool(CollisionGeometryIO::Read(target, key)));

        Editor::TransferUndoHistory(redo, undo, false);
        check(undo.size() == 1 && redo.empty());
        try
        {
            Editor::ExecuteUndoHistory(undo, redo, command());
            check(false);
        }
        catch (const std::runtime_error&)
        {
            check(true);
        }
        check(undo.size() == 1 && redo.empty());

        std::cout << "{\"result\":\"PHYSICS_GEOMETRY_AUTHORING_OK\",\"checks\":" << checks << "}\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
