#ifdef _WIN32
#define UNICODE
#define _UNICODE
#include <windows.h>
#include <windowsx.h>
#include <shobjidl.h>

#include "ipa_loader.hpp"
#include "macho_loader.hpp"
#include "armv6.hpp"

#include <filesystem>
#include <string>
#include <sstream>
#include <stdexcept>
#include <iomanip>
#include <vector>
#include <algorithm>

namespace {
constexpr int SCREEN_WIDTH = 320;
constexpr int SCREEN_HEIGHT = 480;
std::wstring ipaStatus = L"Ctrl+I — установить IPA";
std::wstring installedAppName = L"I Am Rich";
std::filesystem::path installedBundlePath;

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

std::wstring armStateText(const Armv6& cpu) {
    std::wostringstream out;
    out << L"ARMv6 debugger\nPC: 0x" << std::uppercase << std::hex
        << std::setw(8) << std::setfill(L'0') << cpu.reg(15)
        << L"\nCPSR: 0x" << std::setw(8) << cpu.cpsr();
    for (unsigned i = 0; i < 16; ++i) {
        if (i % 2 == 0) out << L"\n";
        out << L"R" << std::dec << i << L"=0x" << std::uppercase << std::hex
            << std::setw(8) << std::setfill(L'0') << cpu.reg(i) << L"   ";
    }
    try {
        const bool thumb = (cpu.cpsr() & (1u << 5)) != 0;
        out << L"\n\nInstruction @ PC: 0x" << std::setw(thumb ? 4 : 8)
            << (thumb ? cpu.peek16(cpu.reg(15)) : cpu.peek32(cpu.reg(15)));
    } catch (const std::exception&) {
        out << L"\n\nInstruction: address is not mapped";
    }
    out << L"\n\nYES = execute one instruction\nNO = stop debugging\nCANCEL = close debugger";
    return out.str();
}

void showArmDebugger(HWND window, Armv6& cpu) {
    for (unsigned count = 0; count < 64 && !cpu.halted(); ++count) {
        const std::wstring state = armStateText(cpu);
        const int answer = MessageBoxW(window, state.c_str(),
            L"OldiOS — ARMv6 Step Debugger",
            MB_YESNOCANCEL | MB_ICONINFORMATION);
        if (answer != IDYES) return;
        const std::uint32_t faultPc = cpu.reg(15);
        std::uint32_t faultInstruction = 0;
        try {
            faultInstruction = (cpu.cpsr() & (1u << 5)) != 0
                ? cpu.peek16(faultPc) : cpu.peek32(faultPc);
        } catch (const std::exception&) {
            // Preserve the PC even if the next instruction is not mapped.
        }
        try {
            cpu.step();
        } catch (const std::exception& error) {
            std::wostringstream fault;
            fault << L"Execution stopped safely.\n\n"
                  << utf8ToWide(error.what())
                  << L"\n\nFaulting PC: 0x" << std::uppercase << std::hex
                  << std::setw(8) << std::setfill(L'0') << faultPc
                  << L"\nInstruction: 0x" << std::setw(8) << faultInstruction
                  << L"\n\nRegister state before instruction:";
            for (unsigned i = 0; i < 15; ++i) {
                if (i % 3 == 0) fault << L"\n";
                fault << L"R" << std::dec << i << L"=0x" << std::uppercase << std::hex
                      << std::setw(8) << std::setfill(L'0') << cpu.reg(i) << L"  ";
            }
            fault << L"\nR15/PC=0x" << std::uppercase << std::hex
                  << std::setw(8) << std::setfill(L'0') << faultPc
                  << L"  CPSR=0x" << std::setw(8) << cpu.cpsr();
            const auto& trace = cpu.recentTrace();
            fault << L"\n\nMost recent completed instructions (oldest to newest):";
            std::vector<const ArmTraceEntry*> completed;
            for (const auto& item : trace) {
                if (item.completed) completed.push_back(&item);
            }
            const std::size_t first = completed.size() > 8 ? completed.size() - 8 : 0;
            for (std::size_t i = first; i < completed.size(); ++i) {
                const auto& item = *completed[i];
                fault << L"\nPC=0x" << std::uppercase << std::hex
                      << std::setw(8) << std::setfill(L'0') << item.pc
                      << L"  OP=0x" << std::setw(8) << item.instruction
                      << L"  NEXT_PC=0x" << std::setw(8) << item.after[15]
                      << L"  R12=0x" << std::setw(8) << item.after[12]
                      << L"  LR=0x" << std::setw(8) << item.after[14];
            }
            if (faultPc >= 0x2FE00000u && faultPc < 0x2FF00000u) {
                fault << L"\n\nNote: this address is in the legacy 32-bit iOS dyld address area "
                         L"seen in some ARMv7 layouts. The current emulator maps the app image "
                         L"and stack only; it does not load dyld or the system shared cache.";
            }
            fault << L"\n\nThe iOS app has not been fully executed.";
            MessageBoxW(window, fault.str().c_str(), L"ARMv6 exception",
                        MB_OK | MB_ICONWARNING);
            return;
        }
    }
    if (cpu.halted()) {
        MessageBoxW(window, L"CPU reached its stop marker.", L"ARMv6 debugger", MB_OK);
    } else {
        MessageBoxW(window, L"Debugging paused after 64 instructions.", L"ARMv6 debugger", MB_OK);
    }
}

bool chooseIpaFile(HWND window, std::filesystem::path& selectedPath) {
    const HRESULT initResult = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const bool mustUninitialize = SUCCEEDED(initResult);
    if (FAILED(initResult) && initResult != RPC_E_CHANGED_MODE) {
        std::wostringstream message;
        message << L"Не удалось инициализировать системный диалог выбора файла.\nHRESULT: 0x"
                << std::hex << static_cast<unsigned long>(initResult);
        MessageBoxW(window, message.str().c_str(), L"OldiOS — выбор IPA",
                    MB_OK | MB_ICONERROR);
        return false;
    }

    IFileOpenDialog* dialog = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                                  IID_PPV_ARGS(&dialog));
    if (FAILED(hr)) {
        if (mustUninitialize) CoUninitialize();
        std::wostringstream message;
        message << L"Не удалось открыть системный диалог выбора файла.\nHRESULT: 0x"
                << std::hex << static_cast<unsigned long>(hr);
        MessageBoxW(window, message.str().c_str(), L"OldiOS — выбор IPA",
                    MB_OK | MB_ICONERROR);
        return false;
    }

    const COMDLG_FILTERSPEC filters[] = {
        {L"iOS application packages (*.ipa)", L"*.ipa"},
        {L"All files (*.*)", L"*.*"}
    };
    DWORD options = 0;
    hr = dialog->GetOptions(&options);
    if (SUCCEEDED(hr)) {
        hr = dialog->SetOptions(options | FOS_FORCEFILESYSTEM |
                                FOS_FILEMUSTEXIST | FOS_PATHMUSTEXIST);
    }
    if (SUCCEEDED(hr)) hr = dialog->SetFileTypes(2, filters);
    if (SUCCEEDED(hr)) hr = dialog->SetFileTypeIndex(1);
    if (SUCCEEDED(hr)) hr = dialog->SetDefaultExtension(L"ipa");
    if (SUCCEEDED(hr)) hr = dialog->SetTitle(L"Выберите IPA-файл");

    if (SUCCEEDED(hr)) hr = dialog->Show(window);

    if (hr == HRESULT_FROM_WIN32(ERROR_CANCELLED)) {
        dialog->Release();
        if (mustUninitialize) CoUninitialize();
        return false;
    }

    IShellItem* item = nullptr;
    if (SUCCEEDED(hr)) hr = dialog->GetResult(&item);
    PWSTR filePath = nullptr;
    if (SUCCEEDED(hr)) hr = item->GetDisplayName(SIGDN_FILESYSPATH, &filePath);

    if (SUCCEEDED(hr) && filePath) {
        selectedPath = std::filesystem::path(filePath);
    }

    if (filePath) CoTaskMemFree(filePath);
    if (item) item->Release();
    dialog->Release();
    if (mustUninitialize) CoUninitialize();

    if (FAILED(hr) || selectedPath.empty()) {
        std::wostringstream message;
        message << L"Не удалось получить путь выбранного файла.\nHRESULT: 0x"
                << std::hex << static_cast<unsigned long>(hr);
        MessageBoxW(window, message.str().c_str(), L"OldiOS — выбор IPA",
                    MB_OK | MB_ICONERROR);
        return false;
    }
    return true;
}

void openIpa(HWND window) {
    std::filesystem::path selectedPath;
    if (!chooseIpaFile(window, selectedPath)) return;

    IpaInfo info;
    if (loadIpaExecutable(selectedPath, info)) {
        ipaStatus = L"IPA: Mach-O загружен";
        MachOImage image;
        std::string machoError;
        if (!loadMachOImage(info.executablePath, image, machoError)) {
            ipaStatus = L"IPA: ошибка загрузки сегментов Mach-O";
            MessageBoxW(window, utf8ToWide(machoError).c_str(),
                        L"Mach-O Loader", MB_OK | MB_ICONWARNING);
            InvalidateRect(window, nullptr, TRUE);
            return;
        }
        std::wstring cpuDetails;
        try {
            if (image.bigEndian) {
                throw std::runtime_error("Big-endian ARM Mach-O is not supported by this ARM interpreter.");
            }
            Armv6 cpu;
            cpu.loadImage(image);
            std::wostringstream instructionText;
            instructionText << L"0x" << std::uppercase << std::hex
                            << std::setw(8) << std::setfill(L'0')
                            << cpu.peek32(image.entryPoint);
            cpuDetails = L"\n\nARM memory loaded.\nPC: " +
                std::to_wstring(cpu.reg(15)) +
                L"\nFirst instruction: " + instructionText.str() +
                L"\nInstruction execution: NOT STARTED";
        } catch (const std::exception& error) {
            cpuDetails = L"\n\nARM memory loading failed:\n" + utf8ToWide(error.what());
        }
        const std::wstring details =
            L"Mach-O executable extracted and validated.\n"
            L"Mapped segments: " + std::to_wstring(image.segments.size()) +
            L"\nMapped memory: " + std::to_wstring(image.totalMappedBytes) +
            L" bytes\nEntry point (decimal): " +
            (image.hasEntryPoint ? std::to_wstring(image.entryPoint) : L"not present") +
            L"\n\nApp bundle:\n" +
            utf8ToWide(info.appBundlePath) +
            L"\n\nExecutable:\n" +
            utf8ToWide(info.executableName) +
            L"\n\nCPU type: ARM (32-bit)\nCPU subtype: " +
            std::to_wstring(info.cpuSubtype & 0x00ffffffu) +
            L"\nMach-O file type: MH_EXECUTE\nSize: " +
            std::to_wstring(info.executableSize) +
            L" bytes\n\nSaved to:\n" +
            info.executablePath.wstring() +
            cpuDetails +
            L"\n\nThe app has NOT been executed.";
        MessageBoxW(window, details.c_str(), L"Mach-O Loader", MB_OK | MB_ICONINFORMATION);
        if (cpuDetails.find(L"ARM memory loaded") != std::wstring::npos) {
            Armv6 debuggerCpu;
            try {
                debuggerCpu.loadImage(image);
                showArmDebugger(window, debuggerCpu);
            } catch (const std::exception& error) {
                MessageBoxW(window, utf8ToWide(error.what()).c_str(), L"ARMv6 debugger", MB_OK | MB_ICONWARNING);
            }
        }
    } else {
        ipaStatus = L"IPA: ошибка загрузки Mach-O";
        const std::wstring details = utf8ToWide(info.error);
        MessageBoxW(window, details.c_str(), L"IPA Loader", MB_OK | MB_ICONWARNING);
    }
    InvalidateRect(window, nullptr, TRUE);
}


void installIpa(HWND window) {
    std::filesystem::path selectedPath;
    if (!chooseIpaFile(window, selectedPath)) return;
    IpaInfo info;
    std::filesystem::path installedPath;
    std::string error;
    if (!installIpaAppBundle(selectedPath, info, installedPath, error)) {
        MessageBoxW(window, utf8ToWide(error).c_str(), L"OldiOS — установка IPA",
                    MB_OK | MB_ICONWARNING);
        return;
    }
    installedBundlePath = installedPath;
    std::wstring appName = installedPath.stem().wstring();
    if (!appName.empty()) installedAppName = appName;
    ipaStatus = L"Установлено: " + installedAppName;
    const std::wstring message = L"Приложение установлено!\n\n" +
        installedAppName + L"\n\nПапка:\n" + installedPath.wstring() +
        L"\n\nИконка добавлена на домашний экран. Запуск настоящего iOS-кода пока не поддерживается.";
    MessageBoxW(window, message.c_str(), L"OldiOS — установка завершена",
                MB_OK | MB_ICONINFORMATION);
    InvalidateRect(window, nullptr, TRUE);
}

void uninstallIpa(HWND window) {
    if (installedBundlePath.empty() || !std::filesystem::exists(installedBundlePath)) {
        MessageBoxW(window, L"Нет приложения, установленного в этой сессии.",
                    L"OldiOS — удаление", MB_OK | MB_ICONINFORMATION);
        return;
    }
    const int answer = MessageBoxW(window,
        L"Удалить установленное приложение из папки Applications?",
        L"OldiOS — удаление приложения", MB_YESNO | MB_ICONQUESTION);
    if (answer != IDYES) return;
    std::error_code ec;
    std::filesystem::remove_all(installedBundlePath, ec);
    if (ec) {
        MessageBoxW(window, ec.message().c_str(), L"Не удалось удалить приложение",
                    MB_OK | MB_ICONERROR);
        return;
    }
    installedBundlePath.clear();
    installedAppName = L"I Am Rich";
    ipaStatus = L"Приложение удалено";
    InvalidateRect(window, nullptr, TRUE);
}

struct HomeApp {
    std::wstring name;
    std::filesystem::path bundlePath;
};

std::vector<HomeApp> getHomeApps() {
    std::vector<HomeApp> apps;
    std::error_code ec;
    const auto root = std::filesystem::current_path(ec) / "Applications";
    if (!ec && std::filesystem::exists(root, ec)) {
        for (std::filesystem::directory_iterator it(root, ec), finish;
             !ec && it != finish; it.increment(ec)) {
            if (!it->is_directory(ec)) continue;
            const auto filename = it->path().filename().wstring();
            if (filename.size() > 4 &&
                filename.substr(filename.size() - 4) == L".app") {
                apps.push_back({filename.substr(0, filename.size() - 4), it->path()});
            }
        }
    }
    std::sort(apps.begin(), apps.end(), [](const HomeApp& a, const HomeApp& b) {
        return a.name < b.name;
    });
    return apps;
}

void drawAppIcon(HDC dc, int x, int y, const std::wstring& name, unsigned index) {
    const COLORREF colors[] = {
        RGB(55, 120, 210), RGB(45, 160, 130), RGB(150, 95, 190),
        RGB(220, 135, 55), RGB(65, 145, 175), RGB(190, 85, 105)
    };
    roundedBox(dc, x, y, x + 48, y + 48, 11,
               colors[index % (sizeof(colors) / sizeof(colors[0]))]);
    const wchar_t letter[] = { name.empty() ? L'A' : name.front(), L'\0' };
    RECT iconRect{x, y + 1, x + 48, y + 47};
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, RGB(255, 255, 255));
    HFONT font = CreateFontW(-27, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    HFONT oldFont = static_cast<HFONT>(SelectObject(dc, font));
    DrawTextW(dc, letter, -1, &iconRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    SelectObject(dc, oldFont);
    DeleteObject(font);

    RECT labelRect{x - 10, y + 51, x + 58, y + 72};
    SetTextColor(dc, RGB(35, 40, 48));
    HFONT labelFont = CreateFontW(-10, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    oldFont = static_cast<HFONT>(SelectObject(dc, labelFont));
    DrawTextW(dc, name.c_str(), -1, &labelRect,
              DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    SelectObject(dc, oldFont);
    DeleteObject(labelFont);
}

void drawScreen(HDC dc) {
    RECT screen{0, 0, SCREEN_WIDTH, SCREEN_HEIGHT};
    HBRUSH background = CreateSolidBrush(RGB(222, 232, 246));
    FillRect(dc, &screen, background);
    DeleteObject(background);

    HBRUSH status = CreateSolidBrush(RGB(248, 249, 251));
    RECT statusRect{0, 0, SCREEN_WIDTH, 25};
    FillRect(dc, &statusRect, status);
    DeleteObject(status);
    centeredText(dc, L"9:41", 1, 22, RGB(25, 28, 32), 13, true);
    centeredText(dc, L"OldiOS", 31, 34, RGB(35, 40, 48), 21, true);

    const auto apps = getHomeApps();
    if (apps.empty()) {
        drawAppIcon(dc, 136, 105, L"Пример", 0);
        centeredText(dc, L"Здесь появятся установленные IPA", 190, 30,
                     RGB(55, 65, 80), 11);
        centeredText(dc, L"Пока показана иконка-пример", 214, 24,
                     RGB(75, 85, 100), 10);
    } else {
        constexpr int columns = 4;
        for (std::size_t i = 0; i < apps.size() && i < 16; ++i) {
            const int column = static_cast<int>(i % columns);
            const int row = static_cast<int>(i / columns);
            drawAppIcon(dc, 20 + column * 76, 88 + row * 91,
                        apps[i].name, static_cast<unsigned>(i));
        }
    }

    centeredText(dc, ipaStatus.c_str(), 392, 18, RGB(65, 75, 90), 9);
    HBRUSH dock = CreateSolidBrush(RGB(235, 239, 246));
    RECT dockRect{8, 418, SCREEN_WIDTH - 8, SCREEN_HEIGHT - 8};
    HBRUSH oldBrush = static_cast<HBRUSH>(SelectObject(dc, dock));
    HPEN pen = CreatePen(PS_SOLID, 1, RGB(215, 222, 232));
    HPEN oldPen = static_cast<HPEN>(SelectObject(dc, pen));
    RoundRect(dc, dockRect.left, dockRect.top, dockRect.right,
              dockRect.bottom, 18, 18);
    SelectObject(dc, oldBrush);
    SelectObject(dc, oldPen);
    DeleteObject(dock);
    DeleteObject(pen);
    centeredText(dc, L"Ctrl+I  Установить IPA", 425, 18,
                 RGB(55, 65, 80), 10, true);
    centeredText(dc, L"Ctrl+U  Удалить последнее", 445, 18,
                 RGB(75, 85, 100), 9);
    roundedBox(dc, 135, 462, 185, 468, 8, RGB(105, 112, 122));
}

LRESULT CALLBACK windowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_KEYDOWN:
        if ((GetKeyState(VK_CONTROL) & 0x8000) && wParam == 'O') {
            openIpa(window);
            return 0;
        }
        if ((GetKeyState(VK_CONTROL) & 0x8000) && wParam == 'I') {
            installIpa(window);
            return 0;
        }
        if ((GetKeyState(VK_CONTROL) & 0x8000) && wParam == 'U') {
            uninstallIpa(window);
            return 0;
        }
        // Forward all other key messages to the default window procedure.
        return DefWindowProcW(window, message, wParam, lParam);
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
        const auto apps = getHomeApps();
        if (apps.empty() && x >= 125 && x <= 195 && y >= 95 && y <= 185) {
            MessageBoxW(window,
                L"Это демонстрационная иконка. Установи IPA сочетанием Ctrl+I.",
                L"OldiOS — пример приложения", MB_OK | MB_ICONINFORMATION);
        } else if (!apps.empty() && y >= 80 && y < 365) {
            const int column = (x - 20) / 76;
            const int row = (y - 88) / 91;
            if (column >= 0 && column < 4 && row >= 0) {
                const std::size_t index = static_cast<std::size_t>(row * 4 + column);
                if (index < apps.size()) {
                    const std::wstring message = apps[index].name +
                        L"\n\nПриложение установлено, но выполнение iOS-кода "
                        L"в текущей версии эмулятора ещё не реализовано.";
                    MessageBoxW(window, message.c_str(), L"OldiOS",
                                MB_OK | MB_ICONINFORMATION);
                }
            }
        }
        return 0;
    }
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    default:
        return DefWindowProcW(window, message, wParam, lParam);
    }
    return DefWindowProcW(window, message, wParam, lParam);
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
