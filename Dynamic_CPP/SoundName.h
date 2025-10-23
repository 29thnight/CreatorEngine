#pragma once
#include "Core.Minimal.h"



//csv로 사운드이름들 읽어서 저장해두는용 
class SoundNames
{
public:

	void LoadSoundNameFromCSV();
	//해당 태그에 있는 사운드이름중 하나
	std::string GetSoudNameRandom(const std::string& soundTag); 
	//해당태그에있는 사운드이름vector
	std::vector<std::string> GetSoundNames(const std::string& soundTag); 
	std::unordered_map<std::string, std::vector<std::string>> soundMap;
};
//extern std::vector<std::string> dashSounds;
//extern std::vector<std::string> stepSounds;
//extern std::vector<std::string> normalBulletSounds;
//extern std::vector<std::string> specialBulletSounds;
//extern std::vector<std::string> RangeChargeSounds;
//extern std::vector<std::string> MeleeChargeSounds;
//extern std::vector<std::string> MeleeChargingSounds;
//extern std::vector<std::string> weaponSwapSounds;
//extern std::vector<std::string> WeaponBreakSounds;
//extern std::vector<std::string> DamageSounds;
//extern std::vector<std::string> MeleeStrikeSounds;
//extern std::vector<std::string> ExplosionSounds;
