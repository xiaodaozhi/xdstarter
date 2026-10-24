// src/experimental/explorer_route/route_fixture.cpp

#include "route_protocol.h"

using namespace xdstarter::route_probe;
namespace {
HWND child = nullptr;
LRESULT CALLBACK procedure(HWND window, UINT message, WPARAM first, LPARAM second) {
    if (message == fixtureTest) {
        PostMessageW(child, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(5, 6));
        PostMessageW(child, WM_LBUTTONUP, 0, MAKELPARAM(5, 6));
        MSG peek{};
        const bool unchanged =
            PeekMessageW(&peek, child, WM_LBUTTONDOWN, WM_LBUTTONDOWN, PM_NOREMOVE) &&
            peek.wParam == MK_LBUTTON && peek.lParam == MAKELPARAM(5, 6);
        SetPropW(window, L"RoutePeekPreserved", reinterpret_cast<HANDLE>(UINT_PTR(unchanged)));
        return 0;
    }
    if (message == WM_LBUTTONDOWN || message == WM_LBUTTONUP) {
        const auto* property = message == WM_LBUTTONDOWN ? downProperty : upProperty;
        const auto count = reinterpret_cast<UINT_PTR>(GetPropW(window, property));
        SetPropW(window, property, reinterpret_cast<HANDLE>(count + 1));
        return 0;
    }
    if (message == WM_CLOSE) {
        DestroyWindow(window);
        return 0;
    }
    if (message == WM_DESTROY && window != child)
        PostQuitMessage(0);
    return DefWindowProcW(window, message, first, second);
}
} // namespace

int wmain(int argc, wchar_t** argv) {
    if (argc != 2 || wcscmp(argv[1], L"--controlled") != 0)
        return 64;
    WNDCLASSW windowClass{};
    windowClass.lpfnWndProc = procedure;
    windowClass.hInstance = GetModuleHandleW(nullptr);
    windowClass.lpszClassName = fixtureClass;
    if (!RegisterClassW(&windowClass))
        return 1;
    const auto root =
        CreateWindowExW(WS_EX_TOOLWINDOW, fixtureClass, L"route fixture", WS_OVERLAPPED, 0, 0, 200,
                        100, nullptr, nullptr, windowClass.hInstance, nullptr);
    child = CreateWindowExW(0, fixtureClass, L"child", WS_CHILD, 0, 0, 50, 30, root, nullptr,
                            windowClass.hInstance, nullptr);
    if (!root || !child)
        return 1;
    SetPropW(root, L"RouteChild", child);
    windowClass.lpszClassName = L"XDStarter.RouteFixture52.Other";
    if (!RegisterClassW(&windowClass))
        return 1;
    const auto other =
        CreateWindowExW(WS_EX_TOOLWINDOW, windowClass.lpszClassName, L"other", WS_OVERLAPPED, 0, 0,
                        100, 100, nullptr, nullptr, windowClass.hInstance, nullptr);
    if (!other)
        return 1;
    SetPropW(root, L"RouteOther", other);
    SetTimer(root, 1, 25, nullptr);
    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    return 0;
}
