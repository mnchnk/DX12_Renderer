#pragma once
#include <Windows.h>

class InputManager
{
public:
	InputManager(const InputManager& rhs) = delete;
	InputManager& operator=(const InputManager& rhs) = delete;
	
	static InputManager* GetInstance()
	{
		static InputManager instance;
		return &instance;
	}

	void SetKeyDown(int key) { mKeyStates[key] = true; }
	void SetKeyUp(int key) { mKeyStates[key] = false; }
	void UpdateMousePos(int x, int y) 
	{
		mMouseX = x, mMouseY = y;
		
		mMouseDeltaX = mMouseX - mLastMouseX;
		mMouseDeltaY = mMouseY - mLastMouseY;
	
		mLastMouseX = mMouseX;
		mLastMouseY = mMouseY;
	}
	void ClearDeltas() { mMouseDeltaX = 0, mMouseDeltaY = 0; }
	void SetLeftMouseDown(bool isDown) { mIsLeftMouseDown = isDown; }
	void SetRightMouseDown(bool isDown) { mIsRightMouseDown = isDown; }

	bool IsKeyDown(int key) const { return mKeyStates[key]; }
	int GetMouseX() const { return mMouseX; }
	int GetMouseY() const { return mMouseY; }
	int GetMouseDeltaX() const { return mMouseDeltaX; }
	int GetMouseDeltaY() const { return mMouseDeltaY; }
	bool IsLeftMouseDown() const { return mIsLeftMouseDown; }
	bool IsRightMouseDown() const { return mIsRightMouseDown; }

private:
	InputManager() = default;
	~InputManager() = default;

	bool mKeyStates[256] = { false };
	
	int mMouseX = 0;
	int mMouseY = 0;

	int mMouseDeltaX = 0;
	int mMouseDeltaY = 0;

	int mLastMouseX = 0;
	int mLastMouseY = 0;

	bool mIsLeftMouseDown = false;
	bool mIsRightMouseDown = false;
};

