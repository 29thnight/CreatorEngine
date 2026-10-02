#pragma once
// 경계를 넘어 하나여야 하는 싱글턴 표본.
//
// ★ Singleton 이 **이미 보이는 자리**에서 연다 — 헤더 쪽은 ClassProperty.h 뒤에서,
//   모듈 쪽은 `import ce.core;` 뒤에서. 이 파일은 ClassProperty.h 를 스스로 열지
//   않는다(열면 모듈 쪽 번역 단위가 헤더로도 보게 된다).
//
//   두 번역 단위가 이 정의를 각자 갖지만 같은 토큰열이므로 ODR 상 한 클래스다.
//   그 기반 Singleton<CppModuleProbeSingleton>::s_instance 가 하나인지가 검사다.
class CppModuleProbeSingleton : public Singleton<CppModuleProbeSingleton>
{
public:
	int value = 0;
};
