#pragma once
#include <concepts>
#include <cstddef>
#include <unordered_map>

// 타입 단위 인스펙터 아이콘 지정 (PHASE 21 W2-I4).
//
// ── s&box 가 하는 방식 ───────────────────────────────────────────────────
//
// `ComponentSheetHeader` 는 `Icon = TargetObject.TypeIcon;` 한 줄이고, 그 값은
// 타입에 붙은 `[Icon( "..." )]` 속성에서 온다. 그림을 고르는 것은 타입의
// 선언이지 인스펙터의 분기가 아니다. 지정이 없으면 `"category"` 로 떨어진다
// (`InspectorHeader.OnPaint` 의 `string.IsNullOrEmpty( Icon ) ? "category"`).
//
// ── 왜 `meta::` 속성이 아니라 여기인가 ───────────────────────────────────
//
// ① `meta::schema` 는 **필드·메서드**의 목록이다. 타입 단위 속성을 들이려면
//    `type_schema` 의 인자를 늘려야 하는데, 그것은 리플렉션 서술자의 핵이라
//    아이콘 하나 때문에 건드릴 자리가 아니다.
// ② 아이콘은 **에디터의 어휘**다. 글리프는 폰트에 딸린 것이고 엔진 런타임은
//    폰트를 모른다. 엔진 헤더에 그림 이름을 적으면 런타임이 에디터의 자원을
//    아는 꼴이 된다.
//
// 그래서 `InspectorDrawer<T>` 와 **같은 모양**을 쓴다. 이 저장소에서 이미 선
// 확장 방식이고(정적 라이브러리라 자기 등록자는 링크에서 사라진다), 포크가
// 자기 타입에 그림을 붙일 때 엔진 분기를 건드리지 않는다.
//
// ── 글리프가 아니라 **역할 이름**이다 ────────────────────────────────────
//
// 특수화가 내놓는 것은 `"Camera"` 같은 역할 이름이지 글리프 바이트가 아니다.
// 글리프 표는 폰트 빌드가 생성하는 것이라 심볼 집합이 바뀌면 지정한 자리마다
// 깨진다. 역할 → 글리프 변환은 `InspectorIconList.h` 의 표 하나뿐이라, 폰트가
// 통째로 갈려도 고칠 자리가 한 곳이다.
namespace editor::inspector
{
    // 기본형은 비어 있다 — `Role` 이 없으므로 아래 개념이 거짓이 된다.
    template<class T>
    struct InspectorIcon
    {
    };

    template<class T>
    concept HasInspectorIcon = requires
    {
        { InspectorIcon<T>::Role } -> std::convertible_to<const char*>;
    };

    /// 이 타입이 고른 역할 이름. 지정이 없으면 `nullptr`.
    ///
    /// 이 변수 템플릿을 인스턴스화하는 곳은 `InspectorIconList.h` 의 등록
    /// 함수 하나뿐이고, 그 헤더를 드는 번역 단위도 하나뿐이다. 특수화가 보이는
    /// 곳과 안 보이는 곳에서 같은 `T` 를 물으면 값이 갈리므로, 묻는 자리를
    /// 하나로 가둔 것이다.
    template<class T>
    inline constexpr const char* IconRoleOf = []
    {
        if constexpr (HasInspectorIcon<T>)
        {
            return static_cast<const char*>(InspectorIcon<T>::Role);
        }
        else
        {
            return static_cast<const char*>(nullptr);
        }
    }();

    /// 타입 ID → 글리프. 그리는 쪽은 런타임 타입만 들고 있으므로 표가 필요하다.
    inline std::unordered_map<std::size_t, const char*>& inspector_icon_table()
    {
        static std::unordered_map<std::size_t, const char*> table;
        return table;
    }

    inline void set_inspector_icon(std::size_t typeID, const char* glyph) noexcept
    {
        if (nullptr != glyph && '\0' != glyph[0])
        {
            inspector_icon_table()[typeID] = glyph;
        }
    }

    /// 이 타입이 그릴 글리프. 지정이 없거나 아이콘 폰트가 없으면 `nullptr` —
    /// 그리는 쪽은 칸만 비우고 넘어간다.
    inline const char* inspector_icon(std::size_t typeID) noexcept
    {
        const auto& table = inspector_icon_table();
        const auto it = table.find(typeID);
        return it != table.end() ? it->second : nullptr;
    }
}
