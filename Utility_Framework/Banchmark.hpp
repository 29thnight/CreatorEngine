#pragma once
#include <chrono>

class Banchmark final
{
private:
	using _clock = std::chrono::high_resolution_clock;
	using _duration = std::chrono::duration<double, std::milli>;
	using _timePoint = std::chrono::high_resolution_clock::time_point;
public:
	Banchmark() : _start(_clock::now()) {}
	~Banchmark()
	{
		_duration elapsed = _clock::now() - _start;
		printf("Banchmark Elapsed time: %f ms\n", elapsed.count());
	}

	double GetElapsedTime()
	{
		_duration elapsed = _clock::now() - _start;
		return elapsed.count();
	}

private:
	_timePoint _start;
};