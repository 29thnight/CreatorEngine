#pragma once
// typed 직렬화기 (PHASE 18 CT6-a → reflgen 도입 P5).
//
// 반영 타입의 YAML 은 reflgen 직렬화가 쓰고 읽는다 — Authoring 백엔드(ReflgenAuthoring.h: WriteNode·ReadNode 위의
// reflgen::writer·reader)와 엔진 serializer 특수화(ReflgenAuthoringSerializers.h: 봉투·훅·헤더·enum·math·포인터)가
// 옛 typed 직렬화기의 형식 계약을 바이트 그대로 낸다. 이 파일에 남은 것은 그 계약의 조각이다:
//   - 스칼라 판정(YamlScalar)과 스칼라 reader(ReadScalar — 저작 자산 코드가 직접 쓴다).
//   - 맵 키 계약(YamlMapKey·EncodeMapKey)과 컨테이너 형상 판정(SerializedAsSequence·SerializedAsKeyedRange).
//   - 타입 계열 판정(컴포넌트·엔티티 — reflgen 서술의 이름과 부모 체인으로).
//   - 역직렬화 후처리 훅의 디스패치 표(TypeOps·RegisterOps).
//
// 레거시 본문(멤버 방출 EmitMember·ReadMember 와 스칼라 emitter, 엔진 스키마의 필드 순회 위의 객체 본문)은
// 엔진 스키마와 함께 걷었다 — 모든 반영 타입이 reflgen 서술이다.
//
// ── range 일반화 (컨테이너 축) ────────────────────────────────────────────
//
// 컨테이너 판정이 `is_vector_v` 에서 형상 판정(ReflectionContainer.h)으로 옮겨
// 왔다. **레거시 파리티는 그대로다** — std::vector 가 지나던 길을 한 글자도
// 바꾸지 않고, 그 길을 다른 컨테이너도 지나게 했을 뿐이다:
//
//   시퀀스 (vector·deque·list·set·array) → YAML 시퀀스, 빈 것은 `~`
//   맵     (map·unordered_map)           → YAML 맵,   빈 것은 `~`
//
// 빈 표기를 `~` 로 **모든 시퀀스에 같게** 거는 것이 계약이다. 빈 vector 만 `~`
// 고 빈 set 은 `[]` 가 되면, 같은 자리에서 컨테이너를 바꾸는 것만으로 파일
// 형상이 갈린다.
//
// 이 이행에서 닫힌 구멍 셋(전부 레거시부터 있던 것):
//   ① 시퀀스 원소 표기가 멤버 표기와 따로 자라 math::rect·math::color 갈래가
//      없었다 — `std::vector<math::rect>` 는 필드로 실으면 컴파일이 깨졌다.
//   ② 시퀀스 원소에 enum 갈래가 없었다 — `std::vector<LightType>` 은 저장
//      시점에 런타임 로그만 남기고 값을 통째로 잃었다.
//   ③ 원소가 미지원일 때 런타임 로그로 넘어가던 자리를 C1 과 같은 기준으로
//      static_assert 로 올렸다.
//
// 맵의 키는 값보다 좁다(YamlMapKey): 복합 스칼라는 한 문장으로 안 접히고,
// 부동소수는 표기 왕복이 정밀도에 걸려 키가 조용히 갈린다. 포인터를 값으로 갖는
// 맵은 거부한다 — 시퀀스의 포인터 원소는 SceneManager 가 복원하지만 맵에는 그
// 복원자가 없어 "적기만 하고 못 읽는" 한쪽 방향이 된다.
#include "ReflectionYml.h"
#include "AuthoringNodeViewAccess.h" // D3-a-4
#include "AuthoringScalarConvert.h" // D3-b-2b-1a
#include "AuthoringReadNode.h" // D3-b-2b-1b
#include <limits>
#include "ReflectionMeta.h"
#include "ReflectionContainer.h" // range 일반화 — is_vector_v 를 대신하는 형상 판정
#include <mathematics/color.hpp>
#include <mathematics/rect.hpp>
#include <mathematics/matrix4x4.hpp>
#include <mathematics/quaternion.hpp>
#include <mathematics/vector2.hpp>
#include <mathematics/vector3.hpp>
#include <mathematics/vector4.hpp>

namespace Meta::Typed
{
    // 스칼라로 적히는 타입 — reflgen 쪽 표기는 ReflgenAuthoringSerializers.h(math·HashingString 등)와 reflgen 기본
    // 규칙(산술·문자열)이 낸다. 컨테이너 판정보다 앞선다(아래 순서 계약).
    //
    // 정확 타입 목록 — 오버로드 가시성(requires{EmitScalar(...)})으로 정의하면
    // 비스코프드 enum이 HashedGuid(size_t) 비명시 생성자로 암묵 변환돼 스칼라로
    // 오판된다(실측: LightType). 레거시 테이블 23종과 동일 집합.
    template<class T>
    concept YamlScalar =
        (std::is_arithmetic_v<T> && !std::is_enum_v<T>)
        || std::is_same_v<T, std::string>
        || std::is_same_v<T, HashingString>
        || std::is_same_v<T, HashedGuid>
        || std::is_same_v<T, file::path>
        || std::is_same_v<T, FileGuid>
        || std::is_same_v<T, math::vector2>
        || std::is_same_v<T, math::vector3>
        || std::is_same_v<T, math::vector4>
        || std::is_same_v<T, math::rect>
        || std::is_same_v<T, math::color>
        || std::is_same_v<T, math::quaternion>
        || std::is_same_v<T, math::matrix4x4>;

    // ── 스칼라 reader — FromYamlScalar 특수화의 typed 등가물 ───────────────

    // -- D3-b-2b-1a: 산술 변환을 backend에서 뗀다 --
    //
    // 값 변환은 `Authoring::Scalar`(문자열 위의 함수)가 하고, 노드는 원문을
    // 꺼내는 데만 쓴다. D3-b-2b-1b가 backend를 바꿔도 **값의 의미가 그대로**인
    // 이유가 이것이다 — 파서만 바꾸면 `010`이 8에서 10이 되고 `1.5x`가 실패에서
    // 1.5가 되는 식으로 조용히 갈린다(실측 21건).
    //
    // ★ 실패 경로도 `ReadNode::As<T>()`가 소유한다. 문자열 변환이 실패하면 backend
    //   예외에 기대지 않고 저작 경계의 일정한 `runtime_error`로 보고한다.
    namespace ScalarDetail
    {
		// 변환기를 태우고 실패 표현까지 ReadNode 경계에서 통일한다.
		template<class T>
		inline T ConvertOrThrow(const Authoring::ReadNode& n)
		{
			return n.As<T>();
		}

        inline std::string StringOf(const Authoring::ReadNode& n)
        {
			return n.AsStringChecked();
        }
    }

    // ★ `char` 계열은 일반 산술 변환기와 의미가 다르므로 별도 경로를 유지한다.
    template<class T>
        requires (std::is_arithmetic_v<T> && !std::is_enum_v<T>
            && !std::is_same_v<std::remove_cv_t<T>, char>
            && !std::is_same_v<std::remove_cv_t<T>, signed char>
            && !std::is_same_v<std::remove_cv_t<T>, unsigned char>)
    inline void ReadScalar(const Authoring::ReadNode& n, T& out)
    {
		out = n.As<T>();
    }

    template<class T>
        requires (std::is_same_v<std::remove_cv_t<T>, char>
            || std::is_same_v<std::remove_cv_t<T>, signed char>
            || std::is_same_v<std::remove_cv_t<T>, unsigned char>)
	inline void ReadScalar(const Authoring::ReadNode& n, T& out) { out = n.As<T>(); }

    inline void ReadScalar(const Authoring::ReadNode& n, std::string& out) { out = ScalarDetail::StringOf(n); }
    inline void ReadScalar(const Authoring::ReadNode& n, HashingString& out) { out = HashingString(ScalarDetail::StringOf(n)); }
    // 절단 수정: 레거시는 as<uint32_t>였다 — FNV64 값이 오면 BadConversion.
    inline void ReadScalar(const Authoring::ReadNode& n, HashedGuid& out) { out = HashedGuid(ScalarDetail::ConvertOrThrow<size_t>(n)); }
    inline void ReadScalar(const Authoring::ReadNode& n, file::path& out) { out = file::path(ScalarDetail::StringOf(n)); }
    inline void ReadScalar(const Authoring::ReadNode& n, FileGuid& out) { out = FileGuid(ScalarDetail::StringOf(n)); }

    inline void ReadScalar(const Authoring::ReadNode& n, math::vector2& out)
    {
        out.x = ScalarDetail::ConvertOrThrow<float>(n["x"]); out.y = ScalarDetail::ConvertOrThrow<float>(n["y"]);
    }

    inline void ReadScalar(const Authoring::ReadNode& n, math::vector3& out)
    {
        out.x = ScalarDetail::ConvertOrThrow<float>(n["x"]); out.y = ScalarDetail::ConvertOrThrow<float>(n["y"]); out.z = ScalarDetail::ConvertOrThrow<float>(n["z"]);
    }

    inline void ReadScalar(const Authoring::ReadNode& n, math::color& out)
    {
        out.r = ScalarDetail::ConvertOrThrow<float>(n["r"]); out.g = ScalarDetail::ConvertOrThrow<float>(n["g"]);
        out.b = ScalarDetail::ConvertOrThrow<float>(n["b"]); out.a = ScalarDetail::ConvertOrThrow<float>(n["a"]);
    }

    inline void ReadScalar(const Authoring::ReadNode& n, math::vector4& out)
    {
        out.x = ScalarDetail::ConvertOrThrow<float>(n["x"]); out.y = ScalarDetail::ConvertOrThrow<float>(n["y"]);
        out.z = ScalarDetail::ConvertOrThrow<float>(n["z"]); out.w = ScalarDetail::ConvertOrThrow<float>(n["w"]);
    }

    inline void ReadScalar(const Authoring::ReadNode& n, math::quaternion& out)
    {
        out.x = ScalarDetail::ConvertOrThrow<float>(n["x"]); out.y = ScalarDetail::ConvertOrThrow<float>(n["y"]);
        out.z = ScalarDetail::ConvertOrThrow<float>(n["z"]); out.w = ScalarDetail::ConvertOrThrow<float>(n["w"]);
    }

    inline void ReadScalar(const Authoring::ReadNode& n, math::matrix4x4& out)
    {
        if (!n.IsSequence() || 16 != n.Size())
        {
            out = math::matrix4x4::identity();
            return;
        }
        for (int row = 0; row < 4; ++row)
            for (int column = 0; column < 4; ++column)
                out.m[row][column] = ScalarDetail::ConvertOrThrow<float>(n.At(static_cast<std::size_t>(row * 4 + column)));
    }

    inline void ReadScalar(const Authoring::ReadNode& n, math::rect& out)
    {
        out.x = ScalarDetail::ConvertOrThrow<float>(n["x"]); out.y = ScalarDetail::ConvertOrThrow<float>(n["y"]);
        out.width = ScalarDetail::ConvertOrThrow<float>(n["width"]); out.height = ScalarDetail::ConvertOrThrow<float>(n["height"]);
    }

    // ── 맵 키 — 값이 아니라 **키 자리**의 계약 ────────────────────────────
    //
    // YAML 맵의 키는 문자열 하나로 접혀야 한다. 그래서 값으로는 실을 수 있는
    // 스칼라라도 키로는 못 쓰는 것이 있다:
    //
    //   · 복합 스칼라(vector3·color·rect·matrix)는 애초에 한 문장으로 안 접힌다.
    //   · 부동소수는 접히긴 하는데 **표기 왕복이 정밀도에 걸린다** — 키가 조용히
    //     갈리면 맵 하나가 통째로 다른 맵이 되고, 그 사고는 값이 틀리는 것보다
    //     훨씬 늦게 발견된다. 접을 수 있다는 이유로 허용하지 않는다.
    //
    // 허용 밖은 소비 측 static_assert 가 선언 시점에 잡는다.
    template<class T>
    concept YamlMapKey =
        std::is_integral_v<T>
        || std::is_enum_v<T>
        || std::is_same_v<std::remove_cv_t<T>, std::string>
        || std::is_same_v<std::remove_cv_t<T>, HashingString>
        || std::is_same_v<std::remove_cv_t<T>, HashedGuid>
        || std::is_same_v<std::remove_cv_t<T>, file::path>
        || std::is_same_v<std::remove_cv_t<T>, FileGuid>;

    // 키의 표기 — 인스펙터가 맵 항목의 이름으로 쓴다(저장 표기는 reflgen 의 키 규칙이 같은 글자를 낸다).
    template<YamlMapKey K>
    inline std::string EncodeMapKey(const K& key)
    {
        using U = std::remove_cv_t<K>;
        if constexpr (std::is_same_v<U, bool>) { return key ? "true" : "false"; }
        else if constexpr (std::is_enum_v<U>)
        {
            return std::to_string(
                static_cast<long long>(static_cast<std::underlying_type_t<U>>(key)));
        }
        else if constexpr (std::is_integral_v<U>) { return std::to_string(key); }
        else if constexpr (std::is_same_v<U, std::string>) { return key; }
        else if constexpr (std::is_same_v<U, HashingString>) { return key.ToString(); }
        else if constexpr (std::is_same_v<U, HashedGuid>) { return std::to_string(key.m_ID_Data); }
        else if constexpr (std::is_same_v<U, file::path>) { return key.string(); }
        else { return key.ToString(); } // FileGuid
    }

    // ── 순서 계약: 스칼라가 range 판정보다 앞선다 ─────────────────────────
    //
    // ReflectionContainer.h 가 std::string·path 를 이미 걸러 내지만, 그 한 겹에
    // 전부를 걸지 않는다. YamlScalar 에 **앞으로 추가될** 어떤 타입이 우연히
    // range 여도(begin/end 를 가진 고정 크기 수학 타입 같은 것) 스칼라로 남아야
    // 한다. 아래 두 콘셉트가 그 우선순위를 타입 판정 자체에 박아 둔다 —
    // if/else 사슬의 줄 순서에 기대면 누군가 분기를 옮기는 순간 무너진다.
    template<class T>
    concept SerializedAsKeyedRange = !YamlScalar<T> && meta::container::KeyedRange<T>;

    template<class T>
    concept SerializedAsSequence = !YamlScalar<T> && meta::container::SequenceRange<T>;

    namespace canary
    {
        // 스칼라 13종이 하나도 range 로 새지 않는다는 것을, 판정하는 자리에서
        // 직접 붙든다. 런타임 게이트는 생성되지 않은 분기를 볼 수 없다.
        static_assert(!SerializedAsSequence<std::string>);
        static_assert(!SerializedAsSequence<file::path>);
        static_assert(!SerializedAsSequence<HashingString>);
        static_assert(!SerializedAsSequence<HashedGuid>);
        static_assert(!SerializedAsSequence<FileGuid>);
        static_assert(!SerializedAsSequence<math::vector2>);
        static_assert(!SerializedAsSequence<math::vector3>);
        static_assert(!SerializedAsSequence<math::vector4>);
        static_assert(!SerializedAsSequence<math::quaternion>);
        static_assert(!SerializedAsSequence<math::color>);
        static_assert(!SerializedAsSequence<math::rect>);
        static_assert(!SerializedAsSequence<math::matrix4x4>);
        static_assert(!SerializedAsSequence<float> && !SerializedAsSequence<int>);

        // 시퀀스와 맵은 서로 배타다 — 한 타입이 둘 다면 분기 순서가 결과를 정한다.
        static_assert(!(SerializedAsSequence<std::vector<int>>
            && SerializedAsKeyedRange<std::vector<int>>));
        static_assert(SerializedAsSequence<std::vector<int>>);

        // 부동소수 키 금지가 실제로 서 있는가. 값으로는 실리는 타입이라
        // "스칼라면 키도 된다"로 새기 쉬운 자리다.
        static_assert(!YamlMapKey<float> && !YamlMapKey<double>);
        static_assert(!YamlMapKey<math::vector3>);
        static_assert(YamlMapKey<std::string> && YamlMapKey<int>);

        // TypeTrait 의 value_type 계보(IsCopyableForProperty)와 여기 range 계보가
        // 같은 답을 내는가. 갈리면 콘솔 세터가 "복사 가능"으로 오판해 any_cast
        // 인스턴스화에서 깨진다 — K2 스테이지 A 에서 실제로 밟은 함정이다.
        // 맵·집합·고정 배열까지 포함한 전수 대조는 reflect.container.roundtrip
        // 프로브가 든다(그쪽 TU 가 <map>/<set>/<array> 를 이미 물고 있다).
        static_assert(!IsCopyableForProperty<std::vector<std::unique_ptr<int>>>(),
            "vector<unique_ptr<T>> 가 복사 가능으로 판정됐다");
        static_assert(IsCopyableForProperty<std::vector<int>>());
        static_assert(IsCopyableForProperty<std::string>());
    }

    // 포인터류의 피지시 타입 — conditional_t는 양팔을 모두 인스턴스화하므로
    // (원시 포인터에 element_type이 없어 hard error) 특수화로 지연 선택한다.
    template<class T> struct Pointee;
    template<class U> struct Pointee<U*> { using type = std::remove_cv_t<U>; };
    template<class U> struct Pointee<std::shared_ptr<U>> { using type = std::remove_cv_t<U>; };
    // K2 스테이지 A: Entity::m_components가 vector<std::unique_ptr<Component>>로
    // 바뀌며 shared_ptr 짝이 필요해졌다 — 삭제자 인자(D)는 무시(항상 default_delete).
    template<class U, class D> struct Pointee<std::unique_ptr<U, D>> { using type = std::remove_cv_t<U>; };
    template<class U> struct Pointee<own::shared_owner<U>> { using type = std::remove_cv_t<U>; };
    template<class U> struct Pointee<own::unique_owner<U>> { using type = std::remove_cv_t<U>; };
    template<class T> using PointeeT = typename Pointee<std::remove_cv_t<T>>::type;

    template<class P>
    inline auto* RawPtrOf(P& p)
    {
        if constexpr (std::is_pointer_v<std::remove_cv_t<P>>)
        {
            return p;
        }
        else if constexpr (requires { p.borrow(); })
        {
            // Borrow only the live lvalue owner; no raw adoption or const removal.
            return p.borrow().unsafe_get();
        }
        else
        {
            return p.get();
        }
    }

    // ── 타입 계열 판정 ─────────────────────────────────────────────────────

    // 이름은 reflgen 서술의 이름(생성기가 적는 한정 이름)이고, 부모는 반영된 직계 부모(direct_bases_t)다.
    template<class T>
    consteval bool IsComponentFamily()
    {
        if constexpr (!reflgen::reflectable<T>)
        {
            return false;
        }
        else if constexpr (reflgen::schema_of<T>.name == std::string_view{ "Component" })
        {
            return true;
        }
        else
        {
            return []<class... Bases>(reflgen::type_list<Bases...>)
            {
                return (IsComponentFamily<Bases>() || ...);
            }(reflgen::direct_bases_t<T>{});
        }
    }

    // 정적 타입이 정확히 Component인가 — 벡터 원소의 실타입 디스패치 판정
    // (레거시: elementTypeID == ComponentTypeID). if constexpr 조건식에 직접
    // 쓰면 && 단락으로도 서술(schema_of<T>) 인스턴스화를 못 피해 분리했다.
    template<class T>
    consteval bool IsComponentExact()
    {
        if constexpr (!reflgen::reflectable<T>)
        {
            return false;
        }
        else
        {
            return reflgen::schema_of<T>.name == std::string_view{ "Component" };
        }
    }

    template<class T>
    consteval bool IsGameObjectType()
    {
        if constexpr (!reflgen::reflectable<T>) { return false; }
        else
        {
            return reflgen::schema_of<T>.name == std::string_view{ "Entity" };
        }
    }

    // ── 런타임 브리지 (타입이 런타임에 정해지는 소비자용) ──────────────────
    // 정의는 ReflectionYml.h 상단(OpsRegistry) — 여기서는 썽크만 만든다.

    // CT6-d: 역직렬화 후처리 — 컴포넌트가 OnDeserialized(node) 또는
    // OnDeserialized()를 선언하면 팩토리 공통 경로가 호출한다(분기 소멸).
    template<class T>
    void PostLoadThunk(void* instance, const Authoring::ReadNode& readNode)
    {
        T& obj = *static_cast<T*>(instance);
		if constexpr (requires { obj.OnDeserialized(Authoring::NodeViewAccess::Make(readNode)); })
		{
			obj.OnDeserialized(Authoring::NodeViewAccess::Make(readNode));
        }
        else if constexpr (requires { obj.OnDeserialized(); })
        {
            obj.OnDeserialized();
        }
    }

    template<class T>
    consteval bool HasPostLoad()
    {
		return requires(T& t, const Authoring::ReadNode& n)
				   { t.OnDeserialized(Authoring::NodeViewAccess::Make(n)); }
			|| requires(T& t) { t.OnDeserialized(); };
    }

    template<class T>
    inline void RegisterOps()
    {
        OpsRegistry()[TypeTrait::GUIDCreator::GetTypeID<T>().m_ID_Data] =
            TypeOps{ HasPostLoad<T>() ? &PostLoadThunk<T> : nullptr, };
    }

}

#include "ReflgenAuthoringSerializers.h"

namespace Meta::Typed
{
    // 컴파일 때 타입을 아는 쓰기·읽기(설정 자산 등). 런타임에 타입이 정해지는 쪽은 서술자(ReflectionYml.h 의
    // SerializeInto·Deserialize)다 — 둘 다 같은 reflgen 직렬화 코드로 간다.
    template<ReflgenRecord T>
    void SerializeObjectInto(T& obj, Authoring::WriteNode node)
    {
        Authoring::ReflgenWriter writer(node);
        reflgen::serialize(writer, obj);
    }

    template<ReflgenRecord T>
    void DeserializeObjectFrom(T& obj, const Authoring::ReadNode& node)
    {
        Authoring::ReflgenReader reader(node);
        reflgen::deserialize(reader, obj);
    }
}
