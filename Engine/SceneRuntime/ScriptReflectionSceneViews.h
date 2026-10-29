#pragma once
// Owner-thread diagnostic/navigation views. These objects remain owned by Scene
// or a recording; a JSON record must never construct or overwrite a service.
#include "CameraSystem.h"
#include "SoundSystem.h"
#include "TransformStore.h"
#include "HierarchyStore.h"
#include "SystemSchedule.h"
#include "ScenePhysicsSimulation.h"
#include "TweenManager.h"
#include "../RenderEngine/TextureFramePins.h"
#include <reflgen/runtime/schema.h>
#include <reflgen/runtime/value_descriptor.h>

namespace reflgen
{
    template<class T> requires (
        std::is_same_v<T, CameraSystem> || std::is_same_v<T, SoundSystem> ||
        std::is_same_v<T, TransformStore> || std::is_same_v<T, HierarchyStore> ||
        std::is_same_v<T, SystemSchedule> || std::is_same_v<T, ScenePhysicsSimulation> ||
        std::is_same_v<T, BasicTweenManager<Scene>> || std::is_same_v<T, TextureFramePins>)
    struct runtime_value_traits<T>
    {
        static constexpr bool serializable = false;
        static constexpr bool deserializable = false;
        static constexpr bool assignment_safe = false;
    };

    template<> struct runtime_schema<CameraSystem>
    {
        using T = CameraSystem;
        static constexpr auto value = schema<T>(method<&T::GetPrimaryCamera>,
            method<&T::GetRegisteredCameras>, method<&T::GetCount>);
    };

    template<> struct runtime_schema<SoundSystem>
    {
        using T = SoundSystem;
        static constexpr auto value = schema<T>(method<&T::ClipKeys>,
            method<&T::ResolveAsset>.parameters("text", "kind"),
            method<&T::WorldScope>, method<&T::PreviewScope>, method<&T::GetCount>);
    };

    template<> struct runtime_schema<TransformStore>
    {
        using T = TransformStore;
        static constexpr auto value = schema<T>(
            field<&T::localMatrix>.with(readonly{}), field<&T::worldMatrix>.with(readonly{}),
            field<&T::dirty>.with(readonly{}), field<&T::worldScale>.with(readonly{}),
            field<&T::worldQuaternion>.with(readonly{}), field<&T::worldPosition>.with(readonly{}),
            field<&T::worldChanged>.with(readonly{}), method<&T::Size>);
    };

    template<> struct runtime_schema<HierarchyStore>
    {
        using T = HierarchyStore;
        static constexpr auto value = schema<T>(method<&T::Size>, method<&T::Revision>,
            method<&T::IsOccupied>.parameters("index"), method<&T::ParentOf>.parameters("index"),
            method<&T::RootOf>.parameters("index"), method<&T::ChildrenOf>.parameters("index"));
    };

    template<> struct runtime_schema<SystemSchedule>
    {
        using T = SystemSchedule;
        static constexpr auto value = schema<T>(method<&T::GetSubscriptionCounts>);
    };

    template<> struct runtime_schema<ScenePhysicsSimulation>
    {
        using T = ScenePhysicsSimulation;
        static constexpr auto value = schema<T>(method<&T::LayerRevision>,
            method<&T::ReadCharacter>.parameters("binding"), method<&T::RenderCharacterPosition>.parameters("binding"),
            method<&T::CharacterHandle>.parameters("binding"), method<&T::Read>.parameters("binding"),
            method<&T::Handle>.parameters("binding"), method<&T::Binding>.parameters("body"),
            method<&T::RenderPose>.parameters("binding"), method<&T::InterpolationAlpha>,
            method<&T::IsRunning>, method<&T::CompletedSteps>, method<&T::DroppedSeconds>);
    };

    template<> struct runtime_schema<BasicTweenManager<Scene>>
    {
        using T = BasicTweenManager<Scene>;
        static constexpr auto value = schema<T>(
            method<static_cast<std::size_t (T::*)() const noexcept>(&T::ActiveCount)>.named("ActiveCount"));
    };

    template<> struct runtime_schema<TextureFramePins>
    {
        using T = TextureFramePins;
        static constexpr auto value = schema<T>(method<&T::Size>, method<&T::Borrow>.parameters("index"),
            method<&T::Owners>);
    };
}
