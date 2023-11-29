#pragma once
#include <chrono>

class Timer {
public:
	Timer();
	float GetMilisecondsElapsed();
	void Restart();
	bool Stop();
	bool Start();
	int GetFPS();
	void Tick();
private:
	bool isRunning = false;
	std::chrono::time_point<std::chrono::steady_clock> start;
	std::chrono::time_point<std::chrono::steady_clock> stop;
	std::chrono::time_point<std::chrono::steady_clock> lastFrame;
	int fps = 0;
	int frames = 0;
};