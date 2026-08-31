//--------------------------------------------------------------------------------------
// Main.cpp
//
// Advanced Technology Group (ATG)
// Copyright (C) Microsoft Corporation. All rights reserved.
//--------------------------------------------------------------------------------------

#include "pch.h"
#include "Game.h"

#include <appnotify.h>

using namespace DirectX;

#ifdef __clang__
#pragma clang diagnostic ignored "-Wcovered-switch-default"
#pragma clang diagnostic ignored "-Wswitch-enum"
#endif

#pragma warning(disable : 4061)

namespace
{
    std::unique_ptr<Game> g_game;
    HANDLE g_plmSuspendComplete = nullptr;
    HANDLE g_plmSignalResume = nullptr;
}

bool g_HDRMode = false;

LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
void ExitGame() noexcept;

namespace
{
    // Updates both scrollbars to reflect the current texture size vs. client area.
    void UpdateScrollBars(HWND hWnd, Game* game)
    {
        if (!game)
            return;

        RECT rc;
        GetClientRect(hWnd, &rc);
        const int clientW = rc.right  - rc.left;
        const int clientH = rc.bottom - rc.top;
        const int texW    = game->GetTextureWidth();
        const int texH    = game->GetTextureHeight();

        SCROLLINFO si = {};
        si.cbSize = sizeof(si);
        si.fMask  = SIF_RANGE | SIF_PAGE;

        si.nMin  = 0;
        si.nMax  = (texW > 0) ? texW - 1 : 0;
        si.nPage = static_cast<UINT>(clientW);
        SetScrollInfo(hWnd, SB_HORZ, &si, TRUE);

        si.nMin  = 0;
        si.nMax  = (texH > 0) ? texH - 1 : 0;
        si.nPage = static_cast<UINT>(clientH);
        SetScrollInfo(hWnd, SB_VERT, &si, TRUE);
    }

    // Clamps the scroll position after a scroll message and returns the new position.
    int HandleScrollMessage(HWND hWnd, int bar, WPARAM wParam, int lineStep)
    {
        SCROLLINFO si = {};
        si.cbSize = sizeof(si);
        si.fMask  = SIF_ALL;
        GetScrollInfo(hWnd, bar, &si);

        int pos = si.nPos;
        const int maxPos = si.nMax - static_cast<int>(si.nPage) + 1;

        switch (LOWORD(wParam))
        {
        case SB_LINEUP:        pos -= lineStep;              break;
        case SB_LINEDOWN:      pos += lineStep;              break;
        case SB_PAGEUP:        pos -= static_cast<int>(si.nPage); break;
        case SB_PAGEDOWN:      pos += static_cast<int>(si.nPage); break;
        case SB_THUMBTRACK:    pos  = HIWORD(wParam);        break;
        case SB_TOP:           pos  = si.nMin;               break;
        case SB_BOTTOM:        pos  = si.nMax;               break;
        default:                                             break;
        }

        pos = std::max(si.nMin, std::min(pos, std::max(0, maxPos)));

        si.fMask = SIF_POS;
        si.nPos  = pos;
        SetScrollInfo(hWnd, bar, &si, TRUE);
        return pos;
    }
}

// Entry point
int WINAPI wWinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE, _In_ LPWSTR lpCmdLine, _In_ int nCmdShow)
{
    if (!XMVerifyCPUSupport())
    {
        #ifdef _DEBUG
        OutputDebugStringA("ERROR: This hardware does not support the required instruction set.\n");
        #endif
        return 1;
    }

    g_game = std::make_unique<Game>();

    // Register class and create window
    PAPPSTATE_REGISTRATION hPLM = {};
    PAPPCONSTRAIN_REGISTRATION hPLM2 = {};

    {
        // Register class
        WNDCLASSEXW wcex = {};
        wcex.cbSize = sizeof(WNDCLASSEXW);
        wcex.style = CS_HREDRAW | CS_VREDRAW;
        wcex.lpfnWndProc = WndProc;
        wcex.hInstance = hInstance;
        wcex.lpszClassName = L"Bc7UnshuffleWindowClass";
        wcex.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
        if (!RegisterClassExW(&wcex))
            return 1;

        int w, h;
        g_game->GetDefaultSize(w, h);
        RECT rc = { 0, 0, static_cast<LONG>(w), static_cast<LONG>(h) };
        const DWORD windowStyle = WS_OVERLAPPEDWINDOW | WS_HSCROLL | WS_VSCROLL;
        AdjustWindowRect(&rc, windowStyle, FALSE);

        // Create window
        HWND hwnd = CreateWindowExW(0, L"Bc7UnshuffleWindowClass", L"BCnUnshuffle", windowStyle,
            CW_USEDEFAULT, CW_USEDEFAULT, rc.right - rc.left, rc.bottom - rc.top, nullptr, nullptr, hInstance,
            nullptr);
        if (!hwnd)
            return 1;

        ShowWindow(hwnd, nCmdShow);

        SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(g_game.get()));

        g_game->Initialize(hwnd, lpCmdLine);

        // Configure scrollbars based on loaded texture dimensions
        UpdateScrollBars(hwnd, g_game.get());

        g_plmSuspendComplete = CreateEventEx(nullptr, nullptr, 0, EVENT_MODIFY_STATE | SYNCHRONIZE);
        g_plmSignalResume = CreateEventEx(nullptr, nullptr, 0, EVENT_MODIFY_STATE | SYNCHRONIZE);
        if (!g_plmSuspendComplete || !g_plmSignalResume)
            return 1;

        if (RegisterAppStateChangeNotification([](BOOLEAN quiesced, PVOID context)
        {
            if (quiesced)
            {
                ResetEvent(g_plmSuspendComplete);
                ResetEvent(g_plmSignalResume);

                // To ensure we use the main UI thread to process the notification, we self-post a message
                PostMessage(reinterpret_cast<HWND>(context), WM_USER, 0, 0);

                // To defer suspend, you must wait to exit this callback
                std::ignore = WaitForSingleObject(g_plmSuspendComplete, INFINITE);
            }
            else
            {
                SetEvent(g_plmSignalResume);
            }
        }, hwnd, &hPLM))
            return 1;

        if (RegisterAppConstrainedChangeNotification([](BOOLEAN constrained, PVOID context)
        {
            // To ensure we use the main UI thread to process the notification, we self-post a message
            SendMessage(reinterpret_cast<HWND>(context), WM_USER + 1, (constrained) ? 1u : 0u, 0);
        }, hwnd, &hPLM2))
            return 1;
    }

    // Main message loop
    MSG msg = {};
    while (WM_QUIT != msg.message)
    {
        if (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
        else
        {
            g_game->Tick();
        }
    }

    g_game.reset();

    UnregisterAppStateChangeNotification(hPLM);
    UnregisterAppConstrainedChangeNotification(hPLM2);

    CloseHandle(g_plmSuspendComplete);
    CloseHandle(g_plmSignalResume);

    return static_cast<int>(msg.wParam);
}

// Windows procedure
LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    static bool s_in_sizemove = false;

    auto game = reinterpret_cast<Game*>(GetWindowLongPtr(hWnd, GWLP_USERDATA));

    switch (message)
    {
    case WM_PAINT:
        if (s_in_sizemove && game)
        {
            game->Tick();
        }
        else
        {
            PAINTSTRUCT ps;
            std::ignore = BeginPaint(hWnd, &ps);
            EndPaint(hWnd, &ps);
        }
        break;


    case WM_MOVE:
        if (game)
        {
            game->OnWindowMoved();
        }
        break;

    case WM_ENTERSIZEMOVE:
        s_in_sizemove = true;
        break;

    case WM_SIZE:
        if (game)
        {
            game->OnWindowSizeChanged(LOWORD(lParam), HIWORD(lParam));
            UpdateScrollBars(hWnd, game);
        }
        break;

    case WM_EXITSIZEMOVE:
        s_in_sizemove = false;
        if (game)
        {
            RECT rc;
            GetClientRect(hWnd, &rc);

            game->OnWindowSizeChanged(rc.right - rc.left, rc.bottom - rc.top);
            UpdateScrollBars(hWnd, game);
        }
        break;

    case WM_HSCROLL:
        if (game)
        {
            SCROLLINFO si = {};
            si.cbSize = sizeof(si);
            si.fMask  = SIF_POS;
            GetScrollInfo(hWnd, SB_VERT, &si);
            const int newX = HandleScrollMessage(hWnd, SB_HORZ, wParam, 32);
            game->SetScrollOffset(newX, si.nPos);
            game->Tick();
        }
        break;

    case WM_VSCROLL:
        if (game)
        {
            SCROLLINFO si = {};
            si.cbSize = sizeof(si);
            si.fMask  = SIF_POS;
            GetScrollInfo(hWnd, SB_HORZ, &si);
            const int newY = HandleScrollMessage(hWnd, SB_VERT, wParam, 32);
            game->SetScrollOffset(si.nPos, newY);
            game->Tick();
        }
        break;

    case WM_MOUSEWHEEL:
        if (game)
        {
            const int delta     = GET_WHEEL_DELTA_WPARAM(wParam);
            const int step      = -(delta / WHEEL_DELTA) * 32;
            const bool shiftHeld = (GET_KEYSTATE_WPARAM(wParam) & MK_SHIFT) != 0;

            SCROLLINFO siH = {}, siV = {};
            siH.cbSize = sizeof(siH); siH.fMask = SIF_POS;
            siV.cbSize = sizeof(siV); siV.fMask = SIF_POS;
            GetScrollInfo(hWnd, SB_HORZ, &siH);
            GetScrollInfo(hWnd, SB_VERT, &siV);

            if (shiftHeld)
            {
                SCROLLINFO si = {};
                si.cbSize = sizeof(si);
                si.fMask  = SIF_ALL;
                GetScrollInfo(hWnd, SB_HORZ, &si);
                const int maxPos = si.nMax - static_cast<int>(si.nPage) + 1;
                const int newX   = std::max(si.nMin, std::min(si.nPos + step, std::max(0, maxPos)));
                si.fMask = SIF_POS; si.nPos = newX;
                SetScrollInfo(hWnd, SB_HORZ, &si, TRUE);
                game->SetScrollOffset(newX, siV.nPos);
            }
            else
            {
                SCROLLINFO si = {};
                si.cbSize = sizeof(si);
                si.fMask  = SIF_ALL;
                GetScrollInfo(hWnd, SB_VERT, &si);
                const int maxPos = si.nMax - static_cast<int>(si.nPage) + 1;
                const int newY   = std::max(si.nMin, std::min(si.nPos + step, std::max(0, maxPos)));
                si.fMask = SIF_POS; si.nPos = newY;
                SetScrollInfo(hWnd, SB_VERT, &si, TRUE);
                game->SetScrollOffset(siH.nPos, newY);
            }
        }
        break;

    case WM_DESTROY:
        PostQuitMessage(0);
        break;
    }

    return DefWindowProc(hWnd, message, wParam, lParam);
}

// Exit helper
void ExitGame() noexcept
{
    PostQuitMessage(0);
}
