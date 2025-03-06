#pragma once
#include "../Utility_Framework/Core.Mathf.h"
#include "../Utility_Framework/Core.Memory.hpp"
#include "CustomTextRenderer.h"
#include <vector>
#include <nlohmann/json.hpp>
using json = nlohmann::json;

using namespace DirectX::SimpleMath;
namespace DirectX11
{
	class D2DRenderer;
}

enum class UIEventType
{
	OnOpacity,
	OnSelect,
	OnCursor,
	OnExecute,
	OnClipping,
	OnScore,
	OnSelected,
	OnStateChange,
	OnFarBongsu,
	OnInteract,
	OnCheckList,
	OnStageImage,
	OnDescription,
	OnStamp,
	OnManduk,
	OnStartText,
	OnEndText,
	UIEventTypeMax
};


//아... 여기 다 박으니까 규모가 커진다!
struct UIObject
{
	std::string				  m_Name{};
	Vector2					  Scale = { 1.0f,1.0f };
	float					  Rotation = 0.f;
	Vector2					  Pos = { 0.f,0.f };
	std::string				  m_Key{};
	ID2D1Bitmap*			  m_pBitmap{};
	std::string				  m_selectKey{};
	ID2D1Bitmap*			  m_pSelectBitmap{};
	std::vector<ID2D1Bitmap*> m_pBitmaps;
	std::deque<std::string>   m_BitmapKeys;
	D2D1_RECT_F				  m_BitmapSize{};
	D2D1_RECT_F				  m_ClippingSize{};
	int						  m_ZOrder{};		// 그리는 순서  
	float 					  m_frameSpeed = 1.0f;
	uint32					  m_Index = 0;
	bool					  m_Visible = true;		// 그리려는지
	bool					  m_IsDeSerialize = true;
	bool					  m_IsPosLock = false;
	D2D_MATRIX_3X2_F		  m_WorldMat{};
	Mathf::Vector2			  m_Center = { 0.0f, 0.0f };
	Mathf::Vector2			  m_Size = { 0.0f, 0.0f };
	bool					  m_IsAnimation = false;
	bool					  m_DestoryMark = false;
	bool					  m_IsVClipping = false;
	bool					  m_IsHClipping = false;
	bool					  m_IsLerpOpacity = false;
	float					  m_ViewPercentage{ 1.f };
	float					  m_EventDelayTime = 0.0f;
	float					  m_Opacity = 1.0f;
	float                     m_OriginOpacity = 1.0f;
	float					  m_TargetOpacity = 0.0f;
	float					  m_OpacityLerpSpeed = 1.2f;

	ControllerButton		  m_Button = ControllerButton::A;
	std::vector<UIEventType>  m_EventType;
	std::string				  m_EventKey{};
	std::string 			  m_UIEventKey{};
	int						  m_LayerID = 0;
	int						  m_SelectID = -1;
	int						  m_ClippingID = 0;
	int						  m_ScoreID = 0;
	int						  m_Score = 0;
	bool					  m_IsScoreObject = false;
	bool					  m_IsSelected = false;
	bool					  m_IsExecute = false;
	bool					  m_IsSelectExcute = false;
	bool					  m_IsStateChain = false;

	int					      m_PlayerID = 0;
	int						  m_FarBongsuID = 0;
	int						  m_currScore = 0;
	int						  m_targetScore = 0;
	float                     m_scoreSpeed = 3.f;
	float					  m_elapedTime = 0.f;

	std::optional<int>		  intValue{ std::nullopt };
	std::optional<float>	  floatValue{ std::nullopt };

	UIObject();
	~UIObject();
	UIObject(ID2D1Bitmap* _pBitmap);
	UIObject(const std::vector<ID2D1Bitmap*>& _pBitmaps);
	void Render(ID2D1RenderTarget* _Context);
	void Update(float deltaTime);

	void Serialize(_inout json& out);
	void DeSerialize(_in json& in);

	void SetBitmap(ID2D1Bitmap* _pBitmap);
	void PushBitmap(ID2D1Bitmap* _pBitmap);

	D2D_MATRIX_3X2_F GetTransform();
	std::strong_ordering operator<=>(const UIObject& other) const
	{
		return m_ZOrder <=> other.m_ZOrder;
	}
};

struct TextUIObject
{
	std::string				  m_Name{};
	Vector2					  Scale = { 1.0f,1.0f };
	float					  Rotation = 0.f;
	Vector2					  prevPos = { 0.f,0.f };
	Vector2					  Pos = { 0.f,0.f };
	std::string				  m_Key{};
	D2D1_RECT_F				  m_Size{ 0, 0, 500, 500 };
	int						  m_ZOrder{};		// 그리는 순서  
	float 					  m_frameSpeed = 1.0f;
	uint32					  m_Index = 0;
	bool					  m_Visible = true;		// 그리려는지
	bool					  m_IsDeSerialize = true;
	Mathf::Vector2			  m_AABBSize = { 250.0f, 250.0f };
	D2D_MATRIX_3X2_F		  m_WorldMat{};
	bool					  m_IsSelected = false;
	bool					  m_DestoryMark = false;
	UIObject*				  m_pParent{};
	//text properties
	std::wstring			  m_Text{};
	D2D1_COLOR_F			  m_Color{ 1, 1, 1, 1 };
	D2D1_COLOR_F			  m_OutlineColor{ 0, 0, 0, 1 };
	float					  m_FontSize{ 16.f };
	DWRITE_TEXT_RANGE		  m_textRange{};
	ID2D1SolidColorBrush*	  m_pBrush{};
	ID2D1SolidColorBrush*	  m_pOutlineBrush{};
	IDWriteTextFormat*		  m_pTextFormat{};
	IDWriteTextLayout*		  m_pTextLayout{};
	CustomTextRenderer*		  m_pTextRenderer{};

	std::string				  m_EventKey{};

	TextUIObject();
	~TextUIObject();

	void Render(ID2D1RenderTarget* _Context);
	void Update(float deltaTime);
	void SetVisible(bool visible) { m_Visible = visible; }
	void SetRoation(float _Rotation);
	void AddRoation(float _Rotation);
	float GetRotation() { return Rotation; }
	void SetPos(const Vector2& _Location);
	void AddPos(float x, float y);
	void SetText(const std::wstring& text);
	Vector2 GetLocation() { return Pos; }
	void SetScale(float x, float y);
	Vector2 GetScale() { return Scale; }

	void Serialize(_inout json& out);
	void DeSerialize(_in json& in);

	D2D_MATRIX_3X2_F GetTransform();
	std::strong_ordering operator<=>(const TextUIObject& other) const
	{
		return m_ZOrder <=> other.m_ZOrder;
	}
};

class ScorePopUp
{
public:

};