#pragma once
// reflgen 직렬화의 엔진 규칙 (reflgen 도입 P5) — 엔진 YAML 형식 계약 가운데 serializer 가 지는 몫.
//
// ReflectionTypedYml.h 가 계열 판정(IsComponentFamily 등)과 TypeOps 를 정의한 뒤에 include 한다 — 여기의 특수화가 그것들을
// 쓴다. reflgen 이 엔진 타입을 직렬화하는 곳(SerializeObjectInto·DeserializeObjectFrom)보다 먼저 보여야 한다.
//
//   - 반영 클래스 봉투: OnBeforeSerialize → 헤더(컴포넌트 `이름: typeID`·m_typeUUID, Entity `Entity: typeID`) → 필드
//     (reflgen::write_fields) → OnAfterSerialize(같은 맵에 키를 더한다). 읽기는 필드 순서대로 키를 찾아(레거시 순서)
//     레거시의 건너뛰기 규칙을 지키고 OnPropertyChanged 를 부른다.
//   - 스칼라: HashingString·FileGuid 는 문자열, HashedGuid 는 정수, math 값은 한 줄(flow) 맵·시퀀스.
//   - enum 은 정수(underlying) — 이름이 아니다.
//   - 포인터: 정적 타입의 객체로 쓴다(null·반영되지 않은 대상은 `~`). 읽을 때는 맵이면 새 인스턴스를 만든다.
//   - 포인터 원소 시퀀스: 원소마다 위와 같고, 정적 타입이 Component 면 실타입(typeID → TypeOps)으로 쓴다. 읽지 않는다
//     — 컴포넌트·엔티티 목록은 SceneManager·ComponentFactory 가 복원한다.
// 그 밖(std::string·산술·시퀀스·문자열 키 맵·std::array·std::set)은 reflgen 의 기본 규칙이 레거시와 같다.
#include "ReflgenAuthoring.h"

#include <reflgen/serial/serializer.h>

#include <memory>
#include <string>
#include <string_view>
#include <type_traits>
#include <tuple>
#include <utility>

namespace Meta::Typed
{
	// reflgen 이 서술한 엔진 클래스 — 봉투를 엔진이 쓴다.
	template<class T>
	concept ReflgenRecord = std::is_class_v<T> && reflgen::reflectable<T>;

	template<class P>
	concept PointerLike = std::is_pointer_v<P> || is_shared_ptr_v<P> || is_unique_ptr_v<P>
        || is_gc_strong_ref_v<P>;

	template<class C>
	concept PointerSequence = SerializedAsSequence<C> && PointerLike<meta::container::ValueT<C>>;

    // Standard containers may advertise a copy constructor whose body cannot
    // copy an exclusive element. Inspect reflected fields/bases and nested
    // container values before instantiating an immutable authoring snapshot.
    // This checks visible structure, not arbitrary copy-constructor bodies or
    // unreflected members. Resource copy contracts must separately guarantee
    // that mutable hooks cannot mutate shared published children.
    template<class T, class... Seen>
    consteval bool CanSnapshotImmutable()
    {
        using V = std::remove_cvref_t<T>;
        if constexpr ((std::is_same_v<V, Seen> || ...))
        {
            return true; // Recursive value containers already being inspected.
        }
        else if constexpr (!std::is_copy_constructible_v<V> || is_unique_ptr_v<V>)
        {
            return false;
        }
        else if constexpr (PointerLike<V>)
        {
            return true; // Copy the handle, never recursively copy its pointee.
        }
        else if constexpr (ReflgenRecord<V>)
        {
            const bool bases = []<class... Bases>(reflgen::type_list<Bases...>)
            {
                return (CanSnapshotImmutable<Bases, Seen..., V>() && ...);
            }(reflgen::direct_bases_t<V>{});
            const bool fields = std::apply([](const auto&... field)
            {
                return (CanSnapshotImmutable<
                    typename std::remove_cvref_t<decltype(field)>::value_type, Seen..., V>() && ...);
            }, reflgen::schema_of<V>.fields);
            return bases && fields;
        }
        else if constexpr (requires { typename V::value_type; })
        {
            return CanSnapshotImmutable<typename V::value_type, Seen..., V>();
        }
        else if constexpr (requires { typename std::tuple_size<V>::type; })
        {
            return []<std::size_t... I>(std::index_sequence<I...>)
            {
                return (CanSnapshotImmutable<std::tuple_element_t<I, V>, Seen..., V>() && ...);
            }(std::make_index_sequence<std::tuple_size_v<V>>{});
        }
        else
        {
            return true;
        }
    }

	namespace ReflgenDetail
	{
		// 레거시 ReadMember 의 건너뛰기 — 키는 있지만 이 모양이면 읽지 않고 값을 그대로 둔다(비우지도 않는다).
		template<class V>
		bool SkipsMemberRead(const Authoring::ReadNode& sub)
		{
			using T = std::remove_cv_t<V>;
			if constexpr (PointerLike<T>)
			{
				return !sub.IsMap();
			}
			else if constexpr (PointerSequence<T>)
			{
				return true; // 컴포넌트·엔티티 목록은 리플렉션이 복원하지 않는다
			}
			else if constexpr (SerializedAsSequence<T>)
			{
				return !sub.IsSequence();
			}
			else if constexpr (SerializedAsKeyedRange<T>)
			{
				return !sub.IsMap();
			}
			else
			{
				return false;
			}
		}

		// 헤더의 타입 이름 — reflgen 서술의 이름(생성기가 적는 한정 이름)이다. 엔진 typeID 를 만드는 이름과 같은 표기
		// 인지는 RegisterReflectManual.h 가 반영 타입마다 컴파일 때 대조한다.
		template<class T>
		constexpr std::string_view RecordName()
		{
			return reflgen::schema_of<T>.name;
		}

		// 레거시 SerializeObjectInto 와 같은 순서 — 훅 → 헤더 → 필드 → 훅.
		template<class T>
		void WriteRecord(reflgen::writer& out, T& obj)
		{
			if constexpr (requires(T& value) { value.OnBeforeSerialize(); })
			{
				obj.OnBeforeSerialize();
			}

			const Uuid::Uuid16* uuid = nullptr;
			std::size_t header = 0;
			if constexpr (IsComponentFamily<T>())
			{
				uuid = TypeTrait::ComponentUUIDRegistry::FindByName(std::string(RecordName<T>()));
				header = nullptr != uuid ? 2 : 1;
			}
			else if constexpr (IsGameObjectType<T>())
			{
				header = 1;
			}

			const std::size_t count = header + reflgen::serialized_field_count<T>();
			auto* authoring = dynamic_cast<Authoring::ReflgenWriter*>(&out);
			if (nullptr != authoring)
			{
				authoring->BeginRecord(count);
			}
			else
			{
				out.begin_object(count);
			}

			if constexpr (IsComponentFamily<T>())
			{
				out.write_key(RecordName<T>());
				out.write_uint(TypeTrait::GUIDCreator::GetTypeID<T>().m_ID_Data);
				if (nullptr != uuid)
				{
					out.write_key(kComponentTypeUUIDKey);
					out.write_string(Uuid::ToString(*uuid));
				}
			}
			else if constexpr (IsGameObjectType<T>())
			{
				out.write_key(GAMEOBJECT_YAML_KEY);
				out.write_uint(TypeTrait::GUIDCreator::GetTypeID<T>().m_ID_Data);
			}

			reflgen::write_fields(out, obj);

			// 리플렉션 멤버가 아닌 호환 데이터를 같은 맵에 더하는 훅 — 저작 트리를 직접 만진다.
			if constexpr (requires(T& value, Authoring::WriteNode& node)
				{ value.OnAfterSerialize(Authoring::MutableNodeViewAccess::Make(node)); })
			{
				if (nullptr != authoring)
				{
					Authoring::WriteNode node = authoring->CurrentNode();
					obj.OnAfterSerialize(Authoring::MutableNodeViewAccess::Make(node));
				}
			}
			out.end_object();
		}

		using PropertyChangedFn = void (*)(void* self, std::string_view name);

		// ★ 필드마다 실체화되는 것은 ReadRecordField 하나다. 부모의 필드는 부모 타입의 ReadRecordFields 가 맡는다 —
		//   자식 타입마다 부모 필드를 다시 실체화하지 않는다(Debug 코드 생성). 순서는 for_each_field 와 같다.
		template<class Owner, class Field>
		void ReadRecordField(Authoring::ReflgenReader& reader, Owner& owner, const Field& field,
			PropertyChangedFn changed, void* self)
		{
			if constexpr (!Field::template has_attribute<reflgen::transient>())
			{
				const std::string_view key = field.name;
				const Authoring::ReadNode sub = reader.ValueOf(key);
				if (!sub) return;
				if (!SkipsMemberRead<typename Field::value_type>(sub))
				{
					reader.Seek(key);
					reflgen::read_field(reader, owner, key);
				}
				if (nullptr != changed) changed(self, key);
			}
		}

		template<class T>
		void ReadRecordFields(Authoring::ReflgenReader& reader, T& obj, PropertyChangedFn changed, void* self)
		{
			if constexpr (reflgen::reflectable<T>)
			{
				[&]<class... Bases>(reflgen::type_list<Bases...>)
				{
					(ReadRecordFields<Bases>(reader, static_cast<Bases&>(obj), changed, self), ...);
				}(reflgen::direct_bases_t<T>{});
				std::apply([&](const auto&... fields) { (ReadRecordField(reader, obj, fields, changed, self), ...); },
					reflgen::schema_of<T>.fields);
			}
		}

		// 레거시 DeserializeObjectFrom 과 같다 — 필드 순서대로 키를 찾고, 있으면(건너뛰기 규칙이 막지 않으면) 읽고,
		// 있었으면 OnPropertyChanged. 맵이 아닌 노드는 아무것도 읽지 않는다.
		template<class T>
		void ReadRecord(reflgen::reader& in, T& obj)
		{
			auto* authoring = dynamic_cast<Authoring::ReflgenReader*>(&in);
			if (nullptr == authoring)
			{
				in.begin_object();
				for (std::string key; in.next_key(key);)
				{
					if (!reflgen::read_field(in, obj, key)) in.skip_value();
				}
				in.end_object();
				return;
			}
			if (!authoring->PeekNode().IsMap())
			{
				authoring->skip_value();
				return;
			}

			// OnPropertyChanged 는 가장 파생된 객체의 것이다 — 타입마다 한 번 만든 함수 포인터로 부모 필드에서도 부른다.
			PropertyChangedFn changed = nullptr;
			if constexpr (requires(T& object, std::string_view name, Meta::PropertyChangeSource source)
				{ object.OnPropertyChanged(name, source); })
			{
				changed = [](void* self, std::string_view name)
				{
					static_cast<T*>(self)->OnPropertyChanged(name, Meta::CurrentPropertyChangeSource());
				};
			}
			authoring->begin_object();
			ReadRecordFields(*authoring, obj, changed, &obj);
			authoring->end_object();
		}

		// 포인터 하나 — 레거시: 대상이 반영 타입이면 정적 타입의 객체, 아니면(null 포함) `~`.
		template<class U>
		void WritePointee(reflgen::writer& out, U* pointee)
		{
			using P = std::remove_cv_t<U>;
			if constexpr (ReflgenRecord<P>)
			{
				if (nullptr != pointee)
				{
                    if constexpr (std::is_const_v<U>)
                    {
                        // Legacy record hooks may mutate serialization scratch.
                        // Never run them through a published immutable asset.
                        if constexpr (CanSnapshotImmutable<P>())
                        {
                            P snapshot(*pointee);
                            reflgen::serialize(out, snapshot);
                        }
                        else
                        {
                            throw reflgen::serialization_error(
                                "immutable noncopyable record needs a const-safe serializer");
                        }
                    }
                    else
                    {
                        reflgen::serialize(out, *pointee);
                    }
                    return;
				}
			}
			out.write_null();
		}

		// 레거시: 맵이면 새 인스턴스를 만들어 읽고 대입한다(원시 포인터의 옛 대상은 해제하지 않는다 — F-2).
		// 아니면 값을 그대로 둔다.
		template<class Pointer, class U>
		void ReadPointee(reflgen::reader& in, Pointer& value)
		{
			using P = std::remove_cv_t<U>;
			if constexpr (ReflgenRecord<P>)
			{
				if constexpr (gc::managed_type<P>)
                {
                    // Scene/ComponentFactory restores graph identity in its
                    // explicit domain. Never create a second unmanaged object
                    // through a raw or legacy owning-pointer serializer.
                    if (in.peek() == reflgen::value_kind::object)
                    {
                        throw reflgen::serialization_error(
                            "managed graph references require the scene/component loader");
                    }
                }
                else if constexpr (std::is_default_constructible_v<P>)
				{
					if (in.peek() == reflgen::value_kind::object)
					{
						if constexpr (std::is_pointer_v<Pointer>)
						{
							P* fresh = new P();
							reflgen::deserialize(in, *fresh);
							value = fresh;
						}
                        else if constexpr (std::is_same_v<Pointer, own::shared_owner<U>>)
                        {
                            auto fresh = own::make_shared<P>();
                            reflgen::deserialize(in, *fresh);
                            // Convert only the completed object. Failed parsing
                            // leaves the previous value and its owner untouched.
                            value = std::move(fresh);
                        }
                        else if constexpr (std::is_same_v<Pointer, own::unique_owner<U>>)
                        {
                            auto fresh = own::make_unique<P>();
                            reflgen::deserialize(in, *fresh);
                            value = std::move(fresh);
                        }
						else
						{
							Pointer fresh(new P());
							reflgen::deserialize(in, *fresh);
							value = std::move(fresh);
						}
						return;
					}
				}
			}
			in.skip_value();
		}
	}
}

// ── 특수화 ────────────────────────────────────────────────────────────────

template<class T>
	requires Meta::Typed::ReflgenRecord<T>
struct reflgen::serializer<T>
{
	// Legacy mutable record hooks remain here; immutable owning pointer serializers
    // copy reflected values before reaching this adapter, preserving published assets.
	static void write(reflgen::writer& out, const T& value)
	{
		Meta::Typed::ReflgenDetail::WriteRecord(out, const_cast<T&>(value));
	}

	static void read(reflgen::reader& in, T& value) { Meta::Typed::ReflgenDetail::ReadRecord(in, value); }
};

template<class E>
	requires std::is_enum_v<E>
struct reflgen::serializer<E>
{
	static void write(reflgen::writer& out, const E& value)
	{
		out.write_int(static_cast<int>(static_cast<std::underlying_type_t<E>>(value)));
	}

	static void read(reflgen::reader& in, E& value) { value = static_cast<E>(static_cast<int>(in.read_int())); }
};

template<class U>
struct reflgen::serializer<U*>
{
	static void write(reflgen::writer& out, U* const& value) { Meta::Typed::ReflgenDetail::WritePointee(out, value); }
	static void read(reflgen::reader& in, U*& value) { Meta::Typed::ReflgenDetail::ReadPointee<U*, U>(in, value); }
};

template<class U>
struct reflgen::serializer<std::shared_ptr<U>>
{
	static void write(reflgen::writer& out, const std::shared_ptr<U>& value)
	{
		Meta::Typed::ReflgenDetail::WritePointee(out, value.get());
	}

	static void read(reflgen::reader& in, std::shared_ptr<U>& value)
	{
		Meta::Typed::ReflgenDetail::ReadPointee<std::shared_ptr<U>, U>(in, value);
	}
};

template<class U, class D>
struct reflgen::serializer<std::unique_ptr<U, D>>
{
	static void write(reflgen::writer& out, const std::unique_ptr<U, D>& value)
	{
		Meta::Typed::ReflgenDetail::WritePointee(out, value.get());
	}

	static void read(reflgen::reader& in, std::unique_ptr<U, D>& value)
	{
		Meta::Typed::ReflgenDetail::ReadPointee<std::unique_ptr<U, D>, U>(in, value);
	}
};

template<class U>
struct reflgen::serializer<own::shared_owner<U>>
{
    static void write(reflgen::writer& out, const own::shared_owner<U>& value)
    {
        Meta::Typed::ReflgenDetail::WritePointee(out, value.borrow().unsafe_get());
    }
    static void read(reflgen::reader& in, own::shared_owner<U>& value)
    {
        Meta::Typed::ReflgenDetail::ReadPointee<own::shared_owner<U>, U>(in, value);
    }
};

template<class U>
struct reflgen::serializer<own::unique_owner<U>>
{
    static void write(reflgen::writer& out, const own::unique_owner<U>& value)
    {
        Meta::Typed::ReflgenDetail::WritePointee(out, value.borrow().unsafe_get());
    }
    static void read(reflgen::reader& in, own::unique_owner<U>& value)
    {
        Meta::Typed::ReflgenDetail::ReadPointee<own::unique_owner<U>, U>(in, value);
    }
};

template<class U>
struct reflgen::serializer<gc::trace_ref<U>>
{
    static void write(reflgen::writer& out, const gc::trace_ref<U>& value)
    {
        Meta::Typed::ReflgenDetail::WritePointee(out, value.get());
    }
    static void read(reflgen::reader& in, gc::trace_ref<U>&)
    {
        // Identity and ownership are restored only by scene/component loaders.
        in.skip_value();
    }
};

template<class U>
struct reflgen::serializer<gc::root_ref<U>>
{
    static void write(reflgen::writer& out, const gc::root_ref<U>& value)
    {
        Meta::Typed::ReflgenDetail::WritePointee(out, value.get());
    }
    static void read(reflgen::reader& in, gc::root_ref<U>&)
    {
        in.skip_value();
    }
};

template<class C>
	requires Meta::Typed::PointerSequence<C>
struct reflgen::serializer<C>
{
	static void write(reflgen::writer& out, const C& value)
	{
		using U = Meta::Typed::PointeeT<meta::container::ValueT<C>>;
		out.begin_array(std::size(value));
		for (const auto& element : value)
		{
			auto* pointee = Meta::Typed::RawPtrOf(element);
			if (nullptr == pointee)
			{
				out.write_null();
			}
			else if constexpr (Meta::Typed::IsComponentExact<U>())
            {
                if constexpr (std::is_const_v<std::remove_pointer_t<decltype(pointee)>>)
                {
                    // Component serialization dispatches through mutable virtual
                    // hooks. A static copy would slice the derived component and
                    // removing const would violate immutable ownership.
                    throw reflgen::serialization_error(
                        "immutable polymorphic component lists need a const-safe serializer");
                }
                else
                {
                    // 원소 정적 타입이 Component 그 자체 → 실타입(typeID → 등록소의 서술자)으로 쓴다(레거시).
                    const reflgen::type_descriptor* type = Meta::Find(pointee->GetTypeID());
                    auto* authoring = dynamic_cast<Authoring::ReflgenWriter*>(&out);
                    if (nullptr == type)
                    {
                        out.write_null(); // unknown component
                    }
                    else if (nullptr == authoring)
                    {
                        throw reflgen::serialization_error("component lists are written only to authoring documents");
                    }
                    else
                    {
                        Meta::SerializeInto(Meta::MostDerived(pointee), *type, authoring->TakeValueNode());
                    }
                }
            }
			else if constexpr (Meta::Typed::ReflgenRecord<U>)
			{
				Meta::Typed::ReflgenDetail::WritePointee(out, pointee);
			}
			else
			{
				Debug::PrintLog(spdlog::level::err, "Serialize: Unsupported sequence element type");
			}
		}
		out.end_array();
	}

	static void read(reflgen::reader& in, C&) { in.skip_value(); }
};

template<>
struct reflgen::serializer<HashingString>
{
	static void write(reflgen::writer& out, const HashingString& value) { out.write_string(value.ToString()); }
	static void read(reflgen::reader& in, HashingString& value) { value = HashingString(in.read_string()); }
};

template<>
struct reflgen::serializer<HashedGuid>
{
	static void write(reflgen::writer& out, const HashedGuid& value) { out.write_uint(value.m_ID_Data); }
	static void read(reflgen::reader& in, HashedGuid& value) { value = HashedGuid(static_cast<size_t>(in.read_uint())); }
};

template<>
struct reflgen::serializer<FileGuid>
{
	static void write(reflgen::writer& out, const FileGuid& value) { out.write_string(value.ToString()); }
	static void read(reflgen::reader& in, FileGuid& value) { value = FileGuid(in.read_string()); }
};

namespace Meta::Typed::ReflgenDetail
{
	// math 값 — 한 줄 맵. 필드는 float 이다(write_float32 → float 표기).
	template<std::size_t N>
	void WriteFloatMap(reflgen::writer& out, const char* const (&keys)[N], const float (&values)[N])
	{
		out.prefer_inline();
		out.begin_object(N);
		for (std::size_t i = 0; i < N; ++i)
		{
			out.write_key(keys[i]);
			out.write_float32(values[i]);
		}
		out.end_object();
	}

	template<std::size_t N>
	void ReadFloatMap(reflgen::reader& in, const char* const (&keys)[N], float* const (&targets)[N])
	{
		in.begin_object();
		for (std::string key; in.next_key(key);)
		{
			bool matched = false;
			for (std::size_t i = 0; i < N && !matched; ++i)
			{
				if (key == keys[i])
				{
					*targets[i] = in.read_float32();
					matched = true;
				}
			}
			if (!matched) in.skip_value();
		}
		in.end_object();
	}
}

template<>
struct reflgen::serializer<math::vector2>
{
	static constexpr const char* keys[2] = { "x", "y" };
	static void write(reflgen::writer& out, const math::vector2& v)
	{
		Meta::Typed::ReflgenDetail::WriteFloatMap(out, keys, { v.x, v.y });
	}
	static void read(reflgen::reader& in, math::vector2& v)
	{
		Meta::Typed::ReflgenDetail::ReadFloatMap(in, keys, { &v.x, &v.y });
	}
};

template<>
struct reflgen::serializer<math::vector3>
{
	static constexpr const char* keys[3] = { "x", "y", "z" };
	static void write(reflgen::writer& out, const math::vector3& v)
	{
		Meta::Typed::ReflgenDetail::WriteFloatMap(out, keys, { v.x, v.y, v.z });
	}
	static void read(reflgen::reader& in, math::vector3& v)
	{
		Meta::Typed::ReflgenDetail::ReadFloatMap(in, keys, { &v.x, &v.y, &v.z });
	}
};

template<>
struct reflgen::serializer<math::vector4>
{
	static constexpr const char* keys[4] = { "x", "y", "z", "w" };
	static void write(reflgen::writer& out, const math::vector4& v)
	{
		Meta::Typed::ReflgenDetail::WriteFloatMap(out, keys, { v.x, v.y, v.z, v.w });
	}
	static void read(reflgen::reader& in, math::vector4& v)
	{
		Meta::Typed::ReflgenDetail::ReadFloatMap(in, keys, { &v.x, &v.y, &v.z, &v.w });
	}
};

template<>
struct reflgen::serializer<math::quaternion>
{
	static constexpr const char* keys[4] = { "x", "y", "z", "w" };
	static void write(reflgen::writer& out, const math::quaternion& v)
	{
		Meta::Typed::ReflgenDetail::WriteFloatMap(out, keys, { v.x, v.y, v.z, v.w });
	}
	static void read(reflgen::reader& in, math::quaternion& v)
	{
		Meta::Typed::ReflgenDetail::ReadFloatMap(in, keys, { &v.x, &v.y, &v.z, &v.w });
	}
};

template<>
struct reflgen::serializer<math::color>
{
	static constexpr const char* keys[4] = { "r", "g", "b", "a" };
	static void write(reflgen::writer& out, const math::color& v)
	{
		Meta::Typed::ReflgenDetail::WriteFloatMap(out, keys, { v.r, v.g, v.b, v.a });
	}
	static void read(reflgen::reader& in, math::color& v)
	{
		Meta::Typed::ReflgenDetail::ReadFloatMap(in, keys, { &v.r, &v.g, &v.b, &v.a });
	}
};

template<>
struct reflgen::serializer<math::rect>
{
	static constexpr const char* keys[4] = { "x", "y", "width", "height" };
	static void write(reflgen::writer& out, const math::rect& v)
	{
		Meta::Typed::ReflgenDetail::WriteFloatMap(out, keys, { v.x, v.y, v.width, v.height });
	}
	static void read(reflgen::reader& in, math::rect& v)
	{
		Meta::Typed::ReflgenDetail::ReadFloatMap(in, keys, { &v.x, &v.y, &v.width, &v.height });
	}
};

template<>
struct reflgen::serializer<math::matrix4x4>
{
	// 한 줄 시퀀스 16 개(행 우선). 레거시: 16 개가 아니면 단위 행렬.
	static void write(reflgen::writer& out, const math::matrix4x4& v)
	{
		out.prefer_inline();
		out.begin_array(16);
		for (int row = 0; row < 4; ++row)
			for (int column = 0; column < 4; ++column)
				out.write_float32(v.m[row][column]);
		out.end_array();
	}

	static void read(reflgen::reader& in, math::matrix4x4& v)
	{
		const std::optional<std::size_t> size = in.begin_array();
		if (size != 16u)
		{
			while (in.next_element()) in.skip_value();
			in.end_array();
			v = math::matrix4x4::identity();
			return;
		}
		for (int index = 0; in.next_element(); ++index)
		{
			const float value = in.read_float32();
			if (index < 16) v.m[index / 4][index % 4] = value;
		}
		in.end_array();
	}
};
