#pragma once
#include "Core.Minimal.h"



//csv�� �����̸��� �о �����صδ¿� 
class SoundNames
{
public:
	SoundNames();
	~SoundNames();
	void LoadSoundNameFromCSV();
	//�ش� �±׿� �ִ� �����̸��� �ϳ�
	std::string GetSoudNameRandom(const std::string& soundTag); 
	//�ش��±׿��ִ� �����̸�vector
	std::vector<std::string> GetSoundNames(const std::string& soundTag); 
	std::unordered_map<std::string, std::vector<std::string>> soundMap;
};
