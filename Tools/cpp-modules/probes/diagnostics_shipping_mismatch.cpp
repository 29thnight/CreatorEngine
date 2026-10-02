// 실패해야 하는 항목 — 구성이 어긋난 BMI 를 import 한다.
//
// 스크립트가 이 파일을 BMI 와 **반대의** CE_SHIPPING 으로 컴파일한다. 컴파일러는
// 사용자 매크로 차이를 BMI 호환성 문제로 보지 않으므로, 막는 것은
// diagnostics_build_guard.h 의 static_assert 하나뿐이다. 이 파일이 컴파일되면
// 그 방어가 이빨이 없다는 뜻이다 — 스크립트는 그것을 붉음으로 읽는다.
//
// 판정 근거는 "컴파일이 실패했다" 가 아니라 **오류 문안에
// CE_MODULE_SHIPPING_MISMATCH 가 있다** 이다. 엉뚱한 이유(모듈을 못 찾음 등)로
// 실패한 것을 통과로 읽지 않기 위해서다.
import ce.diagnostics;

#include "diagnostics_build_guard.h"

int main()
{
	return 0;
}
