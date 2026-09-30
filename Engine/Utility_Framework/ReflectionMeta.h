#pragma once
// 엔진 정체성 스탬핑 (PHASE 18 CT7-b) — meta::identity<T, Base> 를 상속한 클래스는 생성 때 m_name(타입 이름)·
// m_typeID(타입 ID)를 받는다. 생성 순서 계약: 베이스 우선·말단 최종 덮어쓰기(중간 베이스 체인에서도 말단의 것이 남는다).
//
// reflgen 도입 P5: 엔진 스키마(MetaSchema.h — schema·field·method·속성·meta::of·질의)와 reflgen 다리(ReflgenBridge.h)를
// 걷었다. 타입 서술은 [[reflgen::reflect]] 에서 생성되고, 부모도 reflgen 이 클래스 선언에서 읽는다(반영된 가장 가까운
// 조상 — identity<T, Base> 층은 건너뛴다). 여기 남은 것은 서술과 무관한 정체성 스탬핑이다. 옛 상속 서술자
// (meta_identity·identity_descriptor)는 엔진 스키마의 부모 추론만 읽었으므로 함께 걷었다.
#include "ReflectionFunction.h" // 옛 include 사슬 — 이 header 를 거쳐 리플렉션 창구·로그를 받던 소비자가 있다
#include "TypeTrait.h"
#include <reflgen/core/name.h>
#include <utility>

namespace meta
{
    struct no_base {};

    // Self 는 protected 로 공급된다 — 클래스 본문이 &Self::member 로 쓴다(옛 reflect() 레시피의 관용).
    template<class T, class Base = no_base>
    class identity : public Base
    {
    protected:
        using Self = T;

        identity()
        {
            StampIdentity();
        }

        template<class... Args>
        explicit identity(Args&&... args) : Base(std::forward<Args>(args)...)
        {
            StampIdentity();
        }

    private:
        void StampIdentity()
        {
            // m_name 은 씬 파일에 그대로 적힌다 — typeID 를 만드는 이름(TypeTrait::type_name)과 같은 표기여야 한다.
            // reflgen 의 이름은 NUL 종단이 보장된다(__FUNCSIG__ 조각인 TypeTrait 쪽은 아니다).
            static_assert(reflgen::type_name_of<T>() == TypeTrait::type_name<T>(),
                "reflgen 과 엔진의 타입 이름 표기가 갈렸다 — m_name 과 typeID 가 서로 다른 이름에서 나온다");
            this->m_name = reflgen::type_name_of<T>().data();
            this->m_typeID = TypeTrait::GUIDCreator::GetTypeID<T>();
        }
    };

    // 루트 특수화 — 스탬핑할 상속 멤버가 없다. Self 만 공급한다.
    template<class T>
    class identity<T, no_base>
    {
    protected:
        using Self = T;
    };
}
