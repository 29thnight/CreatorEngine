#pragma once

#include "ScriptReflectionService.h"
#include <memory>
#include <type_traits>

namespace ce::script::reflection
{
    namespace scalar_output_detail
    {
        inline std::size_t animator_arity(std::string_view name) noexcept
        {
            if (name == "GetClipCount" || name == "GetSkeletonSerial" || name == "GetBoneCount")
                return 2;
            if (name == "GetClipName" || name == "GetClipFrameCount" || name == "GetClipDuration"
                || name == "GetBoneName" || name == "ResolveBoneIndex" || name == "BuildAvatarBoneMasks")
                return 3;
            return 0;
        }

        template<class T>
        const reflgen::value_descriptor* scalar_pointee(const reflgen::parameter_info& parameter) noexcept
        {
            const auto& pointer = parameter.value();
            if (parameter.passing() != reflgen::parameter_passing::value
                || parameter.type() != reflgen::type_id_of<T*>()
                || parameter.type_name() != reflgen::type_name_of<T*>()
                || pointer.id() != reflgen::type_id_of<T*>()
                || pointer.name() != reflgen::type_name_of<T*>()
                || pointer.size() != sizeof(T*) || pointer.alignment() != alignof(T*)
                || pointer.shape() != reflgen::value_shape::pointer || pointer.children().size() != 1)
                return nullptr;
            // Keep the canonical registration's descriptor. In particular, do
            // not instantiate a local value_descriptor_of<Enum> without its schema.
            const auto& scalar = pointer.children()[0].value();
            constexpr auto shape = std::is_same_v<T, bool>
                ? reflgen::value_shape::boolean : reflgen::value_shape::enumeration;
            if (scalar.id() != reflgen::type_id_of<T>() || scalar.name() != reflgen::type_name_of<T>()
                || scalar.size() != sizeof(T) || scalar.alignment() != alignof(T)
                || scalar.shape() != shape || !scalar.children().empty()
                || !scalar.is_complete() || !scalar.is_serializable() || !scalar.is_assignable())
                return nullptr;
            return &scalar;
        }

        template<class T>
        reflgen::value_view make_scalar(const reflgen::value_descriptor& canonical)
        {
            auto storage = std::make_shared<T>();
            return reflgen::value_view(canonical, storage.get(), storage.get(), storage);
        }
    }

    // The production host installs this only on Animator component roots. The
    // service owns initialization, pointer binding and post-call scalar copyout.
    // This grants exactly the two diagnostic outputs, never a raw buffer.
    template<class Enum>
    void BindAnimatorOutputs(component_view& component)
    {
        static_assert(std::is_enum_v<Enum> && !std::is_const_v<Enum>);
        component.create_output = [](const reflgen::method_info& method, std::size_t index) -> reflgen::value_view
        {
            const auto arity = scalar_output_detail::animator_arity(method.name());
            const auto parameters = method.parameters();
            if (arity == 0 || parameters.size() != arity || index < arity - 2 || index >= arity)
                return {};
            const auto* flag = scalar_output_detail::scalar_pointee<bool>(parameters[arity - 2]);
            const auto* path = scalar_output_detail::scalar_pointee<Enum>(parameters[arity - 1]);
            if (!flag || !path) return {};
            return index == arity - 2 ? scalar_output_detail::make_scalar<bool>(*flag)
                : scalar_output_detail::make_scalar<Enum>(*path);
        };
    }
}
