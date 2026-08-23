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
//     Color4(r,g,b,a)/Quaternion/Rect는 Flow 스타일 맵, FileGuid→ToString).
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
#include "ReflectionYml.h"
#include "ReflectionMeta.h"

namespace Meta::Typed
{
    // ── 스칼라 emitter — ToYamlScalar 특수화의 typed 등가물 ────────────────

    inline MetaYml::Node MakeFlowMap2(const char* k0, float v0, const char* k1, float v1)
    {
        MetaYml::Node n;
        n.SetStyle(MetaYml::EmitterStyle::Flow);
        n[k0] = v0;
        n[k1] = v1;
        return n;
    }

    // 기본 산술·문자열 — yaml-cpp convert 직결 (레거시 기본 템플릿과 동일 식)
    template<class T>
        requires (std::is_arithmetic_v<T> && !std::is_enum_v<T>)
    inline void EmitScalar(MetaYml::Node& node, const char* name, const T& v) { node[name] = v; }

    inline void EmitScalar(MetaYml::Node& node, const char* name, const std::string& v) { node[name] = v; }
    inline void EmitScalar(MetaYml::Node& node, const char* name, const HashingString& v) { node[name] = v.ToString(); }
    inline void EmitScalar(MetaYml::Node& node, const char* name, const HashedGuid& v) { node[name] = v.m_ID_Data; }
    inline void EmitScalar(MetaYml::Node& node, const char* name, const file::path& v) { node[name] = v.string(); }
    inline void EmitScalar(MetaYml::Node& node, const char* name, const FileGuid& v) { node[name] = v.ToString(); }

    inline void EmitScalar(MetaYml::Node& node, const char* name, const Mathf::Vector2& v)
    {
        node[name] = MakeFlowMap2("x", v.x, "y", v.y);
    }

    inline void EmitScalar(MetaYml::Node& node, const char* name, const Mathf::Vector3& v)
    {
        MetaYml::Node n;
        n.SetStyle(MetaYml::EmitterStyle::Flow);
        n["x"] = v.x; n["y"] = v.y; n["z"] = v.z;
        node[name] = n;
    }

    inline void EmitScalar(MetaYml::Node& node, const char* name, const Mathf::Color4& v)
    {
        MetaYml::Node n;
        n.SetStyle(MetaYml::EmitterStyle::Flow);
        n["r"] = v.x; n["g"] = v.y; n["b"] = v.z; n["a"] = v.w;
        node[name] = n;
    }

    inline void EmitScalar(MetaYml::Node& node, const char* name, const Mathf::Vector4& v)
    {
        MetaYml::Node n;
        n.SetStyle(MetaYml::EmitterStyle::Flow);
        n["x"] = v.x; n["y"] = v.y; n["z"] = v.z; n["w"] = v.w;
        node[name] = n;
    }

    inline void EmitScalar(MetaYml::Node& node, const char* name, const Mathf::Quaternion& v)
    {
        MetaYml::Node n;
        n.SetStyle(MetaYml::EmitterStyle::Flow);
        n["x"] = v.x; n["y"] = v.y; n["z"] = v.z; n["w"] = v.w;
        node[name] = n;
    }

    inline void EmitScalar(MetaYml::Node& node, const char* name, const Mathf::Rect& v)
    {
        MetaYml::Node n;
        n.SetStyle(MetaYml::EmitterStyle::Flow);
        n["x"] = v.x; n["y"] = v.y; n["width"] = v.width; n["height"] = v.height;
        node[name] = n;
    }

    // xMatrix(XMMATRIX) — C1에서 추가. 이 타입이 목록에 없어서
    // Skeleton::m_rootTransform이 저장 때마다 "[not support type]" 문자열로
    // 덮여 왔다(Test1·Test2.creator에 그 흔적이 남아 있었다). 행 우선 16 float
    // 시퀀스로 적는다 — SIMD 레지스터 표현(r[4])이 아니라 XMFLOAT4X4로 내려
    // 적어야 정렬·플랫폼에 의존하지 않는다.
    inline void EmitScalar(MetaYml::Node& node, const char* name, const Mathf::xMatrix& v)
    {
        DirectX::XMFLOAT4X4 m{};
        DirectX::XMStoreFloat4x4(&m, v);
        MetaYml::Node n;
        n.SetStyle(MetaYml::EmitterStyle::Flow);
        for (int r = 0; r < 4; ++r)
            for (int c = 0; c < 4; ++c)
                n.push_back(m.m[r][c]);
        node[name] = n;
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
        || std::is_same_v<T, Mathf::Vector2>
        || std::is_same_v<T, Mathf::Vector3>
        || std::is_same_v<T, Mathf::Color4>
        || std::is_same_v<T, Mathf::Vector4>
        || std::is_same_v<T, Mathf::Quaternion>
        || std::is_same_v<T, Mathf::Rect>
        || std::is_same_v<T, Mathf::xMatrix>;

    // ── 스칼라 reader — FromYamlScalar 특수화의 typed 등가물 ───────────────

    template<class T>
        requires (std::is_arithmetic_v<T> && !std::is_enum_v<T>)
    inline void ReadScalar(const MetaYml::Node& n, T& out) { out = n.as<T>(); }

    inline void ReadScalar(const MetaYml::Node& n, std::string& out) { out = n.as<std::string>(); }
    inline void ReadScalar(const MetaYml::Node& n, HashingString& out) { out = HashingString(n.as<std::string>()); }
    // 절단 수정: 레거시는 as<uint32_t>였다 — FNV64 값이 오면 BadConversion.
    inline void ReadScalar(const MetaYml::Node& n, HashedGuid& out) { out = HashedGuid(n.as<size_t>()); }
    inline void ReadScalar(const MetaYml::Node& n, file::path& out) { out = file::path(n.as<std::string>()); }
    inline void ReadScalar(const MetaYml::Node& n, FileGuid& out) { out = FileGuid(n.as<std::string>()); }

    inline void ReadScalar(const MetaYml::Node& n, Mathf::Vector2& out)
    {
        out.x = n["x"].as<float>(); out.y = n["y"].as<float>();
    }

    inline void ReadScalar(const MetaYml::Node& n, Mathf::Vector3& out)
    {
        out.x = n["x"].as<float>(); out.y = n["y"].as<float>(); out.z = n["z"].as<float>();
    }

    inline void ReadScalar(const MetaYml::Node& n, Mathf::Color4& out)
    {
        out.x = n["r"].as<float>(); out.y = n["g"].as<float>();
        out.z = n["b"].as<float>(); out.w = n["a"].as<float>();
    }

    inline void ReadScalar(const MetaYml::Node& n, Mathf::Vector4& out)
    {
        out.x = n["x"].as<float>(); out.y = n["y"].as<float>();
        out.z = n["z"].as<float>(); out.w = n["w"].as<float>();
    }

    inline void ReadScalar(const MetaYml::Node& n, Mathf::Quaternion& out)
    {
        out.x = n["x"].as<float>(); out.y = n["y"].as<float>();
        out.z = n["z"].as<float>(); out.w = n["w"].as<float>();
    }

    // xMatrix — EmitScalar 짝(C1). 낡은 파일에는 이 자리에 "[not support type]"
    // 문자열이 들어 있다(유실된 값은 복원할 수 없다). 시퀀스가 아니거나 길이가
    // 16이 아니면 항등행렬로 둔다 — 쓰레기 값을 행렬로 읽는 것보다 낫다.
    inline void ReadScalar(const MetaYml::Node& n, Mathf::xMatrix& out)
    {
        if (!n.IsSequence() || 16 != n.size())
        {
            out = DirectX::XMMatrixIdentity();
            return;
        }
        DirectX::XMFLOAT4X4 m{};
        for (int i = 0; i < 16; ++i) { m.m[i / 4][i % 4] = n[i].as<float>(); }
        out = DirectX::XMLoadFloat4x4(&m);
    }

    inline void ReadScalar(const MetaYml::Node& n, Mathf::Rect& out)
    {
        out.x = n["x"].as<float>(); out.y = n["y"].as<float>();
        out.width = n["width"].as<float>(); out.height = n["height"].as<float>();
    }

    // 미지원 타입을 "조용한 유실"이 아니라 컴파일 오류로 만들기 위한 의존 거짓
    // (C1). static_assert(false)를 discarded branch에 직접 두면 구현에 따라
    // 즉시 발화하므로 템플릿 매개변수에 의존시킨다.
    template<class> inline constexpr bool kUnsupportedForYaml = false;

    // 벡터의 스칼라 원소 emitter — VectorElementToYaml 파리티(Flow + 개별 push)
    template<YamlScalar T>
    inline void EmitVectorElement(MetaYml::Node& arrayNode, const T& v)
    {
        arrayNode.SetStyle(MetaYml::EmitterStyle::Flow);
        if constexpr (std::is_same_v<T, HashingString>) { arrayNode.push_back(v.ToString()); }
        else if constexpr (std::is_same_v<T, HashedGuid>) { arrayNode.push_back(v.m_ID_Data); }
        else if constexpr (std::is_same_v<T, file::path>) { arrayNode.push_back(v.string()); }
        else if constexpr (std::is_same_v<T, FileGuid>) { arrayNode.push_back(v.ToString()); }
        else if constexpr (std::is_same_v<T, Mathf::xMatrix>)
        {
            // yaml-cpp가 XMMATRIX를 모르므로 스칼라 경로와 같은 16 float로 편다.
            DirectX::XMFLOAT4X4 m{};
            DirectX::XMStoreFloat4x4(&m, v);
            MetaYml::Node e;
            e.SetStyle(MetaYml::EmitterStyle::Flow);
            for (int r = 0; r < 4; ++r)
                for (int c = 0; c < 4; ++c)
                    e.push_back(m.m[r][c]);
            arrayNode.push_back(e);
        }
        else { arrayNode.push_back(v); }
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

    template<class T> MetaYml::Node SerializeThunk(void* instance);
    template<class T> void DeserializeThunk(void* instance, const MetaYml::Node& node);

    // CT6-d: 역직렬화 후처리 — 컴포넌트가 OnDeserialized(node) 또는
    // OnDeserialized()를 선언하면 팩토리 공통 경로가 호출한다(분기 소멸).
    template<class T>
    void PostLoadThunk(void* instance, const MetaYml::Node& node)
    {
        T& obj = *static_cast<T*>(instance);
        if constexpr (requires { obj.OnDeserialized(node); })
        {
            obj.OnDeserialized(node);
        }
        else if constexpr (requires { obj.OnDeserialized(); })
        {
            obj.OnDeserialized();
        }
    }

    template<class T>
    consteval bool HasPostLoad()
    {
        return requires(T& t, const MetaYml::Node& n) { t.OnDeserialized(n); }
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
    void SerializeObjectInto(T& obj, MetaYml::Node& node);

    // 멤버 하나의 방출 — 레거시 분기 순서(벡터→포인터→스칼라→enum→구조체→마커) 보존
    template<class V>
    inline void EmitMember(MetaYml::Node& node, const char* name, V& value)
    {
        using T = std::remove_cv_t<V>;

        if constexpr (is_vector_v<T>)
        {
            using E = VectorElementTypeT<T>;
            MetaYml::Node arrayNode;

            for (auto& elem : value)
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
                        arrayNode.push_back(MetaYml::Node());
                    }
                    else if constexpr (IsComponentExact<U>())
                    {
                        // 원소 정적 타입이 Component 그 자체 → 실타입 디스패치
                        // (레거시: elementTypeID==ComponentTypeID → FindTypeByInstance)
                        if (const TypeOps* ops = FindTypeOps(p->GetTypeID().m_ID_Data))
                        {
                            arrayNode.push_back(ops->serialize(p));
                        }
                        else
                        {
                            arrayNode.push_back(MetaYml::Node()); // unknown component
                        }
                    }
                    else if constexpr (meta::reflectable<U>)
                    {
                        MetaYml::Node sub;
                        SerializeObjectInto(*p, sub);
                        arrayNode.push_back(sub);
                    }
                    else
                    {
                        Debug->LogError("Serialize: Unsupported vector element type");
                    }
                }
                else if constexpr (meta::reflectable<E>)
                {
                    MetaYml::Node sub;
                    SerializeObjectInto(elem, sub);
                    arrayNode.push_back(sub);
                }
                else if constexpr (YamlScalar<E>)
                {
                    EmitVectorElement(arrayNode, elem);
                }
                else
                {
                    Debug->LogError("Serialize: Unsupported vector element type");
                }
            }

            node[name] = arrayNode;
        }
        else if constexpr (std::is_pointer_v<T> || is_shared_ptr_v<T> || is_unique_ptr_v<T>)
        {
            using U = PointeeT<T>;
            auto* p = RawPtrOf(value);

            if (nullptr != p)
            {
                if constexpr (meta::reflectable<U>)
                {
                    MetaYml::Node sub;
                    SerializeObjectInto(*p, sub);
                    node[name] = sub;
                }
                else
                {
                    node[name] = MetaYml::Node(); // unknown pointer (레거시 파리티)
                }
            }
            else
            {
                node[name] = MetaYml::Node(); // nullptr
            }
        }
        else if constexpr (YamlScalar<T>)
        {
            EmitScalar(node, name, value);
        }
        else if constexpr (std::is_enum_v<T>)
        {
            node[name] = static_cast<int>(static_cast<std::underlying_type_t<T>>(value));
        }
        else if constexpr (meta::reflectable<T>)
        {
            MetaYml::Node sub;
            SerializeObjectInto(value, sub);
            node[name] = sub;
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
    void SerializeObjectInto(T& obj, MetaYml::Node& node)
    {
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
            node[std::string(D::identifier)] =
                TypeTrait::GUIDCreator::GetTypeID<T>().m_ID_Data;

            if (const Uuid::Uuid16* uuid =
                TypeTrait::ComponentUUIDRegistry::FindByName(std::string(D::identifier)))
            {
                node[kComponentTypeUUIDKey] = Uuid::ToString(*uuid);
            }
        }
        else if constexpr (IsGameObjectType<T>())
        {
            node[GAMEOBJECT_YAML_KEY] = TypeTrait::GUIDCreator::GetTypeID<T>().m_ID_Data;
        }

        meta::for_each_field(obj, [&](std::string_view memberName, auto& value)
        {
            EmitMember(node, memberName.data(), value);
        });

		// H3: 리플렉션 멤버가 아닌 호환/파생 데이터를 정본 저장소에서 보충하는
		// 선택 훅. Entity는 Scene-owned HierarchyStore의 계층을 기존 YAML 키로
		// 내보낸다. 훅이 없는 타입의 직렬화 형상은 바뀌지 않는다.
		if constexpr (requires(T& value, MetaYml::Node& serializedNode)
			{ value.OnAfterSerialize(serializedNode); })
		{
			obj.OnAfterSerialize(node);
		}
    }

    // ── Deserialize (typed) ────────────────────────────────────────────────

    template<meta::reflectable T>
    void DeserializeObjectFrom(T& obj, const MetaYml::Node& node);

    template<class V>
    inline void ReadMember(const MetaYml::Node& node, const char* name, V& value)
    {
        using T = std::remove_cv_t<V>;
        const MetaYml::Node& sub = node[name];
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
        else if constexpr (is_vector_v<T>)
        {
            using E = VectorElementTypeT<T>;
            if constexpr (std::is_pointer_v<E> || is_shared_ptr_v<E> || is_unique_ptr_v<E>)
            {
                // 레거시 파리티: 포인터 원소 벡터(컴포넌트 목록)는 리플렉션이
                // 복원하지 않는다 — SceneManager/ComponentFactory의 몫.
                return;
            }
            else if constexpr (YamlScalar<E>)
            {
                if (!sub.IsSequence()) { return; }
                value.clear();
                value.reserve(sub.size());
                for (const auto& en : sub)
                {
                    E item{};
                    ReadScalar(en, item);
                    value.push_back(std::move(item));
                }
            }
            else if constexpr (meta::reflectable<E> && std::is_default_constructible_v<E>)
            {
                if (!sub.IsSequence()) { return; }
                value.clear();
                value.reserve(sub.size());
                for (const auto& en : sub)
                {
                    E item{};
                    DeserializeObjectFrom(item, en);
                    value.push_back(std::move(item));
                }
            }
        }
        else if constexpr (YamlScalar<T>)
        {
            ReadScalar(sub, value);
        }
        else if constexpr (std::is_enum_v<T>)
        {
            value = static_cast<T>(sub.as<int>());
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
    void DeserializeObjectFrom(T& obj, const MetaYml::Node& node)
    {
        meta::for_each_field(obj, [&](std::string_view memberName, auto& value)
        {
            ReadMember(node, memberName.data(), value);
        });
    }

    // ── 썽크 (런타임 브리지 대상) ──────────────────────────────────────────

    template<class T>
    MetaYml::Node SerializeThunk(void* instance)
    {
        MetaYml::Node node;
        SerializeObjectInto(*static_cast<T*>(instance), node);
        return node;
    }

    template<class T>
    void DeserializeThunk(void* instance, const MetaYml::Node& node)
    {
        DeserializeObjectFrom(*static_cast<T*>(instance), node);
    }
}
