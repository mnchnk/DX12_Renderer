#pragma once
#include <Windows.h>

class GameTimer
{
public:
	GameTimer();
	~GameTimer() = default;
	GameTimer(const GameTimer& rhs) = delete;
	GameTimer& operator=(const GameTimer& rhs) = delete;

	float TotalTime() const;
	float DeltaTime() const;

	void Reset();
	void Start();
	void Stop();
	void Tick();

private:
	double mSecondsPerCount;
	double mDeltaTime;

	__int64 mBaseTime;
	__int64 mPausedTime;
	__int64 mStopTime;
	__int64 mPrevTime;
	__int64 mCurrTime;

	bool mStopped;
};

