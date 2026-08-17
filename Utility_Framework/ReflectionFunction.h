#pragma once
#include "TypeTrait.h"
#include "MetaUtility.h"
#include "ReflectionType.h"
#include "ReflectionRegister.h"
#include "ReflectionUndo.h"
#include "Core.Mathf.h"
#include "LogSystem.h"
#include "HashingString.h"
#include "MetaSchema.h" // 열거형 표(meta::enum_entries) — magic_enum 대체(CT9-b)

namespace Meta
{
    // 타입의 런타임 Type 테이블 단일 창구 — 정의는 ReflectionMeta.h.
    // CT9: 타입은 reflect() 레시피(또는 meta::of<T> 외부 서술)만 선언하고, 이
    // 함수가 어댑터(meta::adapt)로 런타임 Type을 공급한다. 레거시 Reflect()
    // 폴백 가지는 정의 잔존 0건 증명 후 제거됐다(CT8).
    template<class T>
    const Type& TypeOf();

    template<typename T>
    static inline void Register()
    {
        const Type& type = TypeOf<T>();
        Registry::GetInstance()->Register(type.name, type);
        // CT11: FactoryRegistry 등록 은퇴 — 팩토리는 adapt가 Type에 직접 채운다.
        // CT7: VectorFactory/Invoker 등록 은퇴 — 레거시 워크 전용이었다.
    }

    static inline const Type* Find(std::string_view name)
    {
        return Registry::GetInstance()->Find(name.data());
    }

    // FindEnum(이름 키 조회)은 열거형 점검(8-17)에서 삭제 — 소비자 전원이
    // Property::enumType 직접 참조로 전환됐다(이름 매칭 실패 여지 자체가 소멸).

	static inline const Type* Find(size_t typeID)
	{
		return Registry::GetInstance()->Find(typeID);
	}

    // CT9-b: magic_enum 은퇴 — 자급 표(meta::enum_entries, __FUNCSIG__ 범위
    // 스캔·동일 범위/순서)를 레거시 EnumValue 표로 투영한다. 이름은 정적
    // 물질화라 NUL 종단(콘솔의 문자열 비교 전제).
    template <typename Enum>
    const EnumType& create_enum_type()
    {
        static const auto values = []
        {
            constexpr auto& src = meta::enum_entries<Enum>;
            std::array<EnumValue, src.size()> out{};
            for (size_t i = 0; i < src.size(); ++i)
            {
                out[i] = EnumValue{ src[i].name.data(), static_cast<int>(src[i].value) };
            }
            return out;
        }();
        static const EnumType enumType
        {
            meta::type_name_of<Enum>().data(),
            std::span<const EnumValue>(values.data(), values.size())
        };
        return enumType;
    }

    // CT10 감사 수축: 벡터 특수 분기 3종(포인터/shared_ptr/객체 원소)이
    // 산출하던 필드(isVector·elementType*·createVectorIterator·isPointer·
    // offset·typeInfo·getter)는 전부 소비 0으로 판명되어 삭제 — 남은 산출은
    // name·typeName·setter·typeID뿐이라 세 분기가 구별 불가능해졌고, 단일
    // 제네릭 경로로 접힌다. shared_ptr 이름 보정만이 분기의 생존자다
    // (콘솔·DrawMethods가 읽는 typeName을 살린다).
    template<typename ClassT, typename T>
    Property MakePropertyImpl(const char* name, T ClassT::* member)
    {
        std::string typeStr = ToString<T>();
        HashedGuid typeID = TypeTrait::GUIDCreator::GetTypeID<T>();

        if constexpr (is_shared_ptr_v<T>)
        {
            // 타입 이름은 가리키는 대상의 이름이어야 한다.
            // 원시 포인터는 ToString이 "Material *"에서 " *"를 떼어 "Material"을 주지만,
            // shared_ptr은 "std::shared_ptr<class Material>"이 그대로 남는다.
            typeStr = RemoveObjectPrefix(ExtractPointee(typeStr));
        }

        if constexpr (Meta::HasReflection<std::remove_cvref_t<T>>)
        {
            Meta::Register<std::remove_cvref_t<T>>();
        }

        return
        {
            name,
            typeStr,
            [member](void* instance, std::any value)
            {
                static_cast<ClassT*>(instance)->*member = std::any_cast<T>(value);
            },
            typeID,
        };
    }

    template<typename ClassT, typename EnumT>
    std::enable_if_t<std::is_enum_v<EnumT>, Property>
        MakeEnumPropertyImpl(const char* name, EnumT ClassT::* member)
    {
        Property prop
        {
            name,
            ToString<EnumT>(),
            [member](void* instance, std::any anyValue) {
                int intValue = std::any_cast<int>(anyValue);
                static_cast<ClassT*>(instance)->*member = static_cast<EnumT>(intValue);
            },
			TypeTrait::GUIDCreator::GetTypeID<EnumT>(),
        };
        // 여기서 EnumT를 정적으로 알고 있으므로 이름 키 등록소가 필요 없다 —
        // 콘솔/인스펙터의 enum 항목 소비는 이 포인터 하나로 끝난다.
        prop.enumType = &create_enum_type<EnumT>();
        return prop;
    }

    template<typename ClassT, typename T>
    Property MakeProperty(const char* name, T ClassT::* member)
    {
        if constexpr (std::is_enum_v<T>)
        {
            return MakeEnumPropertyImpl(name, member);
        }
        else
        {
            return MakePropertyImpl(name, member);
        }
    }

    template<typename ClassT, typename Ret, typename... Args, std::size_t... Is>
    Method MakeMethodImpl(
        const char* name,
        Ret(ClassT::* method)(Args...),
        std::index_sequence<Is...>,
        const std::vector<std::string>& paramNames)
    {
        std::vector<MethodParameter> params = {
            MethodParameter{
                (Is < paramNames.size() ? paramNames[Is] : ("arg" + std::to_string(Is))),
                ToString<Args>(),
				TypeTrait::GUIDCreator::GetTypeID<Args>()
            }...
        };

        return
        {
            name,
            [method](void* instance, const std::vector<std::any>& args) -> std::any
            {
                if (args.size() != sizeof...(Args))
                    throw std::runtime_error("Argument count mismatch");

                auto call = [=]<std::size_t... I>(std::index_sequence<I...>) -> std::any
                {
                    if constexpr (std::is_void_v<Ret>)
                    {
                        (static_cast<ClassT*>(instance)->*method)(
                            std::any_cast<std::remove_reference_t<Args>>(args[I])...
                        );
                        return {};
                    }
                    else
                    {
                        return (static_cast<ClassT*>(instance)->*method)(
                              std::any_cast<std::remove_reference_t<Args>>(args[I])...
                        );
                    }
                };

                return call(std::index_sequence_for<Args...>{});
            },
            std::move(params)
        };
    }

    template<typename ClassT, typename Ret, typename... Args>
    Method MakeMethod(const char* name, Ret(ClassT::* method)(Args...), const std::vector<std::string>& paramNames = {})
    {
        return MakeMethodImpl(name, method, std::index_sequence_for<Args...>{}, paramNames);
    }

    // InvokeMethodByMetaName은 CT2에서 삭제 — 호출처 0건. 문자열 메서드
    // 디스패치의 옛 소비자(키프레임 이벤트·입력 액션)는 CoreCLR 은퇴(9-4)에서
    // 관리 측으로 이관됐다. Method 체계 자체는 인스펙터 DrawMethods가 쓰므로 유지.

	// MakePropChangeCommand는 CT10 감사에서 삭제 — 호출자 0건(레거시 인스펙터
	// 체인과 동반 사망). 활성 Undo 경로는 아래 CustomChangeCommand 하나다.

	inline void MakeCustomChangeCommand(std::function<void()> undoFunc, std::function<void()> redoFunc)
	{
        UndoManager::GetInstance()->Execute(
			std::make_unique<CustomChangeCommand>(undoFunc, redoFunc)
		);
	}
}
