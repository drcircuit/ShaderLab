#include "timer.h"

Timer::Timer() {
	start = std::chrono::high_resolution_clock::now();
	stop = std::chrono::high_resolution_clock::now();
	lastFrame = std::chrono::high_resolution_clock::now();
	fps = 0;
	frames = 0;
}
float Timer::GetMilisecondsElapsed() {
	if (isRunning) {
		auto currentTime = std::chrono::high_resolution_clock::now();
		return std::chrono::duration<float>(currentTime - start).count();
	}
	else {
		return std::chrono::duration<float>(stop - start).count();
	}
}
void Timer::Tick() {
	frames++;
}
int Timer::GetFPS() {
	auto currentTime = std::chrono::high_resolution_clock::now();
	if (std::chrono::duration<float>(currentTime - lastFrame).count() >= 1.0f) {
		lastFrame = std::chrono::high_resolution_clock::now();
		fps = frames;
		frames = 0;
	}
	return fps;
}
void Timer::Restart() {
	frames = 0;
	start = std::chrono::high_resolution_clock::now();
	isRunning = true;
}
bool Timer::Stop() {
	if (!isRunning) {
		return false;
	}
	stop = std::chrono::high_resolution_clock::now();
	isRunning = false;
	return true;
}
bool Timer::Start() {
	if (isRunning) {
		return false;
	}
	start = std::chrono::high_resolution_clock::now();
	isRunning = true;
	return true;
}
