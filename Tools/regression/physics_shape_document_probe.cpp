#include "../../Editor/EngineEntry/PhysicsShapeDocument.h"
#include "AuthoringWriteNode.h"
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
int main(int argc, char** argv)
{
    std::size_t checks = 0;
    const auto check = [&](bool value) {
        ++checks;
        if (!value)
            throw std::runtime_error("Shape document check " + std::to_string(checks));
    };
    try
    {
        if (argc >= 2)
        {
            std::ifstream input(argv[1], std::ios::binary);
            const std::string text{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
            check(bool(input) && !text.empty());

            const auto migrated = Editor::ParsePhysicsShapeDocument(text);
            check(migrated && migrated->size() == 1);
            check(migrated && migrated->front().shapeId == 2522806044u &&
                  migrated->front().halfExtent.x == .5f && migrated->front().dynamicFriction == .4f);
        }

        if (argc >= 3)
        {
            std::ifstream input(argv[2], std::ios::binary);
            const std::string text{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
            check(bool(input) && !text.empty());

            const auto capsules = Editor::ParsePhysicsShapeDocument(text);
            check(capsules && capsules->size() == 2);
            check(capsules && (*capsules)[0].kind == PhysicsShapeKind::capsule &&
                  (*capsules)[0].halfHeight == 2 && (*capsules)[0].localRotation.z < -.707f);
            check(capsules && (*capsules)[1].kind == PhysicsShapeKind::capsule &&
                  (*capsules)[1].halfHeight == 1 && (*capsules)[1].localRotation.w == 1);

            if (capsules && capsules->size() == 2)
            {
                const auto static_shapes = ce::physics::ValidatePhysicsShapes(
                    std::span<const PhysicsShapeDefinition>(*capsules).first(1), {1, 1, 1}, {},
                    ce::physics::body_kind::static_body);
                const auto dynamic_shapes = ce::physics::ValidatePhysicsShapes(
                    std::span<const PhysicsShapeDefinition>(*capsules).last(1), {1, 1, 1}, {},
                    ce::physics::body_kind::dynamic);
                check(bool(static_shapes));
                check(bool(dynamic_shapes));
            }
        }

        if (argc >= 4)
        {
            std::ifstream input(argv[3], std::ios::binary);
            const std::string text{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
            check(bool(input) && !text.empty());

            const auto geometry = Editor::ParsePhysicsShapeDocument(text);
            check(geometry && geometry->size() == 1);
            check(geometry && geometry->front().kind == PhysicsShapeKind::convex &&
                  geometry->front().geometryAsset == "d0f01397-566b-41cd-b1cc-78bfb0075459" &&
                  geometry->front().geometryRevision == 1);
            check(geometry && geometry->front().staticFriction == .4f &&
                  geometry->front().geometryScale.x == 1);
        }

        auto parsed = Editor::ParsePhysicsShapeDocument(
            R"([{"shapeId":11,"kind":0,"halfExtent":[1,2,3],"localPosition":[4,5,6],"sensor":true,"queryEnabled":false,"layerOverride":"18446744073709551615"},{"shapeId":22,"kind":1,"radius":2}])");
        check(parsed && parsed->size() == 2);
        check(parsed && (*parsed)[0].halfExtent.y == 2 && (*parsed)[0].localPosition.z == 6 && (*parsed)[0].sensor &&
              !(*parsed)[0].queryEnabled);
        check(parsed && (*parsed)[0].layerOverride == UINT64_MAX && (*parsed)[1].radius == 2);
        check(bool(Editor::ParsePhysicsShapeDocument(
            R"([{"shapeId":1,"kind":0,"halfExtent":{"x":1,"y":2,"z":3},"localRotation":{"x":0,"y":0,"z":0,"w":1}}])")));
        check(!Editor::ParsePhysicsShapeDocument(R"([{"shapeId":1,"kind":0,"halfExtent":{"x":1,"x":2,"z":3}}])"));
        check(!Editor::ParsePhysicsShapeDocument(R"([{"shapeId":1,"kind":0,"halfExtent":{"x":1,"y":2,"bad":3}}])"));
        check(!Editor::ParsePhysicsShapeDocument("[{shapeId: 1, kind: 0, radius: .nan}]"));
        check(!Editor::ParsePhysicsShapeDocument("[{shapeId: 1, kind: 0, geometryScale: [1,.inf,1]}]"));
        check(!Editor::ParsePhysicsShapeDocument(
            R"([{"shapeId":1,"kind":3,"geometryAsset":"91EA9B44-13A6-4AEC-99EC-0963EEF7EAA1","geometryRevision":1}])"));
        auto owned = std::move(*parsed);
        check(owned[0].shapeId == 11);
        check(bool(Editor::ParsePhysicsShapeDocument("- shapeId: 1\n  kind: 2\n  radius: 1\n  halfHeight: 2\n")));
        check(bool(Editor::ParsePhysicsShapeDocument(
            R"([{"shapeId":1,"kind":3,"geometryAsset":"91ea9b44-13a6-4aec-99ec-0963eef7eaa1","geometryRevision":"18446744073709551615","geometryScale":[2,3,4]}])")));
        for (const auto input :
             {"",
              "[]",
              "{}",
              "[{}]",
              "[{shapeId: 1}]",
              "[{kind: 0}]",
              "[{shapeId: 0, kind: 0}]",
              "[{shapeId: 1, kind: 6}]",
              "[{shapeId: 1, kind: 256}]",
              "[{shapeId: 1, kind: 0, unknown: 1}]",
              "[{shapeId: 1, shapeId: 2, kind: 0}]",
              "[{shapeId: 1, kind: 0}, {shapeId: 1, kind: 1}]",
              "[{shapeId: 1, kind: 0, halfExtent: [1, 2]}]",
              "[{shapeId: 1, kind: 0, halfExtent: [-1,2,3]}]",
              "[{shapeId: 1, kind: 1, radius: .nan}]",
              "[{shapeId: 1, kind: 0, sensor: 2}]",
              "[{shapeId: 1, kind: 0, localRotation: [0,0,0,0]}]",
              "[{shapeId: 1, kind: 0, localPosition: [0,.inf,0]}]",
              "[{shapeId: 1, kind: 0, layerOverride: 18446744073709551616}]",
              "[{shapeId: 1, kind: 0, layerOverride: -1}]",
              "[{shapeId: 1, kind: 0, layerOverride: 0x10}]",
              "[{shapeId: 1, kind: 0, layerOverride: 1tail}]",
              "[{shapeId: 1, kind: 3, geometryAsset: bad, geometryRevision: 1}]",
              "[{shapeId: 1, kind: 3, geometryAsset: 91ea9b44-13a6-4aec-99ec-0963eef7eaa1, geometryRevision: 0}]",
              "[{shapeId: 1, kind: 0, restitution: 2}]"})
            check(!Editor::ParsePhysicsShapeDocument(input));
        // A prefab shape override is a YAML document stored inside a scalar.
        for (const std::string_view value : {"- shapeId: 44\n  kind: 0\n  halfExtent:\n    x: 0.5\n",
                                            "first\r\n  second\r\n", "quote: \"value\"\n  path: C:\\temp\n"})
        {
            Authoring::WriteDocument document;
            document.Root().SetMap();
            document.Root().Child("m_valueYaml").SetScalar(value);
            std::string error;
            auto restored = Authoring::WriteDocument::ParseText(document.Dump(), &error);
            check(restored.has_value());
            check(restored && restored->Root().Read()["m_valueYaml"].Scalar() == value);
        }

        std::cout << "PHYSICS_SHAPE_DOCUMENT_OK checks=" << checks << "\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
