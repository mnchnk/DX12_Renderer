#pragma comment(linker, "/SUBSYSTEM:WINDOWS")

#include <Windows.h>
#include <crtdbg.h>
#include "core/Application.h"
#include "Core/InputManager.h"
#include <imgui.h>
#include <memory>

// imgui_impl_win32.h에 선언이 없어서 관례적으로 직접 extern 선언한다.
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(
    HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

// 주 창의 핸들
HWND ghMainWnd = 0;

bool InitWindowsApp(HINSTANCE instanceHandle, int show);
int Run();

LRESULT CALLBACK
WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

std::unique_ptr<Application> theApp = nullptr;

int WINAPI
WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, PSTR pCmdLine, int nShowCmd)
{
#if defined(DEBUG) | defined(_DEBUG)
	_CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
#endif

	if (!InitWindowsApp(hInstance, nShowCmd))
		return 0;

	theApp = std::make_unique<Application>(ghMainWnd, 800, 600);

	if (!theApp->Initialize())
		return 0;

	return theApp->Run();
}

bool InitWindowsApp(HINSTANCE instanceHandle, int show)
{
	WNDCLASS wc;

	wc.style = CS_HREDRAW | CS_VREDRAW;
	wc.lpfnWndProc = WndProc;
	wc.cbClsExtra = 0;
	wc.cbWndExtra = 0;
	wc.hInstance = instanceHandle;
	wc.hIcon = LoadIcon(0, IDI_APPLICATION);
	wc.hCursor = LoadCursor(0, IDC_ARROW);
	wc.hbrBackground = (HBRUSH)GetStockObject(WHITE_BRUSH);
	wc.lpszMenuName = 0;
	wc.lpszClassName = L"BasicWndClass";

	if (!RegisterClass(&wc))
	{
		MessageBox(0, L"RegisterClass FAILED", 0, 0);
		return false;
	}

	RECT R = { 0, 0, 800, 600 };
	AdjustWindowRect(&R, WS_OVERLAPPEDWINDOW, false);
	int width = R.right - R.left;
	int height = R.bottom - R.top;

	ghMainWnd = CreateWindow(
		L"BasicWndClass",
		L"Win32Basic",
		WS_OVERLAPPEDWINDOW,
		CW_USEDEFAULT,
		CW_USEDEFAULT,
		width,
		height,
		0,
		0,
		instanceHandle,
		0);

	if (ghMainWnd == 0)
	{
		MessageBox(0, L"CreateWindow FAILED", 0, 0);
		return false;
	}

	ShowWindow(ghMainWnd, show);
	UpdateWindow(ghMainWnd);

	return true;
}

LRESULT CALLBACK
WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	// ImGui가 먼저 메시지를 본다. 이게 없으면 UI가 그려지기만 하고 조작이 안 된다.
	// ImGui가 소비한 메시지는 게임 입력으로 넘기지 않는다.
	if (ImGui::GetCurrentContext() != nullptr &&
		ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
		return true;

	switch (msg)
	{
	case WM_KEYDOWN:
		InputManager::GetInstance()->SetKeyDown((int)wParam);
		return 0;

	case WM_KEYUP:
		InputManager::GetInstance()->SetKeyUp((int)wParam);
		return 0;
	
	case WM_MOUSEMOVE:
		InputManager::GetInstance()->UpdateMousePos(LOWORD(lParam), HIWORD(lParam));
		return 0;

	case WM_LBUTTONDOWN:
		InputManager::GetInstance()->SetLeftMouseDown(true);
		if(theApp != nullptr)
			theApp->OnMouseDown(LOWORD(lParam), HIWORD(lParam));
		return 0;

	case WM_LBUTTONUP:
		InputManager::GetInstance()->SetLeftMouseDown(false);
		return 0;
	
	case WM_RBUTTONDOWN:
		InputManager::GetInstance()->SetRightMouseDown(true);
		return 0;

	case WM_RBUTTONUP:
		InputManager::GetInstance()->SetRightMouseDown(false);
		return 0;

	case WM_DESTROY:
		PostQuitMessage(0);
		return 0;
	}
	return DefWindowProc(hWnd, msg, wParam, lParam);
}

