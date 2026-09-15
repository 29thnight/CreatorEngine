#pragma once
// typed 직렬화기 (PHASE 18 CT6-a) — any/function 이중 타입소거의 소멸 지점.
//
// meta::for_each_field가 멤버를 **실제 타입 T&**로 방문하므로, 레거시
// Property 워크의 프로퍼티당 비용(std::function 간접호출 2회 + std::any 값
// 복사/박싱 + 조회 폴백)이 컴파일타임 if constexpr 트리로 접힌다.
//
// 파리티 계약: 출력은 레거시 Serialize와 **바이트 동등**해야 한다(골든 diff 0).
//   - 스칼라 23종 인코딩은 ReflectionYamlTemplete의 ToYamlScalar 특수화 그대로
//     (HashingString→ToString, HashedGuid→m_ID_Data, path→string, V2/V3/V4/
//     math::color(r,g,b,a)/Quaternion/math::rect는 Flow 스타일 맵, FileGuid→ToString).
//   - Flow 스타일은 **스칼라 원소 벡터에만** 건다(레거시 vecEntry 경로의 관례).
//     객체 원소 벡터는 기본(블록) 스타일.
//   - 컴포넌트 계열(자신 또는 조상 서술이 "Component")은 맨 앞에 실타입
//     헤더(이름: typeID)와 m_typeUUID를 적는다. GameObject는 자기 헤더만.
//   - 미지원 멤버 타입은 "[not support type]" 마커(레거시와 동일 문구).
//   - 역직렬화는 부재 키 스킵, 포인터는 새 인스턴스 생성 후 대입(원시
//     포인터의 구 대상 누수까지 레거시 파리티 — 분석 F-2, E5에서 해소).
//
// 런타임 브리지: 씬 로드처럼 타입이 런타임에 정해지는 소비자는
// Meta::Typed::TypeOps(타입당 함수 포인터 2개)로 디스패치한다 — 등록은
// RegisterReflectManual.h(등록 정본)가 한다. 레거시 Property 워크는
// 미등록 타입의 폴백으로 남고 CT7에서 소멸한다.
//
// 수정 1건(역직렬화 측만, 골든 무영향): HashedGuid를 as<uint32_t>로 읽던
// 절단을 as<size_t>로 — typeID가 FNV64(CT4-b)가 된 뒤로 32비트 초과 값을
// 읽으면 BadConversion이 나는 잠재 로드 버그였다.
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
    // ── 스칼라 emitter — ToYamlScalar 특수화의 typed 등가물 ────────────────

    inline void EmitFlowMap2(Authoring::WriteNode node,
        const char* k0, float v0, const char* k1, float v1)
    {
        node.SetMap(true);
        node.Child(k0).SetScalar(v0);
        node.Child(k1).SetScalar(v1);
    }

    inline void EmitFlowMap3(Authoring::WriteNode node, float x, float y, float z)
    {
        node.SetMap(true);
        node.Child("x").SetScalar(x);
        node.Child("y").SetScalar(y);
        node.Child("z").SetScalar(z);
    }

    inline void EmitFlowMap4(Authoring::WriteNode node,
        float x, float y, float z, float w)
    {
        node.SetMap(true);
        node.Child("x").SetScalar(x);
        node.Child("y").SetScalar(y);
        node.Child("z").SetScalar(z);
        node.Child("w").SetScalar(w);
    }

    // ── 스칼라는 "어느 노드에 적을 것인가" 하나로 통일한다 ────────────────
    //
    // 예전에는 스칼라를 적는 자리가 둘이었다: 멤버용 `EmitScalar(node, name, v)`
    // 와 벡터 원소용 `EmitVectorElement(arrayNode, v)`. 같은 타입의 표기를 두
    // 벌 적고 있었고, 실제로 **한쪽만 자라 있었다** — 원소 쪽에는 math::rect 와
    // math::color 갈래가 없어서 `std::vector<math::rect>` 는 필드로 실으면
    // ryml save 로 떨어져 컴파일이 깨졌다(레거시부터 그랬다).
    //
    // 정본을 "대상 노드에 적는다" 하나로 두면 멤버·시퀀스 원소·맵 값 셋이 같은
    // 표기를 공유한다. 앞의 구멍은 갈래를 더해서가 아니라 **출처가 하나가 되면서**
    // 닫힌다.
    //
    // 기본 산술·문자열 — WriteNode의 canonical scalar writer.
    template<class T>
        requires (std::is_arithmetic_v<T> && !std::is_enum_v<T>)
    inline void EmitScalarInto(Authoring::WriteNode target, const T& v)
    {
        target.SetScalar(v);
    }

    inline void EmitScalarInto(Authoring::WriteNode target, const std::string& v) { target.SetScalar(v); }
    inline void EmitScalarInto(Authoring::WriteNode target, const HashingString& v) { target.SetScalar(v.ToString()); }
    inline void EmitScalarInto(Authoring::WriteNode target, const HashedGuid& v) { target.SetScalar(v.m_ID_Data); }
    inline void EmitScalarInto(Authoring::WriteNode target, const file::path& v) { target.SetScalar(v.string()); }
    inline void EmitScalarInto(Authoring::WriteNode target, const FileGuid& v) { target.SetScalar(v.ToString()); }

    inline void EmitScalarInto(Authoring::WriteNode target, const math::vector2& v)
    {
        EmitFlowMap2(target, "x", v.x, "y", v.y);
    }

    inline void EmitScalarInto(Authoring::WriteNode target, const math::vector3& v)
    {
        EmitFlowMap3(target, v.x, v.y, v.z);
    }

    inline void EmitScalarInto(Authoring::WriteNode target, const math::color& v)
    {
        target.SetMap(true);
        target.Child("r").SetScalar(v.r);
        target.Child("g").SetScalar(v.g);
        target.Child("b").SetScalar(v.b);
        target.Child("a").SetScalar(v.a);
    }

    inline void EmitScalarInto(Authoring::WriteNode target, const math::vector4& v)
    {
        EmitFlowMap4(target, v.x, v.y, v.z, v.w);
    }

    inline void EmitScalarInto(Authoring::WriteNode target, const math::quaternion& v)
    {
        EmitFlowMap4(target, v.x, v.y, v.z, v.w);
    }

    inline void EmitScalarInto(Authoring::WriteNode target, const math::rect& v)
    {
        target.SetMap(true);
        target.Child("x").SetScalar(v.x);
        target.Child("y").SetScalar(v.y);
        target.Child("width").SetScalar(v.width);
        target.Child("height").SetScalar(v.height);
    }

    inline void EmitScalarInto(Authoring::WriteNode target, const math::matrix4x4& v)
    {
        target.SetSequence(true);
        for (int row = 0; row < 4; ++row)
            for (int column = 0; column < 4; ++column)
                target.Append().SetScalar(v.m[row][column]);
    }

    // 멤버 자리 — 키를 열고 그 노드에 정본을 적는다. 예전 오버로드 13벌이 저마다
    // `node.Child(name)`을 열던 자리가 이 한 줄로 접힌다.
    template<class T>
    inline void EmitScalar(Authoring::WriteNode node, const char* name, const T& v)
    {
        EmitScalarInto(node.Child(name), v);
    }

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
        // ★ 널 노드는 문자열 "null"이다. 이것은 변환이 아니라
        //   **노드->원문 추출**의 규칙이며 `ReadNode::Scalar()`가 소유한다.
        inline bool RawScalar(const Authoring::ReadNode& n, std::string& out)
        {
            if (!n) return false;
            // 널 규칙과 스칼라 판정은 단일 읽기 경계가 소유한다.
            const std::string_view raw = n.Scalar();
            if (raw.empty() && !n.IsNull() && !n.IsScalar()) return false;
            out.assign(raw);
            return true;
        }

        // 변환 디스패치는 `Authoring::Scalar`가 소유한다 — 어댑터도 같은 것을
        // 써야 두 경로가 갈리지 않는다.
        template<class T>
        inline bool TryConvert(std::string_view raw, T& out)
        {
            return Authoring::Scalar::TryConvert(raw, out);
        }

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

    // 미지원 타입을 "조용한 유실"이 아니라 컴파일 오류로 만들기 위한 의존 거짓
    // (C1). static_assert(false)를 discarded branch에 직접 두면 구현에 따라
    // 즉시 발화하므로 템플릿 매개변수에 의존시킨다.
    template<class> inline constexpr bool kUnsupportedForYaml = false;

    // 시퀀스의 스칼라 원소 — VectorElementToYaml 파리티(Flow + 개별 push).
    //
    // 표기는 EmitScalarInto 가 소유하고 여기 남은 것은 **Flow 규칙 하나**다:
    // 스칼라 원소 시퀀스만 flow 로 적는다(객체 원소는 블록). 그 한 줄이 이
    // 함수의 존재 이유 전부다.
    template<YamlScalar T>
    inline void EmitSequenceElement(Authoring::WriteNode arrayNode, const T& v)
    {
        arrayNode.SetFlow();
        EmitScalarInto(arrayNode.Append(), v);
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

    /// 실패를 예외가 아니라 값으로 돌려준다 — 키 하나가 깨졌다고 씬 로드를
    /// 통째로 날리지 않기 위해서다. 호출부가 로그를 남기고 그 항목만 건너뛴다.
    template<YamlMapKey K>
    inline bool DecodeMapKey(std::string_view raw, K& out)
    {
        using U = std::remove_cv_t<K>;
        if constexpr (std::is_same_v<U, bool>)
        {
            out = ("true" == raw || "1" == raw);
            return true;
        }
        else if constexpr (std::is_enum_v<U>)
        {
            std::underlying_type_t<U> underlying{};
            if (!ScalarDetail::TryConvert(raw, underlying)) return false;
            out = static_cast<U>(underlying);
            return true;
        }
        else if constexpr (std::is_integral_v<U>)
        {
            return ScalarDetail::TryConvert(raw, out);
        }
        else if constexpr (std::is_same_v<U, std::string>)
        {
            out.assign(raw);
            return true;
        }
        else if constexpr (std::is_same_v<U, HashingString>)
        {
            out = HashingString(std::string(raw));
            return true;
        }
        else if constexpr (std::is_same_v<U, HashedGuid>)
        {
            std::size_t value{};
            if (!ScalarDetail::TryConvert(raw, value)) return false;
            out = HashedGuid(value);
            return true;
        }
        else if constexpr (std::is_same_v<U, file::path>)
        {
            out = file::path(std::string(raw));
            return true;
        }
        else
        {
            // FileGuid::FromString 은 Uuid::Parse 로 던진다. 키 하나의 오타가
            // 로드 전체를 끊지 않도록 여기서 값으로 바꾼다.
            try { out = FileGuid(std::string(raw)); }
            catch (const std::exception&) { return false; }
            return true;
        }
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
    template<class T> using PointeeT = typename Pointee<std::remove_cv_t<T>>::type;

    template<class P>
    inline auto* RawPtrOf(P& p)
    {
        if constexpr (std::is_pointer_v<std::remove_cv_t<P>>) { return p; }
        else { return p.get(); }
    }

    // ── 타입 계열 판정 ─────────────────────────────────────────────────────

    template<class T>
    consteval bool IsComponentFamily()
    {
        if constexpr (!meta::reflectable<T>)
        {
            return false;
        }
        else
        {
            using D = std::remove_cvref_t<decltype(::meta::reflect<T>())>;
            if constexpr (D::identifier == std::string_view{ "Component" })
            {
                return true;
            }
            else if constexpr (D::has_base)
            {
                return IsComponentFamily<typename D::base_type>();
            }
            else
            {
                return false;
            }
        }
    }

    // 정적 타입이 정확히 Component인가 — 벡터 원소의 실타입 디스패치 판정
    // (레거시: elementTypeID == ComponentTypeID). if constexpr 조건식에 직접
    // 쓰면 && 단락으로도 서술자(meta::reflect<T>()) 인스턴스화를 못 피해 분리했다.
    template<class T>
    consteval bool IsComponentExact()
    {
        if constexpr (!meta::reflectable<T>)
        {
            return false;
        }
        else
        {
            using D = std::remove_cvref_t<decltype(::meta::reflect<T>())>;
            return D::identifier == std::string_view{ "Component" };
        }
    }

    template<class T>
    consteval bool IsGameObjectType()
    {
        if constexpr (!meta::reflectable<T>) { return false; }
        else
        {
            using D = std::remove_cvref_t<decltype(::meta::reflect<T>())>;
            return D::identifier == std::string_view{ "Entity" };
        }
    }

    // ── 런타임 브리지 (타입이 런타임에 정해지는 소비자용) ──────────────────
    // 정의는 ReflectionYml.h 상단(OpsRegistry) — 여기서는 썽크만 만든다.

    template<class T> void SerializeThunk(void* instance, Authoring::WriteNode node);
    template<class T> void DeserializeThunk(void* instance, const Authoring::ReadNode& node);

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
            TypeOps{ &SerializeThunk<T>, &DeserializeThunk<T>,
                HasPostLoad<T>() ? &PostLoadThunk<T> : nullptr, };
    }

    // ── Serialize (typed) ──────────────────────────────────────────────────

    template<meta::reflectable T>
    void SerializeObjectInto(T& obj, Authoring::WriteNode node);

    // 멤버 하나의 방출 — 레거시 분기 순서(벡터→포인터→스칼라→enum→구조체→마커) 보존
    template<class V>
    inline void EmitMember(Authoring::WriteNode node, const char* name, V& value)
    {
        using T = std::remove_cv_t<V>;

        if constexpr (SerializedAsSequence<T>)
        {
            using E = meta::container::ValueT<T>;
            const Authoring::WriteNode arrayNode = node.Child(name);
			// yaml-cpp의 기본 Node에 아무 원소도 push하지 않은 뒤 맵에 넣으면
			// `~`가 된다. 빈 시퀀스 `[]`로 바꾸면 골든과 구 reader의 널 의미가
			// 달라지므로 legacy 표기를 명시 보존한다.
			//
			// range 일반화 이후에도 이 규칙은 **모든 시퀀스에 같게** 건다. 빈
			// vector 만 `~`고 빈 set 은 `[]`가 되면, 같은 자리에서 컨테이너를
			// 바꾸는 것만으로 파일 형상이 갈린다.
			if (std::ranges::empty(value))
			{
				arrayNode.SetNull();
				return;
			}
            arrayNode.SetSequence();

            // std::vector<bool> 의 원소 참조는 프록시 prvalue 라 `auto&` 로 못
            // 묶는다. ElementRefT 가 묶을 수 있으면 참조, 아니면 값으로 받는다.
            for (meta::container::ElementRefT<T> elem : value)
            {
                // K2 스테이지 A: is_unique_ptr_v 병기 — Entity::m_components가
                // vector<std::unique_ptr<Component>>가 되며 원소가 이 갈래를
                // 타야 실타입 디스패치(IsComponentExact)로 간다. 없으면 아래
                // meta::reflectable<E>/YamlScalar<E> 어느 쪽에도 안 걸려 마지막
                // else(미지원 타입 에러 로그)로 떨어진다.
                if constexpr (std::is_pointer_v<E> || is_shared_ptr_v<E> || is_unique_ptr_v<E>)
                {
                    using U = PointeeT<E>;
                    auto* p = RawPtrOf(elem);

                    if (nullptr == p)
                    {
                        arrayNode.Append().SetNull();
                    }
                    else if constexpr (IsComponentExact<U>())
                    {
                        // 원소 정적 타입이 Component 그 자체 → 실타입 디스패치
                        // (레거시: elementTypeID==ComponentTypeID → FindTypeByInstance)
                        if (const TypeOps* ops = FindTypeOps(p->GetTypeID().m_ID_Data))
                        {
                            ops->serializeInto(p, arrayNode.Append());
                        }
                        else
                        {
                            arrayNode.Append().SetNull(); // unknown component
                        }
                    }
                    else if constexpr (meta::reflectable<U>)
                    {
                        SerializeObjectInto(*p, arrayNode.Append());
                    }
                    else
                    {
                        Debug::PrintLog(spdlog::level::err, "Serialize: Unsupported sequence element type");
                    }
                }
                else if constexpr (meta::reflectable<E>)
                {
                    static_assert(!meta::container::ConstElementRange<T>,
                        "원소를 const 로만 순회하는 컨테이너(std::set 계열)에는 reflect() "
                        "타입을 담을 수 없다 — 직렬화가 OnBeforeSerialize 훅 때문에 "
                        "비-const 참조를 요구한다. vector/deque/list 를 쓰라.");
                    SerializeObjectInto(elem, arrayNode.Append());
                }
                else if constexpr (YamlScalar<E>)
                {
                    EmitSequenceElement(arrayNode, elem);
                }
                else if constexpr (std::is_enum_v<E>)
                {
                    // 멤버 자리는 enum 을 int 로 적는데 원소 자리에는 그 갈래가
                    // 아예 없었다 — `std::vector<LightType>` 은 저장 시점에
                    // 런타임 에러 로그만 남기고 값을 통째로 잃었다.
                    arrayNode.SetFlow();
                    arrayNode.Append().SetScalar(
                        static_cast<int>(static_cast<std::underlying_type_t<E>>(elem)));
                }
                else
                {
                    // C1 과 같은 이유로 선언 시점에 잡는다. 원소 타입은 필드
                    // 타입에서 정적으로 나오므로 런타임 로그를 기다릴 이유가 없다.
                    static_assert(kUnsupportedForYaml<E>,
                        "직렬화할 수 없는 시퀀스 원소 타입이다. "
                        "YamlScalar에 추가하거나, reflect()를 달거나, 필드 목록에서 빼라.");
                }
            }

        }
        else if constexpr (SerializedAsKeyedRange<T>)
        {
            using K = meta::container::KeyT<T>;
            using M = meta::container::MappedT<T>;

            static_assert(YamlMapKey<K>,
                "맵 키로 쓸 수 없는 타입이다. 키는 문자열 하나로 접혀야 한다 — "
                "복합 스칼라와 부동소수는 키가 될 수 없다(값으로는 실을 수 있다).");
            static_assert(!(std::is_pointer_v<M> || is_shared_ptr_v<M> || is_unique_ptr_v<M>),
                "포인터를 값으로 갖는 맵은 직렬화하지 않는다. 시퀀스의 포인터 원소는 "
                "SceneManager/ComponentFactory 가 복원하지만 맵에는 그 복원자가 없어 "
                "적기만 하고 못 읽는 한쪽 방향이 된다.");

            const Authoring::WriteNode mapNode = node.Child(name);
            if (std::ranges::empty(value))
            {
                mapNode.SetNull(); // 빈 시퀀스와 같은 규칙
                return;
            }
            mapNode.SetMap();

            // 비-const 로 순회한다 — 값이 reflect() 타입이면 직렬화 훅이
            // 비-const 참조를 요구한다(시퀀스 쪽과 같은 이유다).
            for (auto& entry : value)
            {
                const Authoring::WriteNode valueNode =
                    mapNode.Child(EncodeMapKey(entry.first));

                if constexpr (meta::reflectable<M>)
                {
                    SerializeObjectInto(entry.second, valueNode);
                }
                else if constexpr (YamlScalar<M>)
                {
                    EmitScalarInto(valueNode, entry.second);
                }
                else if constexpr (std::is_enum_v<M>)
                {
                    valueNode.SetScalar(
                        static_cast<int>(static_cast<std::underlying_type_t<M>>(entry.second)));
                }
                else
                {
                    static_assert(kUnsupportedForYaml<M>,
                        "직렬화할 수 없는 맵 값 타입이다. "
                        "YamlScalar에 추가하거나, reflect()를 달거나, 필드 목록에서 빼라.");
                }
            }
        }
        else if constexpr (std::is_pointer_v<T> || is_shared_ptr_v<T> || is_unique_ptr_v<T>)
        {
            using U = PointeeT<T>;
            auto* p = RawPtrOf(value);

            if (nullptr != p)
            {
                if constexpr (meta::reflectable<U>)
                {
                    SerializeObjectInto(*p, node.Child(name));
                }
                else
                {
                    node.Child(name).SetNull(); // unknown pointer (레거시 파리티)
                }
            }
            else
            {
                node.Child(name).SetNull(); // nullptr
            }
        }
        else if constexpr (YamlScalar<T>)
        {
            EmitScalar(node, name, value);
        }
        else if constexpr (std::is_enum_v<T>)
        {
            node.Child(name).SetScalar(
                static_cast<int>(static_cast<std::underlying_type_t<T>>(value)));
        }
        else if constexpr (meta::reflectable<T>)
        {
            SerializeObjectInto(value, node.Child(name));
        }
        else
        {
            // C1: 레거시는 여기서 node[name] = "[not support type]"을 적고 넘어갔다.
            // 컴파일도 되고 저장도 성공한 것처럼 보이는데 왕복하면 값이 사라지는
            // 실패였고, 실제로 Skeleton::m_rootTransform이 그렇게 유실되고 있었다
            // (Test1·Test2.creator에 흔적). 이제 선언 시점에 잡는다.
            //
            // 이 오류를 만났다면 셋 중 하나다:
            //   1. 스칼라라면 EmitScalar/ReadScalar 짝을 만들고 YamlScalar에 추가
            //   2. 중첩 구조체라면 그 타입에 reflect()를 달아 meta::reflectable로
            //   3. 저장할 값이 아니라면 reflect()의 meta::field 목록에서 뺀다
            static_assert(kUnsupportedForYaml<T>,
                "직렬화할 수 없는 [[Property]] 필드 타입이다. "
                "YamlScalar에 추가하거나, reflect()를 달거나, 필드 목록에서 빼라.");
        }
    }

    template<meta::reflectable T>
    void SerializeObjectInto(T& obj, Authoring::WriteNode node)
    {
		node.SetMap();

		// U7: 직렬화 직전 파생 참조를 최신 구조로 다시 계산할 수 있는 선택 훅.
		// UIComponent의 Navigation은 런타임 약참조가 정본이고 디스크에는 계층
		// 로컬 경로를 적는다. 에디터에서 링크를 만든 뒤 부모를 옮긴 경우에도
		// 저장 시점의 계층을 반영하려면 필드 순회 전에 이 훅이 필요하다.
		if constexpr (requires(T& value) { value.OnBeforeSerialize(); })
		{
			obj.OnBeforeSerialize();
		}

        // 헤더 — 레거시는 Component 조상 프레임에서 실타입 헤더를 적었다.
        // typed에서는 T가 곧 실타입이므로 맨 앞에서 한 번 적는다(키 순서 동일:
        // 헤더 → m_typeUUID → 부모 프로퍼티들 → 자신 프로퍼티들).
        if constexpr (IsComponentFamily<T>())
        {
            using D = std::remove_cvref_t<decltype(::meta::reflect<T>())>;
            node.Child(D::identifier).SetScalar(
                TypeTrait::GUIDCreator::GetTypeID<T>().m_ID_Data);

            if (const Uuid::Uuid16* uuid =
                TypeTrait::ComponentUUIDRegistry::FindByName(std::string(D::identifier)))
            {
                node.Child(kComponentTypeUUIDKey).SetScalar(Uuid::ToString(*uuid));
            }
        }
        else if constexpr (IsGameObjectType<T>())
        {
            node.Child(GAMEOBJECT_YAML_KEY).SetScalar(
                TypeTrait::GUIDCreator::GetTypeID<T>().m_ID_Data);
        }

        meta::for_each_field(obj, [&](std::string_view memberName, auto& value)
        {
            EmitMember(node, memberName.data(), value);
        });

		// H3: 리플렉션 멤버가 아닌 호환/파생 데이터를 정본 저장소에서 보충하는
		// 선택 훅. Entity는 Scene-owned HierarchyStore의 계층을 기존 YAML 키로
		// 내보낸다. 훅이 없는 타입의 직렬화 형상은 바뀌지 않는다.
		if constexpr (requires(T& value, Authoring::WriteNode& serializedNode)
			{ value.OnAfterSerialize(Authoring::MutableNodeViewAccess::Make(serializedNode)); })
		{
			obj.OnAfterSerialize(Authoring::MutableNodeViewAccess::Make(node));
		}
    }

    // ── Deserialize (typed) ────────────────────────────────────────────────

    template<meta::reflectable T>
    void DeserializeObjectFrom(T& obj, const Authoring::ReadNode& node);

    /// 노드 하나를 원소 하나로 읽는다 — 시퀀스·맵 값이 공유하는 정본.
    template<class E>
    inline void ReadElement(const Authoring::ReadNode& en, E& item)
    {
        if constexpr (YamlScalar<E>)
        {
            ReadScalar(en, item);
        }
        else if constexpr (std::is_enum_v<E>)
        {
            // 멤버 자리와 같은 규칙 — `as<int>` 직결은 backend 의미에 묶인다.
            int raw{};
            item = ScalarDetail::TryConvert(std::string(en.Scalar()), raw)
                ? static_cast<E>(raw) : static_cast<E>(en.As<int>());
        }
        else
        {
            static_assert(meta::reflectable<E> && std::is_default_constructible_v<E>,
                "역직렬화할 수 없는 원소 타입이다. YamlScalar에 추가하거나, "
                "기본 생성 가능한 reflect() 타입으로 만들거나, 필드 목록에서 빼라.");
            DeserializeObjectFrom(item, en);
        }
    }

    // 시퀀스 채우기 — 컨테이너가 **가진 수단**에 맞춰 넣는다.
    //
    //   · push_back 이 있으면 뒤에 붙인다      (vector·deque·list)
    //   · 인자 하나짜리 insert 만 있으면 넣는다 (set·unordered_set)
    //   · 둘 다 없고 자리로 접근되면 덮어쓴다   (std::array)
    //
    // ★ std::array 는 비울 수 없다. 노드가 배열보다 짧으면 남은 자리를 **값
    //   초기화**한다. 앞부분만 갱신하고 뒤를 그대로 두면 같은 파일을 읽은 결과가
    //   대상 객체의 이전 상태에 따라 달라진다 — 프리팹 오버라이드 적용은 실제로
    //   기존 객체 위에 읽으므로 가정이 아니라 실경로다.
    template<class Container, class E>
    inline void FillSequence(Container& value, const Authoring::ReadNode& sub)
    {
        if constexpr (meta::container::Clearable<Container>)
        {
            value.clear();
        }
        if constexpr (meta::container::Reservable<Container>)
        {
            value.reserve(sub.Size());
        }

        if constexpr (meta::container::FixedSizeRange<Container>)
        {
            const std::size_t capacity = std::size(value);
            std::size_t index = 0;
            for (const auto& en : sub)
            {
                if (index >= capacity) break; // 남는 노드는 버린다(크기가 타입이다)
                ReadElement(en, value[index]);
                ++index;
            }
            for (; index < capacity; ++index)
            {
                value[index] = E{};
            }
        }
        else
        {
            for (const auto& en : sub)
            {
                E item{};
                ReadElement(en, item);
                if constexpr (meta::container::BackInsertable<Container>)
                {
                    value.push_back(std::move(item));
                }
                else
                {
                    value.insert(std::move(item));
                }
            }
        }
    }

    template<class V>
    inline void ReadMember(const Authoring::ReadNode& node, const char* name, V& value)
    {
        using T = std::remove_cv_t<V>;
        const Authoring::ReadNode sub = node[name];
        if (!sub)
        {
            return; // 부재 키 스킵 (레거시 파리티)
        }

        if constexpr (std::is_pointer_v<T> || is_shared_ptr_v<T> || is_unique_ptr_v<T>)
        {
            using U = PointeeT<T>;

            // 중첩 if constexpr — &&는 인스턴스화를 단락하지 않아 불완전
            // 포인티(NodeEditor* 등 전방 선언 대상)에서 is_default_constructible이
            // hard error를 낸다(실측).
            if constexpr (meta::reflectable<U>)
            {
            if constexpr (std::is_default_constructible_v<U>)
            {
                if (!sub.IsMap())
                {
                    return;
                }
                // 레거시 파리티: 항상 새 인스턴스를 만들어 대입한다. 원시
                // 포인터의 구 대상은 해제하지 않는다(F-2 — 소유 정리는 E5).
                if constexpr (std::is_pointer_v<T>)
                {
                    U* fresh = new U();
                    DeserializeObjectFrom(*fresh, sub);
                    value = fresh;
                }
                else
                {
                    // 리뷰 HIGH: 소유권을 스마트 포인터로 먼저 넘긴 뒤
                    // 역직렬화한다(구 코드는 new 직후 raw로 들고 있다가 대입
                    // 시점에야 감쌌다 — DeserializeObjectFrom이 던지면 fresh가
                    // 샜다, shared_ptr 분기도 같은 결함이었다). T(shared_ptr<U>
                    // 또는 std::unique_ptr<U>)를 그대로 써서 두 분기를 함께
                    // 고친다 — 예외가 나면 fresh의 소멸자가 자동 해제한다.
                    T fresh(new U());
                    DeserializeObjectFrom(*fresh, sub);
                    value = std::move(fresh);
                }
            }
            }
        }
        else if constexpr (SerializedAsSequence<T>)
        {
            using E = meta::container::ValueT<T>;
            if constexpr (std::is_pointer_v<E> || is_shared_ptr_v<E> || is_unique_ptr_v<E>)
            {
                // 레거시 파리티: 포인터 원소 벡터(컴포넌트 목록)는 리플렉션이
                // 복원하지 않는다 — SceneManager/ComponentFactory의 몫.
                return;
            }
            else
            {
                static_assert(meta::container::FillableSequence<T>,
                    "채울 수단이 없는 시퀀스다 — push_back·insert·인덱스 대입 중 "
                    "하나는 있어야 역직렬화 경로를 세울 수 있다.");

                if (!sub.IsSequence()) { return; }
                FillSequence<T, E>(value, sub);
            }
        }
        else if constexpr (SerializedAsKeyedRange<T>)
        {
            using K = meta::container::KeyT<T>;
            using M = meta::container::MappedT<T>;

            static_assert(YamlMapKey<K>,
                "맵 키로 쓸 수 없는 타입이다(EmitMember 쪽과 같은 계약).");
            static_assert(meta::container::FillableKeyed<T>,
                "insert_or_assign 이 없는 맵이다 — 키로 덮어쓸 수단이 없으면 "
                "역직렬화 경로를 세울 수 없다.");

            if (!sub.IsMap()) { return; }
            value.clear();

            for (const auto entry : sub.Map())
            {
                K key{};
                if (!DecodeMapKey(entry.key.Scalar(), key))
                {
                    // 키 하나가 깨졌다고 나머지를 버리지 않는다. 다만 조용히
                    // 넘어가지도 않는다 — 조용한 유실이 이 파일이 없앤 병이다.
                    Debug::PrintLog(spdlog::level::err, std::string("Deserialize: 맵 키를 읽지 못했다 - ")
                        + name + " / " + std::string(entry.key.Scalar()));
                    continue;
                }

                M item{};
                ReadElement(entry.value, item);
                value.insert_or_assign(std::move(key), std::move(item));
            }
        }
        else if constexpr (YamlScalar<T>)
        {
            ReadScalar(sub, value);
        }
        else if constexpr (std::is_enum_v<T>)
        {
            // enum도 변환기를 태운다 — `as<int>` 직결은 backend 의미에 묶인다.
            int raw{};
            if (ScalarDetail::TryConvert(std::string(sub.Scalar()), raw))
            {
                value = static_cast<T>(raw);
            }
            else
            {
				value = static_cast<T>(sub.As<int>());
            }
        }
        else if constexpr (meta::reflectable<T>)
        {
            DeserializeObjectFrom(value, sub);
        }
        else
        {
            // C1: 저장 쪽(EmitMember)과 같은 기준으로 컴파일 타임에 잡는다.
            // 런타임 로그는 "이미 유실된 뒤"에야 알려 주므로 늦다.
            static_assert(kUnsupportedForYaml<T>,
                "직렬화할 수 없는 [[Property]] 필드 타입이다. "
                "YamlScalar에 추가하거나, reflect()를 달거나, 필드 목록에서 빼라.");
        }
    }

    template<meta::reflectable T>
    void DeserializeObjectFrom(T& obj, const Authoring::ReadNode& node)
    {
        meta::for_each_field(obj, [&](std::string_view memberName, auto& value)
        {
			const bool memberExists = static_cast<bool>(node[memberName.data()]);
            ReadMember(node, memberName.data(), value);
			if (!memberExists) return;

			if constexpr (requires(T& object, std::string_view name,
				Meta::PropertyChangeSource source)
				{ object.OnPropertyChanged(name, source); })
			{
				obj.OnPropertyChanged(memberName, Meta::CurrentPropertyChangeSource());
			}
        });
    }

    // ── 썽크 (런타임 브리지 대상) ──────────────────────────────────────────

    template<class T>
    void SerializeThunk(void* instance, Authoring::WriteNode node)
    {
        SerializeObjectInto(*static_cast<T*>(instance), node);
    }

    template<class T>
    void DeserializeThunk(void* instance, const Authoring::ReadNode& node)
    {
        DeserializeObjectFrom(*static_cast<T*>(instance), node);
    }
}
