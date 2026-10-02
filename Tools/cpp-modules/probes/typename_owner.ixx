// 관찰 항목의 재료 — 이름 있는 모듈 **본문에** 붙은 타입 하나.
//
// 1단계(헤더 감싸기)에서는 모든 엔터티가 전역 모듈에 남지만, 2단계에서 소유권을
// 모듈로 옮기면 컴포넌트 타입이 이렇게 모듈에 붙는다. 그때
// TypeTrait::type_name<T>() 가 무엇을 내는지를 typename_module_owned.cpp 가 잰다.
export module ce.probe.typename_owner;

export struct CppModuleOwnedComponent
{
	int value = 0;
};
