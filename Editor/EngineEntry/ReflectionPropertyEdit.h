#pragma once
#include "EditorObjectOperations.h"
#include "ReflectionUndo.h"
#include "Component.h"
#include "ReflectionYml.h"
struct PhysicsShapeDefinition;
namespace Meta::TypedDraw
{
    // 멤버 변경을 언두 스택에 싣는다 — 레거시 MakePropChangeCommand 등가.
    template<class Owner, class MemberT, auto MP>
    inline bool CommitMemberChange(Owner* obj, const MemberT& prevValue, const MemberT& newValue, const char* field)
    {
        // Shape drafts can move or disappear on Add/Remove/Reload. Never
        // retain their addresses in Undo; Apply Shapes publishes the list.
        if constexpr (std::is_same_v<Owner, PhysicsShapeDefinition>)
        { obj->*MP = newValue; return true; }
        if (Meta::g_inlinePropertyEditDepth) { obj->*MP = newValue; return true; }

        if constexpr (std::is_base_of_v<Component, Owner>)
        {
            const auto* type = Meta::Find(obj->GetTypeID());
            if (obj->GetOwner() && type)
            {
                obj->*MP = prevValue;
                auto before = Meta::SerializeDocument(obj, *type);
                obj->*MP = newValue;
                return EditorObjectOperations::CommitProperty(*obj, field, std::move(before));
            }
        }
        Meta::MakeCustomChangeCommand(
            [obj, prevValue]() { obj->*MP = prevValue; },
            [obj, newValue]() { obj->*MP = newValue; });
        return true;
    }

    // Enum/container/nested value widgets mutate in place. One owner document
    // transaction replaces their raw nested Undo closures and supplies notification.
    class ComplexMemberEdit
    {
    public:
        ComplexMemberEdit(Component* owner, const char* field) : owner_(owner), field_(field)
        {
            if (!owner_ || Meta::g_inlinePropertyEditDepth || !owner_->GetOwner()) {owner_=nullptr;return;}
            if (const auto* type=Meta::Find(owner_->GetTypeID()))
            {before_.emplace(Meta::SerializeDocument(owner_,*type));++Meta::g_inlinePropertyEditDepth;}
            else owner_=nullptr;
        }
        ~ComplexMemberEdit(){if(owner_)--Meta::g_inlinePropertyEditDepth;}
        void Finish()
        {
            if(!owner_)return;
            auto* owner=owner_;owner_=nullptr;--Meta::g_inlinePropertyEditDepth;
            EditorObjectOperations::CommitProperty(*owner,field_,std::move(*before_));
        }
    private:
        Component* owner_{};const char* field_{};
        std::optional<Authoring::WriteDocument> before_;
    };

}
