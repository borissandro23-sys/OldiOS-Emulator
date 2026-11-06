#ifdef _WIN32
#define UNICODE
#define _UNICODE
#include <windows.h>
#include <windowsx.h>

namespace {
constexpr int SCREEN_WIDTH = 320;
constexpr int SCREEN_HEIGHT = 480;
bool appOpen = false;

void centeredText(HDC dc, const wchar_t* value, int y, int height,
                  COLORREF color, int fontSize, bool bold = false) {
    HFONT font = CreateFontW(-fontSize, 0, 0, 0, bold ? FW_BOLD : FW_NORMAL,
                             FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                             OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                             CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Arial");
    HFONT oldFont = static_cast<HFONT>(SelectObject(dc, font));
    SetTextColor(dc, color);
    SetBkMode(dc, TRANSPARENT);
    RECT rect{0, y, SCREEN_WIDTH, y + height};
    DrawTextW(dc, value, -1, &rect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    SelectObject(dc, oldFont);
    DeleteObject(font);
}

void roundedBox(HDC dc, int left, int top, int right, int bottom,
                int radius, COLORREF color) {
    HBRUSH brush = CreateSolidBrush(color);
    HBRUSH oldBrush = static_cast<HBRUSH>(SelectObject(dc, brush));
    HPEN pen = CreatePen(PS_SOLID, 1, color);
    HPEN oldPen = static_cast<HPEN>(SelectObject(dc, pen));
    RoundRect(dc, left, top, right, bottom, radius, radius);
    SelectObject(dc, oldBrush);
    SelectObject(dc, oldPen);
    DeleteObject(brush);
    DeleteObject(pen);
}

void drawScreen(HDC dc) {
    RECT screen{0, 0, SCREEN_WIDTH, SCREEN_HEIGHT};
    HBRUSH background = CreateSolidBrush(RGB(238, 241, 246));
    FillRect(dc, &screen, background);
    DeleteObject(background);

    // Status bar
    HBRUSH status = CreateSolidBrush(RGB(248, 249, 251));
    RECT statusRect{0, 0, SCREEN_WIDTH, 25};
    FillRect(dc, &statusRect, status);
    DeleteObject(status);
    centeredText(dc, L"9:41", 1, 22, RGB(25, 28, 32), 13, true);

    if (!appOpen) {
        centeredText(dc, L"OldiOS", 44, 32, RGB(35, 40, 48), 21, true);
        // App icon
        roundedBox(dc, 120, 135, 200, 215, 17, RGB(35, 92, 180));
        centeredText(dc, L"i", 137, 68, RGB(255, 255, 255), 48, true);
        centeredText(dc, L"I Am Rich", 220, 25, RGB(35, 40, 48), 13);
        centeredText(dc, L"Нажмите на значок приложения", 300, 30,
                     RGB(90, 98, 110), 12);
    } else {
        centeredText(dc, L"Done", 28, 35, RGB(30, 100, 205), 16);
        centeredText(dc, L"I Am Rich", 82, 34, RGB(30, 35, 45), 22, true);
        centeredText(dc, L"Экран-заглушка приложения", 132, 28,
                     RGB(95, 100, 110), 13);
        // A simple blue jewel placeholder
        POINT jewel[] = {{160, 205}, {205, 250}, {160, 300}, {115, 250}};
        HBRUSH jewelBrush = CreateSolidBrush(RGB(40, 105, 210));
        HPEN jewelPen = CreatePen(PS_SOLID, 2, RGB(220, 232, 255));
        HBRUSH oldBrush = static_cast<HBRUSH>(SelectObject(dc, jewelBrush));
        HPEN oldPen = static_cast<HPEN>(SelectObject(dc, jewelPen));
        Polygon(dc, jewel, 4);
        SelectObject(dc, oldBrush);
        SelectObject(dc, oldPen);
        DeleteObject(jewelBrush);
        DeleteObject(jewelPen);
    }

    // Home button area
    HBRUSH dock = CreateSolidBrush(RGB(220, 225, 233));
    RECT dockRect{0, 440, SCREEN_WIDTH, SCREEN_HEIGHT};
    FillRect(dc, &dockRect, dock);
    DeleteObject(dock);
    roundedBox(dc, 135, 452, 185, 468, 8, RGB(105, 112, 122));
}

LRESULT CALLBACK windowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_PAINT: {
        PAINTSTRUCT paint{};
        HDC dc = BeginPaint(window, &paint);
        drawScreen(dc);
        EndPaint(window, &paint);
        return 0;
    }
    case WM_LBUTTONDOWN: {
        const int x = GET_X_LPARAM(lParam);
        const int y = GET_Y_LPARAM(lParam);
        if (!appOpen && x >= 110 && x <= 210 && y >= 125 && y <= 230) {
            appOpen = true;
            InvalidateRect(window, nullptr, TRUE);
        } else if (appOpen && y < 65) {
            appOpen = false;
            InvalidateRect(window, nullptr, TRUE);
        } else if (y >= 440) {
            appOpen = false;
            InvalidateRect(window, nullptr, TRUE);
        }
        return 0;
    }
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    default:
        return DefWindowProcW(window, message, wParam, lParam);
    }
}
} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int showCommand) {
    const wchar_t CLASS_NAME[] = L"OldiOSEmulatorWindow";
    WNDCLASSW wc{};
    wc.lpfnWndProc = windowProc;
    wc.hInstance = instance;
    wc.lpszClassName = CLASS_NAME;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);

    if (!RegisterClassW(&wc)) return 1;

    RECT bounds{0, 0, SCREEN_WIDTH, SCREEN_HEIGHT};
    AdjustWindowRect(&bounds, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, FALSE);

    HWND window = CreateWindowExW(
        0, CLASS_NAME, L"OldiOS Emulator — iPhone 320x480",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        CW_USEDEFAULT, CW_USEDEFAULT,
        bounds.right - bounds.left, bounds.bottom - bounds.top,
        nullptr, nullptr, instance, nullptr);

    if (!window) return 1;
    ShowWindow(window, showCommand);
    UpdateWindow(window);

    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    return static_cast<int>(message.wParam);
}
#else
#include "armv6.hpp"
#include <exception>
#include <iomanip>
#include <iostream>
#include <vector>

int main() {
    try {
        Armv6 cpu;
        const std::vector<std::uint32_t> program = {
            0xE3A00005u, 0xE3A01007u, 0xE0802001u, 0xE3A04C01u,
            0xE5842000u, 0xE3A02000u, 0xE5943000u, 0xE2433002u,
            0xE353000Au, 0xFFFFFFFFu
        };
        cpu.loadProgram(program);
        cpu.run(100);
        std::cout << "\nOldiOS-Emulator | ARMv6 interpreter prototype\n";
        for (unsigned i = 0; i < 8; ++i) {
            std::cout << "r" << i << " = 0x" << std::hex
                      << std::setw(8) << std::setfill('0')
                      << cpu.reg(i) << std::dec << '\n';
        }
        std::cout << "CPSR = 0x" << std::hex << cpu.cpsr() << std::dec << '\n';
        std::cout << "Expected: r2 = 0, r3 = 10, Z flag = 1\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Emulator error: " << error.what() << '\n';
        return 1;
    }
}
#endif
