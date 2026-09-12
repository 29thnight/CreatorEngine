#pragma once
// 타입 → 아이콘 지정 목록 (PHASE 21 W2-I4).
//
// **포크가 고치는 파일은 여기 하나다.** 자기 타입에 그림을 붙이려면 아래
// `ICON` 줄을 하나 더한다. 엔진 분기도, 인스펙터 코드도 건드리지 않는다.
//
// 이 헤더를 드는 번역 단위는 `InspectorWindow.cpp` 하나다. 특수화가 보이는
// 곳과 안 보이는 곳이 갈리면 `IconRoleOf<T>` 의 값이 번역 단위마다 달라지므로,
// 묻는 자리를 여기로 가뒀다.

#include <cstring>

#include "InspectorIcon.h"
#include "RegisterReflectManual.h"

// 글리프 표는 다른 계통이 만드는 중이다(`Tools/fonts/build-editor-symbol-font.py`
// 가 생성하는 `EditorIcons.h`). 아직 트리에 없을 수 있으므로 있으면 쓰고 없으면
// 역할을 전부 `nullptr` 로 흘린다 — 아이콘 없이도 패널은 칸만 비우고 제대로
// 그려진다. 폰트가 없는데 글리프 바이트를 넘기면 네모가 그려지는데, 그 침묵한
// 실패는 이 저장소가 이미 겪었다.
#if __has_include("EditorIcons.h")
#include "EditorIcons.h"
#define CREATOR_HAS_EDITOR_ICONS 1
#else
#define CREATOR_HAS_EDITOR_ICONS 0
#endif

namespace editor::inspector
{
    /// 역할 이름 → 글리프. 모르는 역할이면 `nullptr`.
    ///
    /// 표가 한 곳인 이유. 역할은 뜻("이건 카메라다")이고 글리프는 폰트의
    /// 사정이다. 지정한 자리마다 글리프를 적으면 폰트를 갈 때 전부 따라
    /// 고쳐야 하고, 고치다 빠뜨린 자리는 조용히 네모가 된다.
    inline const char* inspector_icon_glyph(const char* role) noexcept
    {
#if CREATOR_HAS_EDITOR_ICONS
        if (nullptr == role)
        {
            return nullptr;
        }

        struct entry { const char* role; const char* glyph; };
        static constexpr entry kTable[] = {
            { "Camera",        EditorIcon::Camera },
            { "Model",         EditorIcon::Model },
            { "Texture",       EditorIcon::Texture },
            { "Material",      EditorIcon::Material },
            { "Terrain",       EditorIcon::Terrain },
            { "Audio",         EditorIcon::Audio },
            { "Script",        EditorIcon::Script },
            { "Volume",        EditorIcon::Volume },
            { "Font",          EditorIcon::Font },
            { "Layers",        EditorIcon::Layers },
            { "Appearance",    EditorIcon::Appearance },
            { "AvatarMask",    EditorIcon::AvatarMask },
            { "Graph",         EditorIcon::Hierarchy },
            { "Bounds",        EditorIcon::Orthographic },
            { "Grid",          EditorIcon::Grid },
            { "Move",          EditorIcon::Move },
            { "Select",        EditorIcon::Select },
            { "HDR",           EditorIcon::HDR },
            { "Prefab",        EditorIcon::Prefab },
            { "Scene",         EditorIcon::Scene },
            { "AssetBundle",   EditorIcon::AssetBundle },
            { "Error",         EditorIcon::Error },
            // 지정이 없는 타입이 떨어지는 자리. s&box 의 `"category"` 몫이다.
            { "Component",     EditorIcon::GameObject },
        };

        for (const entry& item : kTable)
        {
            if (0 == std::strcmp(item.role, role))
            {
                return item.glyph;
            }
        }
#else
        (void)role;
#endif
        return nullptr;
    }
}

// 한 타입에 역할 하나. 이름은 위 표의 왼쪽 열에서 고른다.
#define CREATOR_INSPECTOR_ICON(Type, RoleName)                     \
    template<>                                                     \
    struct editor::inspector::InspectorIcon<Type>                  \
    {                                                              \
        static constexpr const char* Role = RoleName;              \
    };

// ── 공간 ────────────────────────────────────────────────────────────────
CREATOR_INSPECTOR_ICON(Transform, "Move")
CREATOR_INSPECTOR_ICON(RectTransformComponent, "Move")

// ── 렌더 ────────────────────────────────────────────────────────────────
CREATOR_INSPECTOR_ICON(CameraComponent, "Camera")
CREATOR_INSPECTOR_ICON(Camera, "Camera")
CREATOR_INSPECTOR_ICON(MeshRenderer, "Model")
CREATOR_INSPECTOR_ICON(Mesh, "Model")
CREATOR_INSPECTOR_ICON(SpriteRenderer, "Texture")
CREATOR_INSPECTOR_ICON(SpriteSheetComponent, "Texture")
CREATOR_INSPECTOR_ICON(DecalComponent, "Appearance")
CREATOR_INSPECTOR_ICON(LightMapping, "HDR")
CREATOR_INSPECTOR_ICON(Material, "Material")

// ── 지형·식생 ───────────────────────────────────────────────────────────
CREATOR_INSPECTOR_ICON(TerrainComponent, "Terrain")
CREATOR_INSPECTOR_ICON(TerrainColliderComponent, "Terrain")
CREATOR_INSPECTOR_ICON(FoliageComponent, "Terrain")

// ── 물리 ────────────────────────────────────────────────────────────────
CREATOR_INSPECTOR_ICON(BoxColliderComponent, "Bounds")
CREATOR_INSPECTOR_ICON(SphereColliderComponent, "Bounds")
CREATOR_INSPECTOR_ICON(CapsuleColliderComponent, "Bounds")
CREATOR_INSPECTOR_ICON(MeshColliderComponent, "Bounds")
CREATOR_INSPECTOR_ICON(Navigation, "Grid")

// ── 애니메이션 ──────────────────────────────────────────────────────────
CREATOR_INSPECTOR_ICON(Animator, "AvatarMask")
CREATOR_INSPECTOR_ICON(AnimationController, "AvatarMask")
CREATOR_INSPECTOR_ICON(AvatarMask, "AvatarMask")
CREATOR_INSPECTOR_ICON(BoneComponent, "AvatarMask")
CREATOR_INSPECTOR_ICON(RagdollComponent, "AvatarMask")

// ── 그래프 계통 ─────────────────────────────────────────────────────────
CREATOR_INSPECTOR_ICON(BehaviorTreeComponent, "Graph")
CREATOR_INSPECTOR_ICON(StateMachineComponent, "Graph")

// ── 소리·스크립트 ───────────────────────────────────────────────────────
CREATOR_INSPECTOR_ICON(SoundComponent, "Audio")
CREATOR_INSPECTOR_ICON(ScriptComponent, "Script")
CREATOR_INSPECTOR_ICON(InvalidScriptComponent, "Error")

// ── UI ──────────────────────────────────────────────────────────────────
CREATOR_INSPECTOR_ICON(Canvas, "Layers")
CREATOR_INSPECTOR_ICON(ImageComponent, "Texture")
CREATOR_INSPECTOR_ICON(TextComponent, "Font")
CREATOR_INSPECTOR_ICON(UIButton, "Select")

// ── 후처리·자산 ─────────────────────────────────────────────────────────
CREATOR_INSPECTOR_ICON(VolumeComponent, "Volume")
CREATOR_INSPECTOR_ICON(VolumeProfile, "Volume")
CREATOR_INSPECTOR_ICON(AssetBundle, "AssetBundle")
CREATOR_INSPECTOR_ICON(Prefab, "Prefab")
CREATOR_INSPECTOR_ICON(Scene, "Scene")

namespace editor::inspector
{
    /// 등록 정본(`REFLECT_TYPE_LIST`)을 한 번 훑어 표를 채운다.
    ///
    /// 지정이 없는 타입은 `"Component"` 로 떨어진다 — s&box 가 빈 아이콘을
    /// `"category"` 로 받는 것과 같다. 아이콘이 아예 없는 줄과 "아직 안 정한
    /// 줄" 이 같아 보이면 지정이 빠진 것을 눈치챌 수 없다.
    inline void register_inspector_icons()
    {
#define CREATOR_REGISTER_ICON_ONE(T)                                        \
        {                                                                   \
            const char* role = IconRoleOf<T>;                               \
            const char* glyph = inspector_icon_glyph(                       \
                nullptr != role ? role : "Component");                      \
            set_inspector_icon(                                             \
                TypeTrait::GUIDCreator::GetTypeID<T>().m_ID_Data, glyph);   \
        }
        REFLECT_TYPE_LIST(CREATOR_REGISTER_ICON_ONE)
#undef CREATOR_REGISTER_ICON_ONE
    }
}
