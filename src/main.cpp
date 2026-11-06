#ifdef _WIN32
#define UNICODE
#define _UNICODE
#include <windows.h>
#include <windowsx.h>
#include <commdlg.h>

#include "ipa_loader.hpp"

#include <filesystem>
#include <string>

namespace {
constexpr int SCREEN_WIDTH = 320;
constexpr int SCREEN_HEIGHT = 480;
bool appOpen = false;
std::wstring ipaStatus = L"Ctrl+O — открыть IPA";

std::wstring utf8ToWide(const std::string& value) {
    if (value.empty()) return {};
    const int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                                         value.data(), static_cast<int>(value.size()),
                                         nullptr, 0);
    if (size <= 0) {
        return L"[текст в неизвестной кодировке]";
    }
    std::wstring result(static_cast<std::size_t>(size), L'\\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                        value.data(), static_cast<int>(value.size()),
                        result.data(), size);
    return result;
}

void centeredText(HDC dc, const wchar_t* value, int y, int height,
                  COLORREF color, int fontSize, bool bold = false) {
    HFONT font = CreateFontW(-fontSize, 0, 0, 0, bold ? FW_BOLD : FW_NORMAL,
                             FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                             OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                             CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    HFONT oldFont = static_cast<HFONT>(SelectObject(dc, font));
    SetTextColor(dc, color);
    SetBkMode(dc, TRANSPARENT);
    RECT rect{0, y, SCREEN_WIDTH, y + height};
    DrawTextW(dc, value, -1, &rect, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
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

void openIpa(HWND window) {
    wchar_t fileName[32768]{};
    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = window;
    dialog.lpstrFilter = L"iOS application (*.ipa)\0*.ipa\0All files (*.*)\0*.*\0";
    dialog.lpstrFile = fileName;
    dialog.nMaxFile = 32768;
    dialog.lpstrTitle = L"Select an IPA file";
    dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;

    if (!GetOpenFileNameW(&dialog)) return;

    IpaInfo info;
    if (inspectIpa(std::filesystem::path(fileName), info)) {
        ipaStatus = L"IPA: найдено приложение";
        const std::wstring details =
            L"IPA archive opened successfully.\n\nApp bundle:\n" +
            utf8ToWide(info.appBundlePath) +
            L"\n\nInfo.plist:\n" +
            utf8ToWide(info.infoPlistPath) +
            L"\n\nFiles in archive: " + std::to_wstring(info.fileCount) +
            L"\n\nThe app has not been executed.";
        MessageBoxW(window, details.c_str(), L"IPA Loader", MB_OK | MB_ICONINFORMATION);
    } else {
        ipaStatus = L"IPA: ошибка проверки";
        const std::wstring details = utf8ToWide(info.error);
        MessageBoxW(window, details.c_str(), L"IPA Loader", MB_OK | MB_ICONWARNING);
    }
    InvalidateRect(window, nullptr, TRUE);
}

void drawScreen(HDC dc) {
    RECT screen{0, 0, SCREEN_WIDTH, SCREEN_HEIGHT};
    HBRUSH background = CreateSolidBrush(RGB(238, 241, 246));
    FillRect(dc, &screen, background);
    DeleteObject(background);

    HBRUSH status = CreateSolidBrush(RGB(248, 249, 251));
    RECT statusRect{0, 0, SCREEN_WIDTH, 25};
    FillRect(dc, &statusRect, status);
    DeleteObject(status);
    centeredText(dc, L"9:41", 1, 22, RGB(25, 28, 32), 13, true);

    if (!appOpen) {
        centeredText(dc, L"OldiOS", 44, 32, RGB(35, 40, 48), 21, true);
        roundedBox(dc, 120, 125, 200, 205, 17, RGB(35, 92, 180));
        centeredText(dc, L"i", 127, 68, RGB(255, 255, 255), 48, true);
        centeredText(dc, L"I Am Rich", 210, 25, RGB(35, 40, 48), 13);
        centeredText(dc, ipaStatus.c_str(), 340, 35, RGB(75, 85, 100), 10);
        centeredText(dc, L"Ctrl+O — выбрать IPA-файл", 375, 25,
                     RGB(90, 98, 110), 10);
    } else {
        centeredText(dc, L"Done", 28, 35, RGB(30, 100, 205), 16);
        centeredText(dc, L"I Am Rich", 82, 34, RGB(30, 35, 45), 22, true);
        centeredText(dc, L"Экран-заглушка приложения", 132, 28,
                     RGB(95, 100, 110), 13);
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

    HBRUSH dock = CreateSolidBrush(RGB(220, 225, 233));
    RECT dockRect{0, 440, SCREEN_WIDTH, SCREEN_HEIGHT};
    FillRect(dc, &dockRect, dock);
    DeleteObject(dock);
    roundedBox(dc, 135, 452, 185, 468, 8, RGB(105, 112, 122));
}

LRESULT CALLBACK windowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_KEYDOWN:
        if ((GetKeyState(VK_CONTROL) & 0x8000) && wParam == 'O') {
            openIpa(window);
            return 0;
        }
        break;
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
        if (!appOpen && x >= 110 && x <= 210 && y >= 115 && y <= 220) {
            appOpen = true;
            InvalidateRect(window, nullptr, TRUE);
        } else if (appOpen && y < 65) {
            appOpen = false;
            InvalidateRect(window, nullptr, TRUE);
        } else if (y >= 440) {
            appOpen = false;
            InvalidateRect(window, nullptr, TRUE);
        } else if (!appOpen && y >= 330 && y <= 410) {
            openIpa(window);
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

int WINAPI WinMain(HINSTANCE instance, HINSTANCE, LPSTR, int showCommand) {
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
#include "ipa_loader.hpp"
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
        std::cout << "IPA loader: ZIP archive structure inspection is available.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Emulator error: " << error.what() << '\n';
        return 1;
    }
}
#endif
