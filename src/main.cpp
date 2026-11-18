#include <Windows.h>
#include "Core/Logger.h"
#include "UI/MainWindow.h"

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, PWSTR, int cmd) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    if (FAILED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED))) return 1;

    nf::Logger::Init();
    nf::Logger::Info("NeoFrame starting");

    nf::MainWindow window;
    if (!window.Create(inst)) {
        MessageBoxW(nullptr, L"Не удалось создать окно NeoFrame. Подробности в neoframe.log.",
                    L"NeoFrame", MB_ICONERROR);
        CoUninitialize();
        return 1;
    }
    window.Show(cmd);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    CoUninitialize();
    return static_cast<int>(msg.wParam);
}
