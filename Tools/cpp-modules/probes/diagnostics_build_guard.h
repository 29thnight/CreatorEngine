#pragma once
// `import ce.diagnostics;` **뒤에** 연다.
//
// ★ 사용자 매크로는 BMI 호환성 검사 대상이 아니다. Development 로 만든 BMI 를
//   Shipping 소비자가 import 해도 컴파일러는 아무 말도 하지 않고, 소비자는
//   Development 의 스코프(서비스를 부르는 쪽)를 받는다 — Shipping 격리 게이트가
//   "심볼 0" 을 보려던 바로 그 코드가 다시 링크된다.
//
//   그래서 BMI 가 자기 구성을 들고 다니고(ce::diagnostics_build::shipping),
//   소비자가 자기 매크로와 대조한다. CE_SHIPPING 이 정의되지 않은 소비자는
//   여기서 식별자 오류로 멈춘다 — `#if` 처럼 조용히 0 이 되지 않는다.
static_assert(ce::diagnostics_build::shipping == (CE_SHIPPING != 0),
              "CE_MODULE_SHIPPING_MISMATCH: ce.diagnostics BMI 의 구성과 이 번역 단위의 CE_SHIPPING 이 다르다");
