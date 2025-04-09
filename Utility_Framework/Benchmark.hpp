#pragma once
#include <chrono>

class Benchmark final
{
private:
	using _clock = std::chrono::high_resolution_clock;
	using _duration = std::chrono::duration<double, std::milli>;
	using _timePoint = std::chrono::high_resolution_clock::time_point;
public:
	Benchmark() : _start(_clock::now()) {}
	~Benchmark() = default;

	double GetElapsedTime()
	{
		_duration elapsed = _clock::now() - _start;
		return elapsed.count();
	}

private:
	_timePoint _start;
};
