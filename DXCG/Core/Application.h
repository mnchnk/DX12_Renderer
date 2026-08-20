#pragma once
#include <Windows.h>   // HWND, UINT
#include <memory>

class Renderer;
class GameTimer;

// 윈도우/메시지 루프/시간을 담당한다.
// 렌더링은 Renderer가 맡고, Application은 매 프레임 dt만 넘겨준다.
class Application
{
public:
	Application(HWND hWnd, UINT clientWidth, UINT clientHeight);
	~Application();
	Application(const Application& rhs) = delete;
	Application& operator=(const Application& rhs) = delete;

	bool Initialize();
	int Run();

	Renderer* GetRenderer() const { return mRenderer.get(); }
	void OnMouseDown(int x, int y);

private:
	std::unique_ptr<Renderer> mRenderer;
	std::unique_ptr<GameTimer> mTimer;
};
