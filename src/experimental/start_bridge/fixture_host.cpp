// src/experimental/start_bridge/fixture_host.cpp

#include "experimental/start_bridge/win_support.h"

#include <windowsx.h>

#include <iostream>
#include <string_view>

namespace {
using namespace xdstarter::bridge_poc;
UINT_PTR generation = 0;
bool recreating = false;
bool visible = false;
HWND currentWindow = nullptr;

void count(HWND window, const wchar_t* property) {
    SetPropW(window, property,
             reinterpret_cast<HANDLE>(reinterpret_cast<UINT_PTR>(GetPropW(window, property)) + 1));
}
void postMouse(HWND window, UINT message, WPARAM flags = 0, int x = 30, int y = 30) {
    PostMessageW(window, message, flags, MAKELPARAM(x, y));
}
void postPointer(HWND window, UINT message, bool contact) {
    POINT point{30, 30};
    ClientToScreen(window, &point);
    const UINT flags =
        POINTER_FLAG_PRIMARY | (contact ? POINTER_FLAG_FIRSTBUTTON | POINTER_FLAG_INCONTACT : 0);
    SetLastError(ERROR_SUCCESS);
    const BOOL posted =
        PostMessageW(window, message, MAKEWPARAM(7, flags), MAKELPARAM(point.x, point.y));
    const DWORD error = posted ? ERROR_SUCCESS : GetLastError();
    count(window, pointerPostsProperty);
    if (posted)
        count(window, pointerPostsSucceededProperty);
    SetPropW(window, pointerPostErrorProperty,
             reinterpret_cast<HANDLE>(static_cast<UINT_PTR>(error)));
}
void postEmulatedPointer(HWND window, UINT message, bool contact, UINT pointer = 7,
                         UINT extraFlags = 0) {
    POINT point{30, 30};
    ClientToScreen(window, &point);
    const UINT flags = POINTER_FLAG_PRIMARY | extraFlags |
                       (contact ? POINTER_FLAG_FIRSTBUTTON | POINTER_FLAG_INCONTACT : 0);
    PostMessageW(window, message, MAKEWPARAM(pointer, flags), MAKELPARAM(point.x, point.y));
}
HWND otherWindow(HWND window) {
    auto other = reinterpret_cast<HWND>(GetPropW(window, otherWindowProperty));
    if (!IsWindow(other)) {
        other = CreateWindowExW(WS_EX_TOOLWINDOW, L"XDStarter.ControlledOther52",
                                L"Other controlled window", WS_POPUP, 0, 0, 100, 100, nullptr,
                                nullptr, GetModuleHandleW(nullptr), nullptr);
        SetPropW(window, otherWindowProperty, other);
    }
    return other;
}
HWND createFixtureWindow();
LRESULT CALLBACK procedure(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    switch (message) {
    case WM_CREATE:
        SetPropW(window, generationProperty, reinterpret_cast<HANDLE>(++generation));
        SetTimer(window, 1, 25, nullptr); // Keep a real GetMessage loop awake.
        return 0;
    case WM_TIMER:
        return 0;
    case WM_LBUTTONDOWN:
    case WM_LBUTTONDBLCLK:
        count(window, nativeMouseDownProperty);
        SetPropW(window, lastExtraInformationProperty,
                 reinterpret_cast<HANDLE>(GetMessageExtraInfo()));
        if ((static_cast<ULONG_PTR>(GetMessageExtraInfo()) & 0xffffff00u) == 0xff515700u)
            count(window, nativePromotedProperty);
        return 0;
    case WM_LBUTTONUP:
        count(window, nativeMouseUpProperty);
        return 0;
    case WM_POINTERDOWN:
        count(window, nativePointerDownProperty);
        return 0;
    case WM_POINTERUP:
        count(window, nativePointerUpProperty);
        return 0;
    case emulatedPointerDownMessage:
        count(window, nativeEmulatedPointerDownProperty);
        return 0;
    case emulatedPointerUpMessage:
        count(window, nativeEmulatedPointerUpProperty);
        return 0;
    case emulatedPointerCaptureLostMessage:
        count(window, nativeEmulatedCaptureLostProperty);
        return 0;
    case emulatedCompatibilityDownMessage:
        count(window, nativeEmulatedCompatibilityDownProperty);
        return 0;
    case emulatedCompatibilityUpMessage:
        count(window, nativeEmulatedCompatibilityUpProperty);
        return 0;
    case WM_RBUTTONDOWN:
        count(window, nativeRightProperty);
        return 0;
    case fixtureCommandMessage:
        switch (static_cast<FixtureCommand>(wparam)) {
        case FixtureCommand::MousePair:
            postMouse(window, WM_LBUTTONDOWN, MK_LBUTTON);
            postMouse(window, WM_LBUTTONUP);
            break;
        case FixtureCommand::PeekMousePair: {
            postMouse(window, WM_LBUTTONDOWN, MK_LBUTTON);
            MSG peek{};
            const bool found =
                PeekMessageW(&peek, window, WM_LBUTTONDOWN, WM_LBUTTONDOWN, PM_NOREMOVE) != FALSE;
            SetPropW(window, peekUnmodifiedProperty,
                     reinterpret_cast<HANDLE>(
                         UINT_PTR(found && peek.message == WM_LBUTTONDOWN ? 1 : 2)));
            postMouse(window, WM_LBUTTONUP);
            break;
        }
        case FixtureCommand::MouseDown:
            postMouse(window, WM_LBUTTONDOWN, MK_LBUTTON);
            break;
        case FixtureCommand::MouseUp:
            postMouse(window, WM_LBUTTONUP);
            break;
        case FixtureCommand::ModifiedMousePair:
            postMouse(window, WM_LBUTTONDOWN, MK_LBUTTON | MK_SHIFT);
            postMouse(window, WM_LBUTTONUP, MK_SHIFT);
            break;
        case FixtureCommand::RightPair:
            postMouse(window, WM_RBUTTONDOWN, MK_RBUTTON);
            postMouse(window, WM_RBUTTONUP);
            break;
        case FixtureCommand::MouseDragPair:
            postMouse(window, WM_LBUTTONDOWN, MK_LBUTTON);
            postMouse(window, WM_MOUSEMOVE, MK_LBUTTON, 70, 30);
            postMouse(window, WM_LBUTTONUP, 0, 70, 30);
            break;
        case FixtureCommand::EmulatedPointerPair:
            postEmulatedPointer(window, emulatedPointerDownMessage, true);
            postEmulatedPointer(window, emulatedPointerUpMessage, false);
            break;
        case FixtureCommand::EmulatedPointerDown:
            postEmulatedPointer(window, emulatedPointerDownMessage, true);
            break;
        case FixtureCommand::EmulatedPointerUp:
            postEmulatedPointer(window, emulatedPointerUpMessage, false);
            break;
        case FixtureCommand::PointerTransportProbe:
            postPointer(window, WM_POINTERDOWN, true);
            postPointer(window, WM_POINTERUP, false);
            break;
        case FixtureCommand::EmulatedPointerCaptureLost:
            PostMessageW(window, emulatedPointerCaptureLostMessage, MAKEWPARAM(7, 0), 0);
            break;
        case FixtureCommand::WrongIdEmulatedPointerCaptureLost:
            PostMessageW(window, emulatedPointerCaptureLostMessage, MAKEWPARAM(8, 0), 0);
            break;
        case FixtureCommand::OtherEmulatedPointerCaptureLost:
            PostMessageW(otherWindow(window), emulatedPointerCaptureLostMessage, MAKEWPARAM(7, 0),
                         0);
            break;
        case FixtureCommand::OtherMousePair:
            postMouse(otherWindow(window), WM_LBUTTONDOWN, MK_LBUTTON);
            postMouse(otherWindow(window), WM_LBUTTONUP);
            break;
        case FixtureCommand::OtherEmulatedPointerUp:
            postEmulatedPointer(otherWindow(window), emulatedPointerUpMessage, false);
            break;
        case FixtureCommand::EmulatedPointerCanceled:
            postEmulatedPointer(window, emulatedPointerUpdateMessage, true, 7,
                                POINTER_FLAG_CANCELED);
            break;
        case FixtureCommand::KeyboardCancel:
            PostMessageW(window, WM_KEYDOWN, VK_ESCAPE, 0);
            PostMessageW(window, WM_KEYUP, VK_ESCAPE, 0);
            break;
        case FixtureCommand::OtherEmulatedCompatibilityPair: {
            const auto other = otherWindow(window);
            postMouse(other, emulatedCompatibilityDownMessage, MK_LBUTTON);
            postMouse(other, emulatedCompatibilityUpMessage);
            break;
        }
        case FixtureCommand::EmulatedCompatibilityDown:
            postMouse(window, emulatedCompatibilityDownMessage, MK_LBUTTON);
            break;
        case FixtureCommand::EmulatedCompatibilityUp:
            postMouse(window, emulatedCompatibilityUpMessage);
            break;
        case FixtureCommand::DuplicateWindow:
            SetPropW(window, duplicateWindowProperty, createFixtureWindow());
            break;
        case FixtureCommand::CloseDuplicateWindow:
            DestroyWindow(reinterpret_cast<HWND>(GetPropW(window, duplicateWindowProperty)));
            RemovePropW(window, duplicateWindowProperty);
            break;
        case FixtureCommand::Recreate:
            recreating = true;
            DestroyWindow(window);
            currentWindow = createFixtureWindow();
            recreating = false;
            break;
        case FixtureCommand::Exit:
            DestroyWindow(window);
            break;
        }
        return 0;
    case WM_PAINT: {
        PAINTSTRUCT paint{};
        const auto dc = BeginPaint(window, &paint);
        RECT client{};
        GetClientRect(window, &client);
        DrawTextW(dc, L"受控 InputSite 替身\n只验证队列桥接与 IPC\n未接入 Explorer", -1, &client,
                  DT_CENTER | DT_VCENTER | DT_WORDBREAK);
        EndPaint(window, &paint);
        return 0;
    }
    case WM_DESTROY:
        KillTimer(window, 1);
        RemovePropW(window, generationProperty);
        if (!recreating && window == currentWindow)
            PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(window, message, wparam, lparam);
}
HWND createFixtureWindow() {
    const auto window =
        CreateWindowExW(WS_EX_TOOLWINDOW, fixtureClass, L"XDStarter bridge PoC controlled host",
                        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 360, 180, nullptr,
                        nullptr, GetModuleHandleW(nullptr), nullptr);
    if (window && visible)
        ShowWindow(window, SW_SHOWNOACTIVATE);
    return window;
}
void acknowledgeBarrier(WPARAM first, LPARAM second) {
    const auto name = mappingName(GetCurrentProcessId(), processCreationTime(GetCurrentProcess()));
    Handle mapping(OpenFileMappingW(FILE_MAP_READ, FALSE, name.c_str()));
    const auto* shared = mapping ? static_cast<const SharedSession*>(MapViewOfFile(
                                       mapping.get(), FILE_MAP_READ, 0, 0, sizeof(SharedSession)))
                                 : nullptr;
    if (!shared)
        return;
    const auto& config = shared->configuration;
    if (config.magic == protocolMagic && config.version == protocolVersion &&
        static_cast<WPARAM>(config.nonce[0]) == first &&
        static_cast<LPARAM>(config.nonce[1]) == second && config.barrierEvent[159] == 0 &&
        config.fixture.processId == GetCurrentProcessId() &&
        config.fixture.threadId == GetCurrentThreadId()) {
        Handle event(OpenEventW(EVENT_MODIFY_STATE, FALSE, config.barrierEvent));
        if (event)
            SetEvent(event.get());
    }
    UnmapViewOfFile(shared);
}
} // namespace

int wmain(int argc, wchar_t** argv) {
    bool controlled = false;
    for (int index = 1; index < argc; ++index) {
        controlled |= std::wstring_view(argv[index]) == L"--controlled";
        visible |= std::wstring_view(argv[index]) == L"--visible";
    }
    if (!controlled) {
        std::wcerr << L"This fixture requires --controlled. It never loads or hooks another "
                      L"application.\n";
        return 64;
    }
    WNDCLASSW windowClass{};
    windowClass.lpfnWndProc = procedure;
    windowClass.hInstance = GetModuleHandleW(nullptr);
    windowClass.lpszClassName = fixtureClass;
    windowClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    if (!RegisterClassW(&windowClass))
        return 1;
    windowClass.lpszClassName = L"XDStarter.ControlledOther52";
    if (!RegisterClassW(&windowClass))
        return 1;
    currentWindow = createFixtureWindow();
    if (!currentWindow)
        return 1;
    std::wcout << L"CONTROLLED_FIXTURE_READY pid=" << GetCurrentProcessId() << L" tid="
               << GetCurrentThreadId() << L'\n';
    std::wcout.flush();
    MSG message{};
    int received = 0;
    for (;;) {
        received = GetMessageW(&message, nullptr, 0, 0);
        if (received <= 0)
            break;
        if (!message.hwnd && message.message == fixtureBarrierMessage) {
            acknowledgeBarrier(message.wParam, message.lParam);
            continue;
        }
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    return received < 0 ? 1 : 0;
}
