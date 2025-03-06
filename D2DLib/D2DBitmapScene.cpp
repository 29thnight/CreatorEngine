#include "pch.h"
#include "D2DBitmapScene.h"
#include "D2DRenderer.h"
#include "../Utility_Framework/Core.Minimal.h"
#include "../RenderEngine/UI_DataSystem.h"
#include "../RenderEngine/FontManager.h"
#include "../GameManager.h"
#include "../EventSystem.h"
#include "../SoundManager.h"
#include "../InputManager.h"

UIObject::UIObject()
{
	m_pBitmaps.resize(10);
	m_BitmapKeys.resize(10);
}

UIObject::~UIObject()
{
}

UIObject::UIObject(ID2D1Bitmap* _pBitmap) :
	m_pBitmap(_pBitmap), 
	m_IsAnimation(false)
{
	D2D_SIZE_F size;
	size = m_pBitmap->GetSize();
	if (size.width < 1900.f)
	{
		m_Size = { size.width, size.height };
		m_Center = { size.width / 2, size.height / 2 };
	}
}

UIObject::UIObject(const std::vector<ID2D1Bitmap*>& _pBitmaps) : 
	m_pBitmaps(_pBitmaps),
	m_IsAnimation(true)
{
	D2D_SIZE_F size;
	size = m_pBitmaps[0]->GetSize();
	if (size.width < 1900.f)
	{
		m_Size = { size.width, size.height };
		m_Center = { size.width / 2, size.height / 2 };
	}
}

void UIObject::Render(ID2D1RenderTarget* _renderTarget)
{
	//if (m_IsAnimation)
	//{
	//	if (!m_IsDeSerialize)
	//	{
	//		m_pBitmaps = UISystem->GetAnimation(m_Key);

	//		if (m_pBitmaps[0])
	//		{
	//			D2D_SIZE_F size;
	//			size = m_pBitmaps[0]->GetSize();
	//			if (size.width < 1900.f)
	//			{
	//				m_Size = { size.width, size.height };
	//				m_Center = { size.width / 2, size.height / 2 };
	//			}
	//			m_IsDeSerialize = true;
	//		}
	//	}

	//	if (m_pBitmaps[0])
	//	{
	//		D2D_SIZE_F size;
	//		size = m_pBitmaps[0]->GetSize();
	//		if (size.width * Scale.x < 1900.f)
	//		{
	//			m_Size = { size.width * Scale.x, size.height * Scale.y };
	//			m_Center = { size.width * Scale.x * 0.5f, size.height * Scale.y * 0.5f };
	//		}
	//	}

	//	if (m_Visible)
	//	{
	//		_renderTarget->SetTransform(m_WorldMat);
	//		_renderTarget->DrawBitmap(m_pBitmaps[m_Index], NULL, m_Opacity);
	//	}
	//}
	//else
	//{
	//	if (!m_IsDeSerialize)
	//	{
	//		if (m_Key != "")
	//		{
	//			m_pBitmap = UISystem->GetBitmap(m_Key);
	//		}
	//		else if (m_BitmapKeys[m_Index] != "")
	//		{
	//			for (auto& key : m_BitmapKeys)
	//			{
	//				m_pBitmaps.push_back(UISystem->GetBitmap(key));
	//			}
	//		}

	//		if (m_pBitmap)
	//		{

	//			D2D_SIZE_F size;
	//			size = m_pBitmap->GetSize();
	//			if (size.width < 1900.f)
	//			{
	//				m_Size = { size.width, size.height };
	//				m_Center = { size.width / 2, size.height / 2 };
	//			}

	//			m_IsDeSerialize = true;
	//		}
	//	}

	//	if (m_pBitmap)
	//	{
	//		D2D_SIZE_F size;
	//		size = m_pBitmap->GetSize();
	//		if (size.width * Scale.x < 1900.f)
	//		{
	//			m_Size = { size.width * Scale.x, size.height * Scale.y };
	//			m_Center = { size.width * Scale.x * 0.5f, size.height * Scale.y * 0.5f };
	//		}
	//	}

	//	if (m_Visible)
	//	{
	//		GetTransform();
	//		_renderTarget->SetTransform(m_WorldMat);

	//		if (m_IsSelected && m_pSelectBitmap)
	//		{
	//			if (m_IsVClipping)
	//			{
	//				D2D_SIZE_F size;
	//				size = m_pBitmap->GetSize();
	//				m_ClippingSize = { 0, 0, size.width * m_ViewPercentage, size.height };
	//				_renderTarget->PushAxisAlignedClip(m_ClippingSize, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
	//			}
	//			else if (m_IsHClipping)
	//			{
	//				D2D_SIZE_F size;
	//				size = m_pBitmap->GetSize();
	//				m_ClippingSize = { 0, size.height - size.height * m_ViewPercentage, size.width, size.height };
	//				_renderTarget->PushAxisAlignedClip(m_ClippingSize, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
	//			}

	//			_renderTarget->DrawBitmap(m_pSelectBitmap, NULL, m_Opacity);

	//			if (m_IsVClipping || m_IsHClipping)
	//			{
	//				_renderTarget->PopAxisAlignedClip();
	//			}
	//		}
	//		else if (nullptr != m_pBitmaps[m_Index])
	//		{
	//			D2D_SIZE_F size;
	//			size = m_pBitmaps[m_Index]->GetSize();
	//			if (size.width * Scale.x < 1900.f)
	//			{
	//				m_Size = { size.width * Scale.x, size.height * Scale.y };
	//				m_Center = { size.width * Scale.x * 0.5f, size.height * Scale.y * 0.5f };
	//			}
	//			_renderTarget->DrawBitmap(m_pBitmaps[m_Index], NULL, m_Opacity);
	//		}
	//		else if (m_pBitmap)
	//		{
	//			if (m_IsVClipping)
	//			{
	//				D2D_SIZE_F size;
	//				size = m_pBitmap->GetSize();
	//				for (auto& event : m_EventType)
	//				{
	//					if (event == UIEventType::OnClipping)
	//					{
	//						UIEvent->Publish("Clipping", m_ClippingID);
	//					}
	//				}
	//				m_ClippingSize = { 0, 0, (size.width * m_ViewPercentage), size.height };
	//				_renderTarget->PushAxisAlignedClip(m_ClippingSize, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
	//			}
	//			else if (m_IsHClipping)
	//			{
	//				D2D_SIZE_F size;
	//				size = m_pBitmap->GetSize();
	//				for (auto& event : m_EventType)
	//				{
	//					if (event == UIEventType::OnClipping)
	//					{
	//						UIEvent->Publish("Clipping", m_ClippingID);
	//					}
	//				}
	//				m_ClippingSize = { 0, size.height - size.height * m_ViewPercentage, size.width, size.height };
	//				_renderTarget->PushAxisAlignedClip(m_ClippingSize, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
	//			}

	//			if (m_IsScoreObject)
	//			{
	//				if (m_currScore >= m_targetScore)
	//				{
	//					m_currScore = m_targetScore;
	//					UIControl->_isScoreEnd = true;
	//				}

	//				for (int i = 0; i < m_currScore; i++)
	//				{
	//					_renderTarget->DrawBitmap(m_pBitmap, NULL, m_Opacity);
	//					m_WorldMat._32 += m_Size.y;
	//					_renderTarget->SetTransform(m_WorldMat);
	//				}
	//			}
	//			else
	//			{
	//				_renderTarget->DrawBitmap(m_pBitmap, NULL, m_Opacity);
	//			}
	//			
	//			if (m_IsVClipping || m_IsHClipping)
	//			{
	//				_renderTarget->PopAxisAlignedClip();
	//			}
	//		}
	//	}
	//}
}

void UIObject::Update(float deltaTime)
{
	//if (m_IsAnimation)
	//{
	//	m_Index++;
	//	if (m_Index >= m_pBitmaps.size())
	//	{
	//		m_Index = 0;
	//	}
	//}

	//if (InputManagement->IsControllerButtonReleased(0, m_Button) && UIControl->GetCurrentLayer() == m_LayerID)
	//{
	//	m_IsExecute = true;
	//}

	//if (m_IsLerpOpacity)
	//{
	//	m_Opacity = Mathf::lerp(m_Opacity, m_TargetOpacity, m_OpacityLerpSpeed);

	//	if (fabs(m_Opacity - m_TargetOpacity) < 0.01f)
	//	{
	//		m_Opacity = m_TargetOpacity;
	//		m_IsLerpOpacity = false;
	//	}
	//}

	//if (m_EventDelayTime > 0.0f)
	//{
	//	m_EventDelayTime -= deltaTime;
	//}

	//if (m_IsScoreObject && m_currScore != m_targetScore)
	//{
	//	m_elapedTime += deltaTime;
	//	if (m_elapedTime >= m_scoreSpeed)
	//	{
	//		m_elapedTime -= m_elapedTime;
	//		m_currScore++;
	//		Sound->playSound("Result_FootStamp", ChannelType::UI);
	//		if (m_currScore > m_targetScore)
	//		{
	//			m_currScore = m_targetScore;
	//		}
	//	}
	//}

	//if (m_IsExecute && !m_IsLerpOpacity/* && 0.f == UIControl->_excuteDelay*/)
	//{
	//	if (!m_IsSelectExcute)
	//	{
	//		if (m_EventKey != "" && m_EventDelayTime <= 0.0f)
	//		{
	//			if (intValue.has_value())
	//			{
	//				Event->Publish(m_EventKey, (void*)&intValue.value());
	//			}
	//			else if (floatValue.has_value())
	//			{
	//				Event->Publish(m_EventKey, (void*)&floatValue.value());
	//			}
	//			UIControl->_excuteDelay = 0.8f;
	//		}
	//	}
	//	else if (m_IsSelected)
	//	{
	//		if (m_EventKey != "" && m_EventDelayTime <= 0.0f)
	//		{
	//			if (intValue.has_value())
	//			{
	//				Event->Publish(m_EventKey, (void*)&intValue.value());
	//			}
	//			else if (floatValue.has_value())
	//			{
	//				Event->Publish(m_EventKey, (void*)&floatValue.value());
	//			}
	//			UIControl->_excuteDelay = 0.8f;
	//		}
	//	}
	//}

	//m_IsExecute = false;
}

void UIObject::Serialize(_inout json& out)
{
	/*if (m_DestoryMark)
	{
		return;
	}
	json data;

	data["Name"] = m_Name;
	data["Key"] = m_Key;
	data["SelectKey"] = m_selectKey;
	data["PosLock"] = m_IsPosLock;
	data["Visible"] = m_Visible;
	data["Transform"]["Scale"] = {Scale.x, Scale.y};
	data["Transform"]["Rotation"] = Rotation;
	data["Transform"]["Pos"] = { Pos.x, Pos.y };
	data["Transform"]["ZOrder"] = m_ZOrder;
	data["AABB"]["Center"] = { m_Center.x, m_Center.y };
	data["AABB"]["Size"] = { m_Size.x, m_Size.y };
	data["Animation"] = m_IsAnimation;
	data["FrameSpeed"] = m_frameSpeed;
	data["Opacity"] = m_OriginOpacity;

	data["ID"] = m_SelectID;
	data["V_Clipping"] = m_IsVClipping;
	data["H_Clipping"] = m_IsHClipping;
	data["ClippingID"] = m_ClippingID;
	data["ViewPercentage"] = m_ViewPercentage;
	data["EventKey"] =m_EventKey;
	data["EventDelayTime"] = m_EventDelayTime;
	data["Button"] = (int)m_Button;
	data["UILayer"] = m_LayerID;
	data["UIEvent"] = json::array();
	data["PlayerID"] = m_PlayerID;
	data["FarBongSuID"] = m_FarBongsuID;
	data["ScoreObject"] = m_IsScoreObject;
	data["ScoreID"] = m_ScoreID;
	for (auto& event : m_EventType)
	{
		data["UIEvent"].push_back((int)event);
	}
	data["IntValue"] = intValue.value_or(0);
	data["FloatValue"] = floatValue.value_or(0.0f);
	data["SelectExcute"] = m_IsSelectExcute;
	if (m_BitmapKeys.size() > 0)
	{
		data["BitmapKeys"] = json::array();
		for (auto& key : m_BitmapKeys)
		{
			if(key != "")
			{
				data["BitmapKeys"].push_back(key);
			}
		}
	}

	out["ObjList"].push_back(data);*/
}

void UIObject::DeSerialize(_in json& in)
{
	/*m_IsDeSerialize = false;
	m_Name = in["Name"].get<std::string>();
	m_Key = in["Key"].get<std::string>();
	m_selectKey = in["SelectKey"].get<std::string>();
	m_Visible = in["Visible"].get<bool>();
	Scale = Mathf::jsonToVector2(in["Transform"]["Scale"]);
	Rotation = in["Transform"]["Rotation"].get<float>();
	Pos = Mathf::jsonToVector2(in["Transform"]["Pos"]);
	m_OriginOpacity = in["Opacity"].get<float>();
	m_Opacity = m_OriginOpacity;
	m_ZOrder = in["Transform"]["ZOrder"].get<int>();
	m_Center = Mathf::jsonToVector2(in["AABB"]["Center"]);
	m_Size = Mathf::jsonToVector2(in["AABB"]["Size"]);
	m_IsAnimation = in["Animation"].get<bool>();
	m_frameSpeed = in["FrameSpeed"].get<float>();
	m_SelectID = in["ID"].get<int>();
	m_IsVClipping = in["V_Clipping"].get<bool>();
	m_IsHClipping = in["H_Clipping"].get<bool>();
	m_ClippingID = in["ClippingID"].get<int>();
	m_EventKey = in["EventKey"].get<std::string>();
	m_EventDelayTime = in["EventDelayTime"].get<float>();
	m_Button = (ControllerButton)in["Button"].get<int>();
	m_LayerID = in["UILayer"].get<int>();
	intValue = in["IntValue"].get<int>();
	floatValue = in["FloatValue"].get<float>();
	if(in.contains("FarBongSuID"))
	{
		m_FarBongsuID = in["FarBongSuID"].get<int>();
	}

	if (in.contains("ScoreObject"))
	{
		m_IsScoreObject = in["ScoreObject"].get<bool>();
	}

	if (in.contains("ScoreID"))
	{
		m_ScoreID = in["ScoreID"].get<int>();
	}

	if (in.contains("BitmapKeys"))
	{
		auto& keys = in["BitmapKeys"];
		for (auto& key : keys)
		{
			m_BitmapKeys.push_back(key);
		}

		std::erase_if(m_BitmapKeys, [](const std::string& key) { return key == ""; });

		for (int i = 0; i < m_BitmapKeys.size(); i++)
		{
			if (m_BitmapKeys[i] != "")
			{
				m_pBitmaps[i] = UISystem->GetBitmap(m_BitmapKeys[i]);
			}
		}
	}

	if (in.contains("PlayerID"))
	{
		m_PlayerID = in["PlayerID"].get<int>();
	}

	if (in.contains("UIEvent"))
	{
		auto& eventList = in["UIEvent"];
		for (auto& event : eventList)
		{
			m_EventType.push_back((UIEventType)event.get<int>());
		}

		for (auto& event : m_EventType)
		{
			if (event == UIEventType::OnOpacity)
			{
				UIControl->DeSerializeOpacityEventUI(m_LayerID, this);
			}
			else if (event == UIEventType::OnSelect)
			{
				UIControl->DeSerializeSelectEventUI(m_LayerID, this);
			}
			else if (event == UIEventType::OnClipping)
			{
				UIControl->DeSerializeClippingEventUI(this);
			}
			else if (event == UIEventType::OnScore)
			{
				UIControl->DeSerializeScoreEventUI(m_LayerID, this);
			}
			else if (event == UIEventType::OnSelected)
			{
				UIControl->DeSerializeSelectedEventUI(m_LayerID, this);
			}
			else if (event == UIEventType::OnStateChange)
			{
				m_IsStateChain = true;
				UIControl->DeSerializeStateEventUI(this, m_PlayerID);
			}
			else if (event == UIEventType::OnFarBongsu)
			{
				UIControl->DeSerializeFarBongsuEventUI(this, m_FarBongsuID);
			}
			else if (event == UIEventType::OnInteract)
			{
				UIControl->DeSerializeInteractEventUI(this, m_PlayerID);
			}
			else if (event == UIEventType::OnManduk)
			{
				UIControl->DeSerializeMandukEventUI(this);
			}
			else if (event == UIEventType::OnStamp)
			{
				UIControl->DeSerializeStampEventUI(this);
			}
			else if (event == UIEventType::OnDescription)
			{
				UIControl->DeSerializeDescEventUI(this);
			}
			else if (event == UIEventType::OnStageImage)
			{
				UIControl->DeSerializeStageImgEventUI(this);
			}
			else if (event == UIEventType::OnCheckList)
			{
				UIControl->DeSerializeDescEventUI(this);
			}
			else if (event == UIEventType::OnStartText)
			{
				UIControl->DeSerializeStageStartText(this);
			}
			else if (event == UIEventType::OnEndText)
			{
				UIControl->DeSerializeScoreEndText(this);
			}
		}
	}

	if (in.contains("SelectExcute"))
	{
		m_IsSelectExcute = in["SelectExcute"].get<bool>();
	}

	if (m_IsAnimation)
	{
		m_pBitmaps = UISystem->GetAnimation(m_Key);
		if (m_pBitmaps.size() > 0)
		{
			m_IsDeSerialize = true;
		}
	}
	else
	{
		m_pBitmap = UISystem->GetBitmap(m_Key);
		if (m_selectKey != "")
		{
			m_pSelectBitmap = UISystem->GetBitmap(m_selectKey);
		}

		if (m_pBitmap || 0 < m_pBitmaps.size())
		{
			m_IsPosLock = true;
			m_IsDeSerialize = true;
		}
	}*/
}

void UIObject::SetBitmap(ID2D1Bitmap* _pBitmap)
{
	m_pBitmap = _pBitmap;
	D2D_SIZE_F size;
	size = m_pBitmap->GetSize();
	m_Size = { size.width, size.height };
	m_Center = { size.width / 2, size.height / 2 };
}

D2D_MATRIX_3X2_F UIObject::GetTransform()
{
	m_WorldMat = D2D1::Matrix3x2F::Scale(D2D1::SizeF(Scale.x, Scale.y)) *
		D2D1::Matrix3x2F::Rotation(Rotation) *
		D2D1::Matrix3x2F::Translation(Pos.x, Pos.y);

	return m_WorldMat;
}

TextUIObject::TextUIObject()
{
}

TextUIObject::~TextUIObject()
{
	if (m_pTextRenderer)
	{
		delete m_pTextRenderer;
		m_pTextRenderer = nullptr;
	}
}

void TextUIObject::Render(ID2D1RenderTarget* _Context)
{
	if (!m_Visible || !m_IsDeSerialize || m_pTextFormat == nullptr)
	{
		return;
	}

	D2D_MATRIX_3X2_F mat = GetTransform();
	if (prevPos != Pos)
	{
		m_Size.left = Pos.x;
		m_Size.top = Pos.y;
		m_Size.right = m_Size.left + m_Size.right;
		m_Size.bottom = m_Size.top + m_Size.bottom;
		prevPos = Pos;
	}

	if (m_pBrush)
	{
		m_pBrush->SetColor(m_Color);
		m_pOutlineBrush->SetColor(m_OutlineColor);
	}
	else
	{
		_Context->CreateSolidColorBrush(m_Color, &m_pBrush);
		_Context->CreateSolidColorBrush(m_OutlineColor, &m_pOutlineBrush);
		m_pBrush->SetColor(m_Color);
		m_pOutlineBrush->SetColor(m_OutlineColor);
	}

	_Context->SetTransform(mat);
	if (0 != m_Text.length() && m_pTextLayout)
	{
		m_pTextLayout->SetFontSize(m_FontSize, m_textRange);

		if(!m_pTextRenderer)
		{
			ComPtr<ID2D1Factory> pFactory = nullptr;

			_Context->GetFactory(&pFactory);
			m_pTextRenderer = new CustomTextRenderer(pFactory.Get(), _Context, m_pOutlineBrush);
		}

		_Context->DrawTextLayout(
			D2D1::Point2F(m_Size.left, m_Size.top),
			m_pTextLayout,
			m_pBrush
		);

		m_pTextLayout->Draw(nullptr, m_pTextRenderer, m_Size.left, m_Size.top);

	}
}

void TextUIObject::Update(float deltaTime)
{
	if (m_EventKey != "")
	{
		Event->Publish(m_EventKey, &m_Text);
		SetText(m_Text);
	}
}

void TextUIObject::SetRoation(float _Rotation)
{
	Rotation = _Rotation;
	Rotation = fmodf(Rotation, 360.0f);
}

void TextUIObject::AddRoation(float _Rotation)
{
	Rotation += _Rotation;
	Rotation = fmodf(Rotation, 360.0f);
}

void TextUIObject::SetPos(const Vector2& _Location)
{
	Pos = _Location;
}

void TextUIObject::AddPos(float x, float y)
{
	Pos = { Pos.x + x, Pos.y + y };
}

void TextUIObject::SetText(const std::wstring& text)
{
	m_Text = text;
	if (m_pTextLayout)
	{
		m_pTextLayout->Release();
		m_pTextLayout = nullptr;
	}
	FontSystem->GetFactory()->CreateTextLayout(
		m_Text.c_str(),
		m_Text.length(),
		m_pTextFormat, 
		m_Size.right - m_Size.left,
		m_Size.bottom - m_Size.top,
		&m_pTextLayout);
	m_textRange.length = m_Text.length();
}

void TextUIObject::SetScale(float x, float y)
{
	Scale = Vector2(x, y);
}

void TextUIObject::Serialize(_inout json& out)
{
	if (m_DestoryMark)
	{
		return;
	}

	json data;
	data["Name"] = m_Name;
	data["Key"] = m_Key;
	data["Visible"] = m_Visible;
	data["Transform"]["Scale"] = { Scale.x, Scale.y };
	data["Transform"]["Rotation"] = Rotation;
	data["Transform"]["Pos"] = { Pos.x, Pos.y };
	data["Transform"]["ZOrder"] = m_ZOrder;
	data["AABB"]["Size"] = { m_Size.right, m_Size.bottom };
	data["Text"] = m_Text;
	data["FontSize"] = m_FontSize;
	data["Color"] = { m_Color.r, m_Color.g, m_Color.b, m_Color.a };
	data["OutlineColor"] = { m_OutlineColor.r, m_OutlineColor.g, m_OutlineColor.b, m_OutlineColor.a };
	data["FontKey"] = m_Key;
	data["EventKey"] = m_EventKey;
	data["ZOrder"] = m_ZOrder;

	out["TextList"].push_back(data);
}

void TextUIObject::DeSerialize(_in json& in)
{
	m_Name = in["Name"].get<std::string>();
	m_Key = in["Key"].get<std::string>();
	m_Visible = in["Visible"].get<bool>();
	Scale = Mathf::jsonToVector2(in["Transform"]["Scale"]);
	Rotation = in["Transform"]["Rotation"].get<float>();
	Pos = Mathf::jsonToVector2(in["Transform"]["Pos"]);
	m_ZOrder = in["Transform"]["ZOrder"].get<int>();
	m_Size.right = in["AABB"]["Size"][0].get<float>();
	m_Size.bottom = in["AABB"]["Size"][1].get<float>();
	m_Text = in["Text"].get<std::wstring>();
	m_FontSize = in["FontSize"].get<float>();

	m_Color.r = in["Color"][0].get<float>();
	m_Color.g = in["Color"][1].get<float>();
	m_Color.b = in["Color"][2].get<float>();
	m_Color.a = in["Color"][3].get<float>();

	m_OutlineColor.r = in["OutlineColor"][0].get<float>();
	m_OutlineColor.g = in["OutlineColor"][1].get<float>();
	m_OutlineColor.b = in["OutlineColor"][2].get<float>();
	m_OutlineColor.a = in["OutlineColor"][3].get<float>();

	m_Key = in["FontKey"].get<std::string>();
	m_EventKey = in["EventKey"].get<std::string>();
	m_ZOrder = in["ZOrder"].get<int>();

	if (!m_Key.empty())
	{
		m_pTextFormat = FontSystem->GetFont(m_Key);
	}
	m_IsDeSerialize = true;
}

D2D_MATRIX_3X2_F TextUIObject::GetTransform()
{
	m_WorldMat = D2D1::Matrix3x2F::Scale(D2D1::SizeF(Scale.x, Scale.y)) *
		D2D1::Matrix3x2F::Rotation(Rotation) *
		D2D1::Matrix3x2F::Translation(Pos.x, Pos.y);

	if (m_pParent)
	{
		m_WorldMat = m_pParent->GetTransform() * m_WorldMat;
	}

	return m_WorldMat;
}
