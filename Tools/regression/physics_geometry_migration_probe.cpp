#include "../../Engine/SceneRuntime/CollisionGeometryCodec.h"
#include "../../Engine/Physics/PhysicsScene.h"
#include <fstream>
#include <iostream>
#include <iterator>

int main(int argc, char** argv)
{
    using namespace ce::physics;
    try
    {
        if (argc != 2) return 2;

        std::ifstream input(argv[1], std::ios::binary);
        if (!input) throw std::runtime_error("Geometry source missing");

        const std::vector<char> raw((std::istreambuf_iterator<char>(input)), {});
        const auto bytes = std::as_bytes(std::span(raw));
        auto source = CollisionGeometryCodec::Decode(bytes);
        if (!source) throw std::runtime_error("Native geometry decode rejected source");

        auto scene = PhysicsScene::create();
        if (!scene) throw std::runtime_error("Physics initialization failed");

        auto blob = std::visit([&](const auto& form) {
            using T = std::remove_cvref_t<decltype(form)>;
            if constexpr (std::same_as<T, convex_source>)
                return (*scene)->cook_geometry_blob(PhysicsScene::convex_cook_input{form.points});
            else if constexpr (std::same_as<T, triangle_mesh_source>)
                return (*scene)->cook_geometry_blob(PhysicsScene::triangle_cook_input{form.points, form.triangles});
            else
                return (*scene)->cook_geometry_blob(heightfield_desc{form.rows, form.columns, form.heights});
        }, source->form);
        if (!blob) throw std::runtime_error("SDK cook rejected source");

        auto geometry = (*scene)->load_geometry_blob(static_cast<geometry_kind>(source->form.index()), *blob);
        if (!geometry || (*geometry)->kind() != static_cast<geometry_kind>(source->form.index()))
            throw std::runtime_error("SDK cooked import failed");

        std::cout << "{\"result\":\"PHYSICS_GEOMETRY_MIGRATION_NATIVE_OK\",\"revision\":"
                  << source->key.revision << ",\"kind\":" << source->form.index()
                  << ",\"cookedBytes\":" << blob->size() << "}\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
