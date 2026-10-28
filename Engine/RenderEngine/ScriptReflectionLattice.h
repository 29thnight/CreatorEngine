#pragma once
// Lattice is an independent native project. Its immutable material products are
// described at the engine registration boundary so Lattice does not generate a
// second copy of the same schemas or acquire a scripting dependency.
#include "../../Lattice/Material/LXMaterialCompiler.h"
#include <reflgen/runtime/schema.h>

namespace reflgen
{
    template<> struct runtime_schema<LX::LXMaterialSource>
    {
        using T = LX::LXMaterialSource;
        static constexpr auto value = schema<T>(field<&T::scope>, field<&T::node>, field<&T::pin>,
            field<&T::instances>, field<&T::property>);
    };
    template<> struct runtime_schema<LX::LXMaterialParameter>
    {
        using T = LX::LXMaterialParameter;
        static constexpr auto value = schema<T>(field<&T::id>, field<&T::identifier>, field<&T::name>,
            field<&T::type>, field<&T::value>, field<&T::colorSpace>, field<&T::exposed>);
    };
    template<> struct runtime_schema<LX::LXMaterialResource>
    {
        using T = LX::LXMaterialResource;
        static constexpr auto value = schema<T>(field<&T::kind>, field<&T::slot>, field<&T::parameter>,
            field<&T::reference>, field<&T::colorSpace>, field<&T::source>);
    };
    template<> struct runtime_schema<LX::LXMaterialSourceRange>
    {
        using T = LX::LXMaterialSourceRange;
        static constexpr auto value = schema<T>(field<&T::firstLine>, field<&T::lastLine>, field<&T::source>);
    };
    template<> struct runtime_schema<LX::LXMaterialExecutionRequirements>
    {
        using T = LX::LXMaterialExecutionRequirements;
        static constexpr auto value = schema<T>(field<&T::features>, field<&T::forward>, field<&T::refraction>,
            field<&T::subsurface>, field<&T::volume>, field<&T::error>);
    };
    template<> struct runtime_schema<LX::LXMaterialProgram>
    {
        using T = LX::LXMaterialProgram;
        static constexpr auto value = schema<T>(field<&T::slang>, field<&T::semanticKey>, field<&T::features>,
            field<&T::surface>, field<&T::volume>, field<&T::textureSamples>, field<&T::parameters>,
            field<&T::resources>, field<&T::sourceMap>, field<&T::execution>);
    };
    template<> struct runtime_schema<LX::LXMaterialShaderArtifact>
    {
        using T = LX::LXMaterialShaderArtifact;
        static constexpr auto value = schema<T>(field<&T::backend>, field<&T::entryPoint>, field<&T::bytecode>);
    };
}
