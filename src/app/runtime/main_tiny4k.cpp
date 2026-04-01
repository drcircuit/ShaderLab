#include <windows.h>

extern "C" int WINAPI WinMain(HINSTANCE instance, HINSTANCE, LPSTR, int) {
    WNDCLASSA windowClass = {};
    windowClass.lpfnWndProc = DefWindowProcA;
    windowClass.hInstance = instance;
    windowClass.lpszClassName = "SLTiny4K";
    windowClass.hbrBackground = static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));

    if (!RegisterClassA(&windowClass)) {
        return 1;
    }

    HWND windowHandle = CreateWindowExA(
        WS_EX_TOPMOST,
        "SLTiny4K",
        "",
        WS_POPUP,
        0,
        0,
        GetSystemMetrics(SM_CXSCREEN),
        GetSystemMetrics(SM_CYSCREEN),
        nullptr,
        nullptr,
        instance,
        nullptr);

    if (!windowHandle) {
        return 2;
    }

    ShowWindow(windowHandle, SW_SHOW);
    UpdateWindow(windowHandle);

    MSG message;
    while (GetMessageA(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageA(&message);
    }

    return 0;
}
