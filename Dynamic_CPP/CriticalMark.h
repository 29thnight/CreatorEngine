#pragma once

//enum class CriticalMark
//{
//	P1,
//	P2,
//	None,
//};

class CriticalMark
{
public:
	CriticalMark() {};
	~CriticalMark() {};

	void UpdateMark();
	void ResetMark();
	bool TryCriticalHit(int _playerindex);
	int markIndex = -1;



};

