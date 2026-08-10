#pragma once
#ifndef DYNAMICCPP_EXPORTS

class Material;
class ImageComponent;

// ── 셰이더 선택 창 두 개 (PHASE 4-3 슬라이스 5) ──
//
// "SelectShader"와 "SelectImageCustomShader"는 ShaderSystem 안에 있었다.
// 그 조각 하나 때문에 셰이더 시스템(층 3)이 ImageComponent.h를 열었다 —
// 실제로 부르는 것은 SetCustomPixelShader 한 줄이고, 그 메서드는 기반
// 클래스 UIComponent 것이다. 완전 타입이 필요해서 열었을 뿐이다.
//
// 창을 여는 쪽도 대상을 지정하는 쪽도 전부 에디터(층 6)였다. 조각만
// 아래에 남아 있었다.
//
// 플레이어에도 짐이었다. PlayerMain은 창 펌프를 부르지 않는 이유로
// "엔진 안에서 등록되던 조각들(SelectShader 등)은 플레이어 화면에 속하지
// 않는다"고 적어 두었다 — 등록은 하되 그리지 않는 회피였다. 이제 플레이어는
// 이 창들을 등록조차 하지 않는다.
class ShaderSelectionWindow
{
public:
	// 에디터 시작 때 한 번. ImGui 컨텍스트 둘을 등록한다.
	static void Register();

	// 목록에서 고른 것을 받을 대상. 고르는 순간 비워진다.
	static void SetShaderTarget(Material* material);
	static void SetImageTarget(ImageComponent* image);
};

#endif // !DYNAMICCPP_EXPORTS
