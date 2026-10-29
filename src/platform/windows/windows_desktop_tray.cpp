#include "windows_desktop_tray.h"

#include <QMetaObject>
#include <QThread>

#include <cstring>
#include <iterator>
#include <utility>

#include <windows.h>

#include <shellapi.h>

namespace xdstarter {
namespace {

constexpr wchar_t trayWindowClass[] = L"XDStarter.DesktopTray.1";
constexpr UINT trayCallbackMessage = WM_APP + 0x270;
constexpr UINT trayIconId = 1;
constexpr UINT toggleCommand = 1;
constexpr UINT quitCommand = 2;
constexpr UINT winKeyCaptureCommand = 3;
constexpr UINT startButtonCaptureCommand = 4;

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
    explicit Impl(WindowsDesktopTray* owner, NotificationOperation operation)
        : owner(owner), notificationOperation(std::move(operation)) {
        if (!notificationOperation) {
            notificationOperation = [](quint32 command, void* data) {
                return Shell_NotifyIconW(static_cast<DWORD>(command),
                                         static_cast<NOTIFYICONDATAW*>(data)) != FALSE;
            };
        }
    }

    bool notifyIcon(DWORD command, NOTIFYICONDATAW* data) {
        return notificationOperation(static_cast<quint32>(command), data);
    }

    NOTIFYICONDATAW notification() const {
        NOTIFYICONDATAW data{};
        data.cbSize = sizeof(data);
        data.hWnd = window;
        data.uID = trayIconId;
        data.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP | NIF_SHOWTIP;
        data.uCallbackMessage = trayCallbackMessage;
        data.hIcon = icon;
        QString captureState;
        if (winKeyActive) {
            captureState = QStringLiteral("Win 键唤起已启用");
        } else if (!winKeyCanEnable) {
            captureState = QStringLiteral("Win 键唤起不可用");
        } else if (winKeyWasEnabled) {
            captureState = QStringLiteral("Win 键唤起已暂停");
        } else {
            captureState = QStringLiteral("Win 键唤起未启用");
        }
        QString tooltip =
            QStringLiteral("XDStarter — %1\n%2")
                .arg(captureState, startButtonActive ? QStringLiteral("开始按钮接管已启用")
                                                     : QStringLiteral("开始按钮使用系统入口"));
        if (!winKeyStatus.isEmpty()) {
            tooltip += QLatin1Char('\n') + winKeyStatus;
        }
        // NOTIFYICONDATA's tooltip is a fixed UTF-16 buffer. Preserve the two
        // state lines even for a long diagnostic, and never split a surrogate.
        tooltip.truncate(static_cast<qsizetype>(std::size(data.szTip)) - 1);
        if (!tooltip.isEmpty() && tooltip.at(tooltip.size() - 1).isHighSurrogate()) {
            tooltip.chop(1);
        }
        tooltip.toWCharArray(data.szTip);
        data.szTip[tooltip.size()] = L'\0';
        return data;
    }

    bool registerIcon() {
        if (!window || !icon) {
            return false;
        }
        const auto generation = owner->eventSessionGeneration();
        const HWND expectedWindow = window;
        const HICON expectedIcon = icon;
        const auto current = [this, generation, expectedWindow, expectedIcon] {
            return owner->eventSessionGeneration() == generation && window == expectedWindow &&
                   icon == expectedIcon;
        };
        NOTIFYICONDATAW data = notification();
        if (!notifyIcon(NIM_ADD, &data)) {
            if (current())
                available = false;
            return false;
        }
        if (!current())
            return false;
        data.uVersion = NOTIFYICON_VERSION_4;
        if (!notifyIcon(NIM_SETVERSION, &data)) {
            if (current()) {
                notifyIcon(NIM_DELETE, &data);
                if (current())
                    available = false;
            }
            return false;
        }
        if (!current())
            return false;
        available = true;
        return true;
    }

    void queueEvent(UINT message, LPARAM parameter) {
        if (message == taskbarCreatedMessage) {
            // Explorer has discarded its icon registry. Re-registration happens
            // on the Qt event loop, outside the native callback.
            owner->queueSessionCallback([this] {
                const auto generation = owner->eventSessionGeneration();
                available = false;
                emit owner->registrationAboutToReset();
                if (!owner->isCurrentEventSession(generation))
                    return;
                const bool restored = registerIcon();
                if (!owner->isCurrentEventSession(generation))
                    return;
                emit owner->registrationChanged(
                    restored, restored
                                  ? QStringLiteral("托盘入口已恢复。")
                                  : QStringLiteral("托盘入口恢复失败；可从窗口退出 XDStarter。"));
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
    NotificationOperation notificationOperation;
    HWND window = nullptr;
    HICON icon = nullptr;
    UINT taskbarCreatedMessage = 0;
    bool available = false;
    bool menuOpen = false;
    bool winKeyActive = false;
    bool startButtonActive = false;
    bool startButtonCanEnable = false;
    bool winKeyCanEnable = false;
    bool winKeyWasEnabled = false;
    QString winKeyStatus;
    QPoint menuPosition;
};

WindowsDesktopTray::WindowsDesktopTray(QObject* parent, NotificationOperation operation)
    : DesktopTray(parent), impl_(std::make_unique<Impl>(this, std::move(operation))) {}

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
        return {false, QStringLiteral("托盘入口不可用；可从窗口退出 XDStarter。"), error};
    }
    beginEventSession();
    emit registrationChanged(true, QStringLiteral("托盘入口已就绪，开始按钮未接管。"));
    return {true, QStringLiteral("托盘入口已就绪。"), {}};
}

void WindowsDesktopTray::stop() {
    endEventSession();
    if (impl_->menuOpen) {
        EndMenu();
    }
    if (impl_->window) {
        NOTIFYICONDATAW data = impl_->notification();
        impl_->notifyIcon(NIM_DELETE, &data);
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

void WindowsDesktopTray::setWinKeyCaptureState(bool active, bool canEnable, const QString& status) {
    if (QThread::currentThread() != thread()) {
        return;
    }
    impl_->winKeyActive = active;
    impl_->winKeyCanEnable = canEnable;
    impl_->winKeyWasEnabled = impl_->winKeyWasEnabled || active;
    impl_->winKeyStatus = status.left(512).simplified();
    if (impl_->available) {
        NOTIFYICONDATAW data = impl_->notification();
        if (!impl_->notifyIcon(NIM_MODIFY, &data)) {
            impl_->available = false;
            emit registrationChanged(false,
                                     QStringLiteral("托盘状态更新失败；可从窗口退出 XDStarter。"));
        }
    }
}

void WindowsDesktopTray::showContextMenu() {
    if (!impl_->available || impl_->menuOpen || QThread::currentThread() != thread()) {
        return;
    }
    HMENU menu = CreatePopupMenu();
    if (!menu) {
        return;
    }
    const wchar_t* captureLabel =
        impl_->winKeyActive ? L"暂停 Win 键唤起"
                           : impl_->winKeyWasEnabled ? L"恢复 Win 键唤起" : L"启用 Win 键唤起";
    const UINT captureFlags =
        MF_STRING | (impl_->winKeyActive || impl_->winKeyCanEnable ? MF_ENABLED : MF_GRAYED);
    if (!AppendMenuW(menu, MF_STRING, toggleCommand, L"显示/隐藏 XDStarter") ||
        !AppendMenuW(
            menu,
            MF_STRING |
                (impl_->startButtonActive || impl_->startButtonCanEnable ? MF_ENABLED : MF_GRAYED),
            startButtonCaptureCommand,
            impl_->startButtonActive ? L"暂停开始按钮接管" : L"启用开始按钮接管") ||
        !AppendMenuW(menu, captureFlags, winKeyCaptureCommand, captureLabel) ||
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
    } else if (command == startButtonCaptureCommand &&
               (impl_->startButtonActive || impl_->startButtonCanEnable)) {
        emit startButtonCaptureToggleRequested();
    } else if (command == winKeyCaptureCommand && (impl_->winKeyActive || impl_->winKeyCanEnable)) {
        emit winKeyCaptureToggleRequested();
    } else if (command == quitCommand) {
        emit quitRequested();
    }
}

void WindowsDesktopTray::setStartButtonCaptureState(bool active, bool canEnable, const QString&) {
    impl_->startButtonActive = active;
    impl_->startButtonCanEnable = canEnable;
    if (impl_->available) {
        auto notification = impl_->notification();
        if (!impl_->notifyIcon(NIM_MODIFY, &notification)) {
            impl_->available = false;
            emit registrationChanged(false, QStringLiteral("无法更新托盘状态。"));
        }
    }
}

} // namespace xdstarter
