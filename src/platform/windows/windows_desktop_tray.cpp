#include "windows_desktop_tray.h"

#include <QMetaObject>
#include <QThread>

#include <algorithm>
#include <cstring>
#include <iterator>

#include <windows.h>

#include <shellapi.h>

namespace xdstarter {
namespace {

constexpr wchar_t trayWindowClass[] = L"XDStarter.DesktopTray.1";
constexpr UINT trayCallbackMessage = WM_APP + 0x270;
constexpr UINT trayIconId = 1;
constexpr UINT toggleCommand = 1;
constexpr UINT quitCommand = 2;

HICON createNativeIcon(const QImage& source) {
    // The application supplies its bundled image, not a machine-local icon path.
    // DIB pixels carry premultiplied BGRA and are copied into an owned HICON.
    const QImage image = source.scaled(32, 32, Qt::KeepAspectRatio, Qt::SmoothTransformation)
                             .convertToFormat(QImage::Format_ARGB32_Premultiplied);
    if (image.isNull()) {
        return nullptr;
    }
    BITMAPV5HEADER header{};
    header.bV5Size = sizeof(header);
    header.bV5Width = image.width();
    header.bV5Height = -image.height();
    header.bV5Planes = 1;
    header.bV5BitCount = 32;
    header.bV5Compression = BI_BITFIELDS;
    header.bV5RedMask = 0x00ff0000;
    header.bV5GreenMask = 0x0000ff00;
    header.bV5BlueMask = 0x000000ff;
    header.bV5AlphaMask = 0xff000000;
    void* pixels = nullptr;
    HBITMAP color = CreateDIBSection(nullptr, reinterpret_cast<BITMAPINFO*>(&header),
                                     DIB_RGB_COLORS, &pixels, nullptr, 0);
    if (!color || !pixels) {
        if (color) {
            DeleteObject(color);
        }
        return nullptr;
    }
    for (int row = 0; row < image.height(); ++row) {
        std::memcpy(static_cast<unsigned char*>(pixels) + row * image.width() * 4,
                    image.constScanLine(row), static_cast<size_t>(image.width()) * 4);
    }
    HBITMAP mask = CreateBitmap(image.width(), image.height(), 1, 1, nullptr);
    ICONINFO info{};
    info.fIcon = TRUE;
    info.hbmColor = color;
    info.hbmMask = mask;
    HICON icon = mask ? CreateIconIndirect(&info) : nullptr;
    if (mask) {
        DeleteObject(mask);
    }
    DeleteObject(color);
    return icon;
}

QString trayError(const QString& prefix) {
    return prefix + QStringLiteral("-%1").arg(GetLastError());
}

} // namespace

class WindowsDesktopTray::Impl {
  public:
    explicit Impl(WindowsDesktopTray* owner) : owner(owner) {}

    NOTIFYICONDATAW notification() const {
        NOTIFYICONDATAW data{};
        data.cbSize = sizeof(data);
        data.hWnd = window;
        data.uID = trayIconId;
        data.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP | NIF_SHOWTIP;
        data.uCallbackMessage = trayCallbackMessage;
        data.hIcon = icon;
        constexpr wchar_t tooltip[] = L"XDStarter — 未接管系统开始菜单";
        std::copy(std::begin(tooltip), std::end(tooltip), std::begin(data.szTip));
        return data;
    }

    bool registerIcon() {
        if (!window || !icon) {
            return false;
        }
        NOTIFYICONDATAW data = notification();
        if (!Shell_NotifyIconW(NIM_ADD, &data)) {
            available = false;
            return false;
        }
        data.uVersion = NOTIFYICON_VERSION_4;
        if (!Shell_NotifyIconW(NIM_SETVERSION, &data)) {
            Shell_NotifyIconW(NIM_DELETE, &data);
            available = false;
            return false;
        }
        available = true;
        return true;
    }

    void queueEvent(UINT message, LPARAM parameter) {
        if (message == taskbarCreatedMessage) {
            // Explorer has discarded its icon registry. Re-registration happens
            // on the Qt event loop, outside the native callback.
            owner->queueSessionCallback([this] {
                const bool restored = registerIcon();
                emit owner->registrationChanged(
                    restored, restored
                                  ? QStringLiteral("托盘入口已恢复。")
                                  : QStringLiteral("托盘入口恢复失败；系统开始菜单保持原有行为。"));
            });
            return;
        }
        if (message != trayCallbackMessage || HIWORD(parameter) != trayIconId) {
            return;
        }
        const UINT event = LOWORD(parameter);
        if (event == NIN_SELECT || event == NIN_KEYSELECT) {
            owner->queueSessionCallback([this] { emit owner->toggleRequested(); });
        } else if (event == WM_CONTEXTMENU) {
            POINT position{};
            GetPhysicalCursorPos(&position);
            menuPosition = {position.x, position.y};
            const QPoint positionAtEvent = menuPosition;
            owner->queueSessionCallback(
                [this, positionAtEvent] { emit owner->menuRequested(positionAtEvent); });
        }
    }

    static LRESULT CALLBACK windowProcedure(HWND window, UINT message, WPARAM word, LPARAM value) {
        if (message == WM_NCCREATE) {
            const auto* creation = reinterpret_cast<CREATESTRUCTW*>(value);
            SetWindowLongPtrW(window, GWLP_USERDATA,
                              reinterpret_cast<LONG_PTR>(creation->lpCreateParams));
        }
        auto* state = reinterpret_cast<Impl*>(GetWindowLongPtrW(window, GWLP_USERDATA));
        if (state && (message == trayCallbackMessage || message == state->taskbarCreatedMessage)) {
            // Never allow an allocation exception to cross a Win32 callback.
            try {
                state->queueEvent(message, value);
            } catch (...) {
            }
            return 0;
        }
        if (message == WM_NCDESTROY) {
            SetWindowLongPtrW(window, GWLP_USERDATA, 0);
        }
        return DefWindowProcW(window, message, word, value);
    }

    WindowsDesktopTray* owner;
    HWND window = nullptr;
    HICON icon = nullptr;
    UINT taskbarCreatedMessage = 0;
    bool available = false;
    bool menuOpen = false;
    QPoint menuPosition;
};

WindowsDesktopTray::WindowsDesktopTray(QObject* parent)
    : DesktopTray(parent), impl_(std::make_unique<Impl>(this)) {}

WindowsDesktopTray::~WindowsDesktopTray() { stop(); }

DesktopActionResult WindowsDesktopTray::start(const QImage& image) {
    if (QThread::currentThread() != thread()) {
        return {false, QStringLiteral("托盘入口必须在界面线程初始化。"),
                QStringLiteral("tray-wrong-thread")};
    }
    if (image.isNull() || image.width() > 4096 || image.height() > 4096) {
        return {false, QStringLiteral("托盘图标素材无效。"), QStringLiteral("invalid-tray-icon")};
    }
    stop();
    impl_->icon = createNativeIcon(image);
    if (!impl_->icon) {
        return {false, QStringLiteral("无法创建托盘图标。"),
                trayError(QStringLiteral("tray-icon"))};
    }
    const HINSTANCE instance = GetModuleHandleW(nullptr);
    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.hInstance = instance;
    windowClass.lpfnWndProc = Impl::windowProcedure;
    windowClass.lpszClassName = trayWindowClass;
    if (!RegisterClassExW(&windowClass) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        const QString error = trayError(QStringLiteral("tray-class"));
        stop();
        return {false, QStringLiteral("无法创建托盘恢复入口。"), error};
    }
    impl_->taskbarCreatedMessage = RegisterWindowMessageW(L"TaskbarCreated");
    // A hidden top-level tool window receives TaskbarCreated broadcasts. A
    // message-only HWND would miss them. This window never becomes visible.
    impl_->window = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, trayWindowClass,
                                    L"XDStarter tray host", WS_POPUP, 0, 0, 0, 0, nullptr, nullptr,
                                    instance, impl_.get());
    if (!impl_->window || !impl_->taskbarCreatedMessage || !impl_->registerIcon()) {
        const QString error = trayError(QStringLiteral("tray-register"));
        stop();
        return {false, QStringLiteral("托盘入口不可用；系统开始菜单保持原有行为。"), error};
    }
    beginEventSession();
    emit registrationChanged(true, QStringLiteral("托盘入口已就绪，系统开始菜单保持原有行为。"));
    return {true, QStringLiteral("托盘入口已就绪。"), {}};
}

void WindowsDesktopTray::stop() {
    endEventSession();
    if (impl_->menuOpen) {
        EndMenu();
    }
    if (impl_->window) {
        NOTIFYICONDATAW data = impl_->notification();
        Shell_NotifyIconW(NIM_DELETE, &data);
        DestroyWindow(impl_->window);
        impl_->window = nullptr;
    }
    if (impl_->icon) {
        DestroyIcon(impl_->icon);
        impl_->icon = nullptr;
    }
    impl_->available = false;
}

bool WindowsDesktopTray::isAvailable() const { return impl_->available; }

void WindowsDesktopTray::showContextMenu() {
    if (!impl_->available || impl_->menuOpen || QThread::currentThread() != thread()) {
        return;
    }
    HMENU menu = CreatePopupMenu();
    if (!menu) {
        return;
    }
    if (!AppendMenuW(menu, MF_STRING, toggleCommand, L"显示/隐藏 XDStarter") ||
        !AppendMenuW(menu, MF_STRING, quitCommand, L"退出 XDStarter")) {
        DestroyMenu(menu);
        return;
    }
    impl_->menuOpen = true;
    const quint64 generation = eventSessionGeneration();
    const HWND menuOwner = impl_->window;
    SetForegroundWindow(impl_->window);
    const UINT command =
        TrackPopupMenuEx(menu, TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON,
                         impl_->menuPosition.x(), impl_->menuPosition.y(), impl_->window, nullptr);
    if (IsWindow(menuOwner)) {
        PostMessageW(menuOwner, WM_NULL, 0, 0);
    }
    DestroyMenu(menu);
    impl_->menuOpen = false;
    if (!isCurrentEventSession(generation) || !impl_->available) {
        return;
    }
    if (command == toggleCommand) {
        emit toggleRequested();
    } else if (command == quitCommand) {
        emit quitRequested();
    }
}

} // namespace xdstarter
