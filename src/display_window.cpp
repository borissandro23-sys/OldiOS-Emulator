#include "display_window.hpp"

#include <stdexcept>

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>

namespace {
struct WindowState {
    unsigned width;
    unsigned height;
};

LRESULT CALLBACK displayProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    auto* state = reinterpret_cast<WindowState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
        state = static_cast<WindowState*>(create->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
    }

    switch (message) {
    case WM_PAINT: {
        PAINTSTRUCT paint{};
        HDC dc = BeginPaint(hwnd, &paint);
        RECT client{};
        GetClientRect(hwnd, &client);
        FillRect(dc, &client, reinterpret_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));

        if (state) {
            const int availableW = client.right - client.left;
            const int availableH = client.bottom - client.top;
            const double scaleX = static_cast<double>(availableW) / state->width;
            const double scaleY = static_cast<double>(availableH) / state->height;
            const double scale = (scaleX < scaleY ? scaleX : scaleY);
            const int displayW = static_cast<int>(state->width * scale);
            const int displayH = static_cast<int>(state->height * scale);
            RECT screen{
                (availableW - displayW) / 2,
                (availableH - displayH) / 2,
                (availableW + displayW) / 2,
                (availableH + displayH) / 2
            };
            HBRUSH screenBrush = CreateSolidBrush(RGB(238, 238, 238));
            FillRect(dc, &screen, screenBrush);
            DeleteObject(screenBrush);

            SetBkMode(dc, TRANSPARENT);
            SetTextColor(dc, RGB(45, 45, 45));
            const wchar_t* title = L"Virtual iOS display";
            DrawTextW(dc, title, -1, &screen, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }
        EndPaint(hwnd, &paint);
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    default:
        return DefWindowProcW(hwnd, message, wParam, lParam);
    }
}
}
#endif

DisplayWindow::DisplayWindow(unsigned logicalWidth, unsigned logicalHeight)
    : width_(logicalWidth), height_(logicalHeight) {
    if (width_ == 0 || height_ == 0 || width_ > 8192 || height_ > 8192) {
        throw std::runtime_error("Virtual display dimensions must be between 1 and 8192.");
    }
}

int DisplayWindow::run() {
#ifdef _WIN32
    SetProcessDPIAware();
    const wchar_t className[] = L"OldiOSEmulatorDisplay";
    HINSTANCE instance = GetModuleHandleW(nullptr);
    WNDCLASSW wc{};
    wc.lpfnWndProc = displayProc;
    wc.hInstance = instance;
    wc.lpszClassName = className;
    wc.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
    wc.hbrBackground = reinterpret_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
    if (!RegisterClassW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        throw std::runtime_error("Could not register display window class.");
    }

    WindowState state{width_, height_};
    RECT rect{0, 0, static_cast<LONG>(width_), static_cast<LONG>(height_)};
    AdjustWindowRect(&rect, WS_OVERLAPPEDWINDOW, FALSE);
    HWND hwnd = CreateWindowExW(
        0, className, L"OldiOS-Emulator | Virtual Display",
        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
        rect.right - rect.left, rect.bottom - rect.top,
        nullptr, nullptr, instance, &state);
    if (!hwnd) throw std::runtime_error("Could not create display window.");

    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);
    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    return static_cast<int>(message.wParam);
#else
    return 0;
#endif
}
