#include "SoundName.h"
#include "pch.h"
#include "Core.Random.h"
//std::vector<std::string> dashSounds
//{
//	"Dodge 1_Movement_01",
//	"Dodge 1_Movement_02",
//	"Dodge 1_Movement_03"
//};
//std::vector<std::string> stepSounds
//{
//	"Step_Movement_Small_01",
//	"Step_Movement_Small_02",
//	"Step_Movement_Small_03"
//
//};
//std::vector<std::string> normalBulletSounds
//{
//	"Electric_Attack_01",
//	"Electric_Attack_02",
//	"Electric_Attack_03"
//
//
//};
//std::vector<std::string> specialBulletSounds
//{
//	"Electric_Skill_01",
//	"Electric_Skill_02"
//};

//std::vector<std::string> RangeChargeSounds
//{
//	"Articulated Sounds - Occult Master - Electric Energy Projectile Impact Multiple Variations3shots",
//};
////근접공격은 그냥 직접대입해놨음 수정시 각PlaySlashEvent 찾아가서 수정
//std::vector<std::string> MeleeChargeSounds
//{
//	"Nunchaku Attack_Skill_Bass_01",
//	"Nunchaku Attack_Skill_Bass_02",
//	"Nunchaku Attack_Skill_Bass_03"
//
//};
//std::vector<std::string> MeleeChargingSounds
//{
//	"S2_Cute_P1_Charge_v1",
//	"S2_Cute_P1_Charge_v2",
//};
//std::vector<std::string> weaponSwapSounds
//{
//	"Notification_Thud_Deep_Impact_Metal_Sweep_01_01",
//	"Notification_Thud_Deep_Impact_Metal_Sweep_01_02"
//};
//std::vector<std::string> WeaponBreakSounds
//{
//	"Foley_Break_Vessel_Pot_Ceramic_Debris_Light_01_01",
//	"Foley_Break_Vessel_Pot_Ceramic_Debris_Light_01_02",
//	"Foley_Break_Vessel_Pot_Ceramic_Debris_Light_01_03"
//};
//std::vector<std::string> DamageSounds
//{
//	"ARMOR Hit Leather 01",
//	"ARMOR Hit Leather 02",
//	"ARMOR Hit Leather 03"
//};
//
//
//std::vector<std::string> MeleeStrikeSounds
//{
//	"SWORD Hit Metal Sword Large 01",
//	"SWORD Hit Metal Sword Large 02",
//	"SWORD Hit Metal Sword Large 03",
//	"SWORD Hit Metal Sword Large 04",
//	"SWORD Hit Metal Sword Large 05",
//	"SWORD Hit Metal Sword Large 06"
//
//};
//
//std::vector<std::string> ExplosionSounds
//{
//	"Designed_Skill_Explode_Shield_Hit_Hard_Flame_Fire_Burst_01_01",
//	"Designed_Skill_Explode_Shield_Hit_Hard_Flame_Fire_Burst_01_02",
//	"Designed_Skill_Explode_Shield_Hit_Hard_Flame_Fire_Burst_01_03"
//
//};

void SoundNames::LoadSoundNameFromCSV()
{
	soundMap.clear();
	auto path = PathFinder::Relative("CSV\\SoundName.csv");
	CSVReader rdrNew(path.string());
	for (auto& row : rdrNew)
	{
		std::string soundTag = row[0].as<std::string>();
		if (!soundTag.empty() && soundTag.back() == '\0')
			soundTag.pop_back();
		std::vector<std::string> sounds;
		
		for (int i = 1; i < row.GetSize(); ++i)
		{
			std::string cell = row[i].as<std::string>();
			if (!cell.empty() && cell.back() == '\0')
				cell.pop_back();
			if (cell.empty()) continue;
			sounds.push_back(cell);
		}



		//for (int i = 1; i < row.GetSize(); ++i)
		//{
		//	std::string cell = row[i].as<std::string>();

		//	// 문자열이 비어있거나, 모든 문자가 공백(스페이스/탭/개행 등)인 경우 건너뜀
		//	bool onlySpaces = std::all_of(cell.begin(), cell.end(),
		//		[](unsigned char c) { return std::isspace(c); });

		//	if (cell.empty() || onlySpaces)
		//		continue; // 빈칸 또는 공백만 있는 칸이면 skip

		//	sounds.push_back(cell);
		//}

		soundMap[soundTag] = sounds;
	}
	
}

std::string SoundNames::GetSoudNameRandom(const std::string& soundTag)
{
	auto it = soundMap.find(soundTag);
	if (it == soundMap.end() || it->second.empty()) return "";
	auto& sounds = it->second;
	if (sounds.size() == 1)
		return sounds[0];
	int index = Random<int>(0, static_cast<int>(sounds.size()) - 1).Generate();
	return sounds[index];
}

std::vector<std::string> SoundNames::GetSoundNames(const std::string& soundTag)
{
	auto it = soundMap.find(soundTag);
	if (it == soundMap.end() || it->second.empty())
		return {}; 

	return it->second; 
}
