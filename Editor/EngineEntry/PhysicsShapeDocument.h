#pragma once
#include "../../Engine/SceneRuntime/PhysicsShapeDefinition.h"
#include "AuthoringParsedDocument.h"
#include "AuthoringScalarConvert.h"
#include <charconv>
#include <unordered_set>

namespace Editor
{
inline ce::physics::result<std::vector<PhysicsShapeDefinition>> ParsePhysicsShapeDocument(std::string_view text)
{
    using namespace ce::physics;
    const auto invalid = [] {
        return std::unexpected(error{error_code::invalid_argument, 0, "Invalid physics shape document"});
    };
    try
    {
        if (text.empty() || text.size() > 16 * 1024 * 1024)
            return invalid();
        std::string failure;
        auto document = Authoring::ParsedDocument::ParseText(std::string(text), failure);
        const auto root = document.Root();
        if (!document || !root.IsSequence() || !root.Size() || root.Size() > 65535)
            return invalid();
        std::vector<PhysicsShapeDefinition> shapes;
        shapes.reserve(root.Size());
        for (const auto node : root)
        {
            if (!node.IsMap() || !node["shapeId"].IsScalar() || !node["kind"].IsScalar())
                return invalid();
            PhysicsShapeDefinition shape;
            std::unordered_set<std::string> seen;
            const auto scalar = [](auto value, auto& out) {
                if (!value.IsScalar() || !Authoring::Scalar::TryConvert(value.AsString(), out))
                    return false;
                if constexpr (std::is_floating_point_v<std::remove_cvref_t<decltype(out)>>)
                    return std::isfinite(out);
                return true;
            };
            const auto decimal = [](auto value, auto& out) {
                if (!value.IsScalar())
                    return false;
                const auto text = value.AsString();
                const auto parsed = std::from_chars(text.data(), text.data() + text.size(), out);
                return parsed.ec == std::errc{} && parsed.ptr == text.data() + text.size();
            };
            const auto parts = [&](auto value, auto& out) {
                if (value.IsSequence())
                {
                    if (value.Size() != out.size())
                        return false;
                    std::size_t index = 0;
                    for (const auto child : value)
                        if (!scalar(child, out[index++]))
                            return false;
                    return true;
                }
                if (!value.IsMap() || value.Size() != out.size())
                    return false;
                constexpr std::array<const char*, 4> names{"x", "y", "z", "w"};
                std::unordered_set<std::string> axes;
                for (const auto pair : value.Map())
                    if (!pair.key.IsScalar() || !axes.insert(pair.key.AsString()).second)
                        return false;
                for (std::size_t index = 0; index < out.size(); ++index)
                    if (!scalar(value[names[index]], out[index]))
                        return false;
                return true;
            };
            for (const auto pair : node.Map())
            {
                if (!pair.key.IsScalar())
                    return invalid();
                const auto key = pair.key.AsString();
                if (!seen.insert(key).second)
                    return invalid();
                const auto value = pair.value;
                bool valid = false;
                if (key == "shapeId")
                    valid = decimal(value, shape.shapeId);
                else if (key == "kind")
                {
                    std::uint8_t kind{};
                    valid = decimal(value, kind) && kind <= 5;
                    shape.kind = static_cast<PhysicsShapeKind>(kind);
                }
                else if (key == "geometryRevision")
                    valid = decimal(value, shape.geometryRevision);
                else if (key == "layerOverride")
                    valid = decimal(value, shape.layerOverride);
                else if (key == "geometryAsset")
                {
                    valid = value.IsScalar();
                    if (valid)
                        shape.geometryAsset = value.AsString();
                }
                else if (key == "radius")
                    valid = scalar(value, shape.radius);
                else if (key == "halfHeight")
                    valid = scalar(value, shape.halfHeight);
                else if (key == "staticFriction")
                    valid = scalar(value, shape.staticFriction);
                else if (key == "dynamicFriction")
                    valid = scalar(value, shape.dynamicFriction);
                else if (key == "restitution")
                    valid = scalar(value, shape.restitution);
                else if (key == "sensor")
                    valid = scalar(value, shape.sensor);
                else if (key == "queryEnabled")
                    valid = scalar(value, shape.queryEnabled);
                else if (key == "localRotation")
                {
                    std::array<float, 4> values{};
                    valid = parts(value, values);
                    shape.localRotation = {values[0], values[1], values[2], values[3]};
                }
                else if (key == "halfExtent" || key == "geometryScale" || key == "localPosition")
                {
                    std::array<float, 3> values{};
                    valid = parts(value, values);
                    math::vector3 vector{values[0], values[1], values[2]};
                    if (key == "halfExtent")
                        shape.halfExtent = vector;
                    else if (key == "geometryScale")
                        shape.geometryScale = vector;
                    else
                        shape.localPosition = vector;
                }
                if (!valid)
                    return invalid();
            }
            if (!shape.geometryAsset.empty())
            {
                Uuid::Uuid16 id;
                if (!Uuid::TryParse(shape.geometryAsset, id) || id.IsNil() ||
                    Uuid::ToString(id) != shape.geometryAsset || !shape.geometryRevision)
                    return invalid();
            }
            shapes.push_back(std::move(shape));
        }
        // Body-specific motion/scale/project and geometry import validation belongs to SetShapes.
        if (auto valid = ValidatePhysicsShapes(shapes, {1, 1, 1}, {}, body_kind::static_body); !valid)
            return std::unexpected(valid.error());
        return shapes;
    }
    catch (const std::bad_alloc&)
    {
        return std::unexpected(error{error_code::out_of_memory, 0, "Shape document allocation failed"});
    }
    catch (...)
    {
        return invalid();
    }
}
} // namespace Editor
