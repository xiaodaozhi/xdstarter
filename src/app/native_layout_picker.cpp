#include "native_layout_picker.h"

#include "core/picker_request_state.h"

#include <QCoreApplication>
#include <QDebug>
#include <QGuiApplication>
#include <QPointer>
#include <QTimer>

#ifdef Q_OS_WIN
#include "platform/windows/native_picker_focus.h"
#include "platform/windows/native_picker_protocol.h"

#include <QWinEventNotifier>

#include <algorithm>
#include <cstdint>
#include <cwchar>
#include <new>
#include <string>
#include <vector>
#endif

namespace xdstarter {

class NativeLayoutPickerPrivate {
  public:
    explicit NativeLayoutPickerPrivate(NativeLayoutPicker* object) : q(object) {}
    ~NativeLayoutPickerPrivate() { shutdown(); }

    NativeLayoutPicker* q;
    QPointer<QWindow> parentWindow;
    QString title;
    QString jsonFilter;
    QString allFilter;
    QUrl selectedFile;
    PickerRequestState state;
    bool visible = false;

    void setVisible(bool value) {
        if (visible == value) return;
        visible = value;
        emit q->visibleChanged();
    }

#ifdef Q_OS_WIN
    struct Request {
        PickerRequestState::Id id = 0;
        QPointer<QWindow> owner;
        HWND hwnd = nullptr;
        bool ownerWasEnabled = false;
        HANDLE job = nullptr;
        HANDLE process = nullptr;
        HANDLE mapping = nullptr;
        native_picker::Shared* shared = nullptr;
        QWinEventNotifier* notifier = nullptr;
        bool registered = false;
        ~Request() {
            if (registered) native_picker::unregisterForegroundOwner(hwnd, process);
            if (notifier) delete notifier;
            // Closing the private job kills only this request's process tree.
            // It does not wait for COM or a native dialog on the GUI thread.
            if (job) CloseHandle(job);
            if (process) CloseHandle(process);
            if (shared) UnmapViewOfFile(shared);
            if (mapping) CloseHandle(mapping);
        }
    };
    std::unique_ptr<Request> active;

    static bool copyText(wchar_t* target, std::size_t capacity, const QString& source) {
        const auto value = source.toStdWString();
        if (value.empty() || value.size() >= capacity) return false;
        std::copy(value.begin(), value.end(), target);
        target[value.size()] = L'\0';
        return true;
    }

    bool start(PickerRequestState::Id id) {
        auto request = std::make_unique<Request>();
        request->id = id;
        request->owner = parentWindow;
        if (!request->owner || !request->owner->isVisible()) return false;
        request->hwnd = reinterpret_cast<HWND>(request->owner->winId());
        DWORD ownerPid = 0;
        if (!request->hwnd || !IsWindow(request->hwnd) ||
            GetWindowThreadProcessId(request->hwnd, &ownerPid) != GetCurrentThreadId() ||
            ownerPid != GetCurrentProcessId() || GetAncestor(request->hwnd, GA_ROOT) != request->hwnd)
            return false;
        request->ownerWasEnabled = IsWindowEnabled(request->hwnd) != FALSE;
        if (!request->ownerWasEnabled) return false;

        SECURITY_ATTRIBUTES inherited{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
        request->mapping = CreateFileMappingW(INVALID_HANDLE_VALUE, &inherited, PAGE_READWRITE,
                                               0, sizeof(native_picker::Shared), nullptr);
        if (!request->mapping) return false;
        request->shared = static_cast<native_picker::Shared*>(
            MapViewOfFile(request->mapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(native_picker::Shared)));
        if (!request->shared) return false;
        new (request->shared) native_picker::Shared{};
        request->shared->owner = reinterpret_cast<std::uintptr_t>(request->hwnd);
        if (!copyText(request->shared->title, native_picker::textCapacity, title) ||
            !copyText(request->shared->jsonFilter, native_picker::textCapacity, jsonFilter) ||
            !copyText(request->shared->allFilter, native_picker::textCapacity, allFilter))
            return false;

        HANDLE parentProcess = nullptr;
        if (!DuplicateHandle(GetCurrentProcess(), GetCurrentProcess(), GetCurrentProcess(), &parentProcess,
                             SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, TRUE, 0))
            return false;
        request->shared->parentProcess = reinterpret_cast<std::uintptr_t>(parentProcess);
        request->job = CreateJobObjectW(nullptr, nullptr);
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
        limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        if (!request->job || !SetInformationJobObject(request->job, JobObjectExtendedLimitInformation,
                                                        &limits, sizeof(limits))) {
            CloseHandle(parentProcess);
            return false;
        }

        SIZE_T bytes = 0;
        InitializeProcThreadAttributeList(nullptr, 2, 0, &bytes);
        if (!bytes) { CloseHandle(parentProcess); return false; }
        std::vector<unsigned char> storage(bytes);
        auto* attributes = reinterpret_cast<PPROC_THREAD_ATTRIBUTE_LIST>(storage.data());
        if (!InitializeProcThreadAttributeList(attributes, 2, 0, &bytes)) {
            CloseHandle(parentProcess);
            return false;
        }
        HANDLE handles[] = {request->mapping, parentProcess};
        HANDLE jobs[] = {request->job};
        const bool attributesReady =
            UpdateProcThreadAttribute(attributes, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, handles,
                                      sizeof(handles), nullptr, nullptr) &&
            UpdateProcThreadAttribute(attributes, 0, PROC_THREAD_ATTRIBUTE_JOB_LIST, jobs,
                                      sizeof(jobs), nullptr, nullptr);
        const std::wstring executable = (QCoreApplication::applicationDirPath() +
                                          QStringLiteral("/xdstarter.exe")).toStdWString();
        const std::wstring command = L"\"" + executable + L"\" --internal-layout-picker " +
                                     std::to_wstring(reinterpret_cast<std::uintptr_t>(request->mapping));
        std::vector<wchar_t> mutableCommand(command.begin(), command.end());
        mutableCommand.push_back(L'\0');
        STARTUPINFOEXW startup{};
        startup.StartupInfo.cb = sizeof(startup);
        startup.lpAttributeList = attributes;
        PROCESS_INFORMATION created{};
        const BOOL createdOk = attributesReady && CreateProcessW(
            executable.c_str(), mutableCommand.data(), nullptr, nullptr, TRUE,
            CREATE_SUSPENDED | CREATE_NO_WINDOW | EXTENDED_STARTUPINFO_PRESENT, nullptr, nullptr,
            &startup.StartupInfo, &created);
        DeleteProcThreadAttributeList(attributes);
        CloseHandle(parentProcess);
        if (!createdOk) return false;

        request->process = created.hProcess;
        request->notifier = new QWinEventNotifier(request->process, q);
        QObject::connect(request->notifier, &QWinEventNotifier::activated, q, [this] { finished(); });
        native_picker::registerForegroundOwner(request->hwnd, request->process);
        request->registered = true;
        if (ResumeThread(created.hThread) == DWORD(-1)) {
            CloseHandle(created.hThread);
            TerminateJobObject(request->job, 0x88);
            return false;
        }
        CloseHandle(created.hThread);
        active = std::move(request);
        return true;
    }

    void beginPending() {
        if (state.stopping() || active || !state.current()) return;
        const auto id = *state.current();
        if (!start(id)) {
            const auto delivery = state.finish(id, PickerRequestState::Result::Failed);
            if (delivery == PickerRequestState::Delivery::Failed) {
                setVisible(false);
                emit q->failed();
            }
        } else {
            emit q->currentProcessIdChanged();
        }
    }

    static void restoreOwner(const Request& request) {
        if (!request.ownerWasEnabled || !request.owner || !IsWindow(request.hwnd) ||
            reinterpret_cast<HWND>(request.owner->winId()) != request.hwnd)
            return;
        DWORD pid = 0;
        if (GetWindowThreadProcessId(request.hwnd, &pid) == GetCurrentThreadId() &&
            pid == GetCurrentProcessId() && !IsWindowEnabled(request.hwnd))
            EnableWindow(request.hwnd, TRUE);
    }

    static void restoreForegroundBeforeCancel(const Request& request) {
        if (!request.ownerWasEnabled || !request.owner || !request.owner->isVisible() ||
            request.owner->property("presentationSuspended").toBool() || !IsWindow(request.hwnd) ||
            reinterpret_cast<HWND>(request.owner->winId()) != request.hwnd)
            return;
        const HWND foreground = GetForegroundWindow();
        if (!foreground || !IsWindowVisible(foreground) ||
            !native_picker::isTrustedForeground(request.hwnd, foreground))
            return;
        DWORD pid = 0;
        if (GetWindowThreadProcessId(request.hwnd, &pid) != GetCurrentThreadId() ||
            pid != GetCurrentProcessId())
            return;
        // A user dismissing the currently foreground picker is returning to
        // its own menu. Restore before terminating the helper: afterwards
        // Windows may choose an unrelated foreground window because the owner
        // was disabled. Never activate if focus has already moved elsewhere.
        if (!IsWindowEnabled(request.hwnd)) EnableWindow(request.hwnd, TRUE);
        if (GetForegroundWindow() == foreground)
            SetForegroundWindow(request.hwnd);
    }

    void finished() {
        if (!active) return;
        auto request = std::move(active);
        request->notifier->setEnabled(false);
        request->notifier->deleteLater();
        request->notifier = nullptr;
        if (request->registered) {
            native_picker::unregisterForegroundOwner(request->hwnd, request->process);
            request->registered = false;
        }
        DWORD exitCode = 1;
        GetExitCodeProcess(request->process, &exitCode);
        const LONG nativeError = request->shared->error;
        auto result = PickerRequestState::Result::Failed;
        QString path;
        if (exitCode == 0 && request->shared->result == static_cast<LONG>(native_picker::Result::Rejected)) {
            result = PickerRequestState::Result::Rejected;
        } else if (exitCode == 0 &&
                   request->shared->result == static_cast<LONG>(native_picker::Result::Accepted) &&
                   request->shared->path[0] &&
                   wmemchr(request->shared->path, L'\0', native_picker::pathCapacity)) {
            path = QString::fromWCharArray(request->shared->path);
            result = PickerRequestState::Result::Accepted;
        }
        restoreOwner(*request);
        const auto delivery = state.finish(request->id, result);
        request.reset();
        emit q->currentProcessIdChanged();
        if (delivery != PickerRequestState::Delivery::Ignore) {
            setVisible(false);
            if (delivery == PickerRequestState::Delivery::Accepted) {
                selectedFile = QUrl::fromLocalFile(path);
                emit q->selectedFileChanged();
                emit q->accepted();
            } else if (delivery == PickerRequestState::Delivery::Rejected) {
                emit q->rejected();
            } else {
                qWarning().nospace() << "Native layout picker failed: processExit=" << exitCode
                                     << " nativeError=0x" << Qt::hex << quint32(nativeError) << Qt::dec;
                emit q->failed();
            }
        }
        QTimer::singleShot(0, q, [this] { beginPending(); });
    }
#endif

    void open() {
        if (!q->supported()) return;
        const auto id = state.begin();
        if (!id) { emit q->failed(); return; }
        setVisible(true);
#ifdef Q_OS_WIN
        beginPending();
#endif
    }

    void close() {
        const auto id = state.cancel();
        if (!id) return;
        emit q->currentProcessIdChanged();
        setVisible(false);
#ifdef Q_OS_WIN
        if (active && active->id == *id) {
            restoreForegroundBeforeCancel(*active);
            if (active->registered) {
                native_picker::unregisterForegroundOwner(active->hwnd, active->process);
                active->registered = false;
            }
            // Terminating another process is asynchronous; the event notifier
            // waits for actual exit before restoring its owner or starting the
            // next request. No COM call or process wait runs on this thread.
            if (!TerminateProcess(active->process, 0x88))
                TerminateJobObject(active->job, 0x88);
        } else {
            (void)state.finish(*id, PickerRequestState::Result::Rejected);
        }
#else
        (void)state.finish(*id, PickerRequestState::Result::Rejected);
#endif
        emit q->rejected();
    }

    void shutdown() {
        if (state.stopping()) return;
        (void)state.shutdown();
#ifdef Q_OS_WIN
        if (active) {
            if (active->registered) {
                native_picker::unregisterForegroundOwner(active->hwnd, active->process);
                active->registered = false;
            }
            active->notifier->setEnabled(false);
            // Preserve the job until this object is destroyed. The desktop
            // host's aboutToQuit handler can restore system entry before the
            // eventual job close; the request process is asked to exit now.
            TerminateProcess(active->process, 0x88);
        }
#endif
        visible = false;
    }
};

NativeLayoutPicker::NativeLayoutPicker(QObject* parent)
    : QObject(parent), d(std::make_unique<NativeLayoutPickerPrivate>(this)) {
    if (qApp)
        connect(qApp, &QCoreApplication::aboutToQuit, this, [this] { d->shutdown(); });
}
NativeLayoutPicker::~NativeLayoutPicker() = default;
bool NativeLayoutPicker::supported() const {
#ifdef Q_OS_WIN
    return QGuiApplication::platformName() == QLatin1String("windows");
#else
    return false;
#endif
}
bool NativeLayoutPicker::visible() const { return d->visible; }
quint32 NativeLayoutPicker::currentProcessId() const {
#ifdef Q_OS_WIN
    if (d->active && d->state.current() == d->active->id)
        return GetProcessId(d->active->process);
#endif
    return 0;
}
QWindow* NativeLayoutPicker::parentWindow() const { return d->parentWindow; }
void NativeLayoutPicker::setParentWindow(QWindow* value) {
    if (d->parentWindow == value) return;
    d->parentWindow = value;
    emit parentWindowChanged();
}
QString NativeLayoutPicker::title() const { return d->title; }
void NativeLayoutPicker::setTitle(const QString& value) {
    if (d->title == value) return;
    d->title = value;
    emit titleChanged();
}
QString NativeLayoutPicker::jsonFilter() const { return d->jsonFilter; }
void NativeLayoutPicker::setJsonFilter(const QString& value) {
    if (d->jsonFilter == value) return;
    d->jsonFilter = value;
    emit filtersChanged();
}
QString NativeLayoutPicker::allFilter() const { return d->allFilter; }
void NativeLayoutPicker::setAllFilter(const QString& value) {
    if (d->allFilter == value) return;
    d->allFilter = value;
    emit filtersChanged();
}
QUrl NativeLayoutPicker::selectedFile() const { return d->selectedFile; }
void NativeLayoutPicker::open() { d->open(); }
void NativeLayoutPicker::close() { d->close(); }

} // namespace xdstarter
