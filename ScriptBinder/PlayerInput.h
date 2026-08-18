#pragma once
#include "Core.Minimal.h"
#include "Component.h"
#include "KeyState.h"
class ActionMap;
class PlayerInputComponent : public meta::identity<PlayerInputComponent, Component>
{
   public:
   static consteval auto reflect()
   {
       return meta::schema<Self>(
           meta::field<&Self::m_actionMapName>,
           meta::field<&Self::m_scriptName>,
           meta::field<&Self::controllerIndex>);
   }
public:
	PlayerInputComponent() = default;
	
	// C3 â€” ê°€ìƒ Update ì˜¤ë²„ë¼ì´ë“œë¥¼ ë²„ë¦¬ê³  ë¹„ê°€ìƒ ì§„ì…ì ìœ¼ë¡œ. ì¼ì‹œì •ì§€ ê²½ë¡œ
	// (Scene::InternalPauseUpdateForUI)ê°€ ì´ê²ƒì„ ì§ì ‘ ë¶€ë¥´ë¯€ë¡œ ì´ë¦„ì´ ê³„ì•½ì´ë‹¤.
	void TickInput(float tick);
	void OnAddedToScene() override;
	void OnRemovingFromScene() override;

	void SetActionMap(std::string mapName);
	void SetActionMap(ActionMap* _actionMap);
	void SetControllerVibration(float tick, float leftMotorSpeed, float rightMotorSpeed, float lowFre, float highFre);
	void SetControllerVibration(float tick, float power);
	//¾µ ¾×¼Ç¸Ê³×ÀÓ
	ActionMap* m_actionMap = nullptr; 
	std::string m_actionMapName = "None";
	//ÇÔ¼ö°¡ Æ÷ÇÔµÈ ½ºÅ©¸³Æ®³×ÀÓ ¾Æ¸¶ ÀÚ±â°¡ ¼ÒÀ¯ÁßÀÎ°Í¸¸? ¾ø¾îµµ µÉµí
	std::string m_scriptName{};

	//¾µ ÄÁÆ®·Ñ·¯ ÀÎµ¦½º Å°º¸µå,¸¶¿ì½º´Â 0¸¸Áö¿ø
	int controllerIndex = 0;
	//ÄÄÆ÷³ÍÆ® »ı¼º½Ã ÀÚµ¿À¸·Î ÀÎÇ²¾×¼Ç¸Å´ÏÀú°¡ ¼öÁı -> ¼öÁıµÈ ÄÄÆ÷³ÍÆ® ¼øÈ¸ÇÏ¸é¼­ µî·ÏµÈ¸ÊÀÇ Å°¹ÙÀÎµùµÈ Å°°¡ Ã¼Å©µÉ½Ã 
	//ÄÄÆ÷³ÍÆ® ÁÖÀÎÀÇ ½ºÅ©¸³Æ®µéÀ» ¼øÈ¸ÇÏ¸é¼­ ÀÖ´Â ÇÔ¼ö ½ÇÇà½ÃÄÑÁÜ
};

