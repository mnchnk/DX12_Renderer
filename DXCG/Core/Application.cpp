#include "Core/Application.h"
#include "Core/GameTimer.h"
#include "Graphics/Renderer.h"

Application::Application(HWND hWnd, UINT clientWidth, UINT clientHeight)
{
    mRenderer = std::make_unique<Renderer>(hWnd, clientWidth, clientHeight);
    mTimer = std::make_unique<GameTimer>();
}

Application::~Application() = default;

bool Application::Initialize()
{
    return mRenderer->Initialize();
}

int Application::Run()
{
    MSG msg = { 0 };

    mTimer->Reset();

    while (msg.message != WM_QUIT)
    {
        if (PeekMessage(&msg, 0, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        else
        {
            mTimer->Tick();

            // Renderer는 Timer 객체를 모른다. 필요한 값(dt)만 받는다.
            mRenderer->Update(mTimer->DeltaTime());
            mRenderer->Draw();
        }
    }

    return (int)msg.wParam;
}

void Application::OnMouseDown(int x, int y)
{
    mRenderer->Pick(x, y);
}
