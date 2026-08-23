#pragma once
#include "TypeTrait.h"
#include "LogSystem.h"
#include <array>
#include <string_view>
#include <cstdlib>

// 컴포넌트 타입의 영속(디스크) UUID 표 (SceneGraphRedesignPlan K1-b).
//
// 지금 디스크의 컴포넌트 헤더는 타입 "이름"이 정본이고, 런타임 typeID도 그
// 이름의 해시일 뿐이다(TypeTrait.h GUIDCreator::GetTypeID — typeid(T).hash_code()
// 32비트 절단). 리네임하면 이름도 typeID도 함께 바뀌어 로드 시 컴포넌트 노드를
// 찾지 못하고 조용히 버려진다(§1.1 "리네임 시 컴포넌트 소실"). 여기 UUID는 손으로
// 박아 리네임에 불변이다 — Uuid::FromName처럼 이름에서 유도하면 "이름이 바뀌면
// 값도 바뀐다"는 점에서 같은 병을 재현하므로 쓰지 않는다(§5 예외 2).
//
// 새 컴포넌트를 추가하면 이 표에 한 줄, LifecycleRegistry::RegisterAllComponents에
// 한 줄을 더한다. 값은 처음 박을 때만 정하고 그 뒤로는 절대 바꾸지 않는다 —
// 디스크의 모든 기록이 이 값을 참조하게 된다.
namespace ComponentTypeUUID
{
    struct Entry
    {
        std::string_view typeName;
        const char*       literal; // Uuid::Parse가 읽는 8-4-4-4-12 표기.
    };

    // LifecycleRegistry.cpp의 RegisterAllComponents와 같은 순서 — 새 타입을
    // 더할 때 두 표가 나란히 있어야 눈으로 대조하기 쉽다.
    inline constexpr std::array<Entry, 32> kTable{ {
        { "Animator",                    "b1144467-8a80-45e8-9a24-eab65e16e2be" },
        { "BehaviorTreeComponent",       "e624092d-c27d-46e6-8d85-e18efd30c903" },
        // E7-b(트랙 E): 저장된 GameObjectType::Bone 판정을 대신하는 마커
        // 컴포넌트. 처음 박는 값이라 앞으로 절대 바꾸지 않는다(파일 상단
        // 주석 — 디스크의 모든 기록이 이 값을 참조하게 된다).
        { "BoneComponent",               "1dcdf030-472b-4f67-a410-a602b52900a1" },
        { "BoxColliderComponent",        "54adcb54-9e43-463a-b1fa-b1cef25fb788" },
        { "CameraComponent",             "de554d88-0db0-40a0-a77d-0bc32b5d9dc7" },
        { "Canvas",                      "47df23f8-62a6-42bc-aa91-a07dd4eff26f" },
        { "CapsuleColliderComponent",    "aa90d64a-ddf5-4ebd-b82a-82d693843630" },
        { "CharacterControllerComponent","c0ef9380-46b3-4c3d-83ca-36900f4ea9bc" },
        { "DecalComponent",              "81770712-8de4-4f23-bbfe-9cb4f60d69d3" },
        { "FoliageComponent",            "10ececa3-9c91-4456-a703-11912b6df9de" },
        { "ImageComponent",              "80bd9dbe-a072-4d67-baf2-fd671c968cf6" },
        { "LightComponent",              "3803b17f-21c6-4a57-b216-985d0c7a3886" },
        { "InvalidScriptComponent",      "029ab702-c78c-4560-aab1-04a48c3d334b" },
        { "RagdollComponent",            "306b82eb-c865-4b89-97b7-f3b69a44688a" },
        { "RectTransformComponent",      "3c81141d-dc63-4152-9c33-1dd29c45da74" },
        { "StateMachineComponent",       "a8688982-9812-48ef-bdd4-ed92690a889f" },
        { "UIComponent",                 "f8526eda-5faa-48bc-9167-40764dc3d685" },
        { "MeshColliderComponent",       "ad4d45d9-15bc-4491-98ac-e916b53989b7" },
        { "MeshRenderer",                "a84ed25a-31de-4289-92a0-546b27cee0ff" },
        { "PlayerInputComponent",        "fb08e9f8-ba18-4444-85db-a042d822bd4c" },
        { "RigidBodyComponent",          "7934f0df-0a5b-43d3-a645-d79e49d3aa82" },
        { "ScriptComponent",             "16beda95-3967-4d95-838b-b9e6694b6609" },
        { "SoundComponent",              "f2441c9e-234b-42cd-8067-2276a3c985fe" },
        { "SphereColliderComponent",     "fc615921-be6a-4f34-9d48-36d9a13e724a" },
        { "SpriteRenderer",              "637e2910-0658-42cd-a58c-7bae908a852c" },
        { "SpriteSheetComponent",        "94f8df82-d32c-4278-ba01-24bf9e494594" },
        { "TerrainComponent",            "8169a2f7-9f8d-4187-b35e-0d1314c1d303" },
        { "TerrainColliderComponent",    "1f2ad305-15f7-4612-97ce-8dea548f19f2" },
        { "TextComponent",               "8b8f7463-847a-4a4d-8362-fc40a2a1d243" },
        // S1-b: Transform이 Component로 승격되며 신규 등록. 처음 박는 값이라
        // 앞으로 절대 바꾸지 않는다(파일 상단 주석 — 디스크의 모든 기록이
        // 이 값을 참조하게 된다).
        { "Transform",                   "5f3a1c9e-7d24-4b6a-9e0d-2c8f451a6b7d" },
        { "UIButton",                    "ca884274-e16c-446d-b008-fd172d774b14" },
        { "VolumeComponent",             "54293993-11f7-47a0-a77f-0f5ef2bd88c7" },
    } };

    // 기동 시 1회: kTable을 파싱해 TypeTrait::ComponentUUIDRegistry(전역 조회 표)에
    // 채운다. 이름 중복·UUID 중복은 둘 다 로드 시 "어느 타입인지 못 정한다"로
    // 이어지므로 여기서 즉시 죽는다 — 조용히 넘어가면 실제로 밟기 전까지 아무도
    // 모른다. 호출은 LifecycleRegistry::RegisterAllComponents에서 한다.
    inline void RegisterAll()
    {
        for (const auto& entry : kTable)
        {
            if (TypeTrait::ComponentUUIDRegistry::FindByName(entry.typeName) != nullptr)
            {
                Debug->LogCritical("ComponentTypeUUID: 이름 중복 등록 - " + std::string(entry.typeName));
                Log::FlushNow();
                std::abort();
            }

            const Uuid::Uuid16 uuid = Uuid::Parse(entry.literal);

            if (const std::string* dupName = TypeTrait::ComponentUUIDRegistry::FindNameByUUID(uuid))
            {
                Debug->LogCritical("ComponentTypeUUID: UUID 중복 - " + *dupName
                    + " / " + std::string(entry.typeName));
                Log::FlushNow();
                std::abort();
            }

            TypeTrait::ComponentUUIDRegistry::Register(entry.typeName, uuid);
        }
    }
}
