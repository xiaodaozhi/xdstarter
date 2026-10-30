// Hosted-only experiment for Issue #88. This is not a product file picker.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shobjidl.h>
#include <objbase.h>

#include <atomic>
#include <chrono>
#include <iostream>
#include <string>
#include <thread>
#include <utility>

namespace {

struct Request {
    HWND owner = nullptr;
    std::wstring title;
    HANDLE checked = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    HANDLE proceed = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    HANDLE finished = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    std::atomic_bool cancel{false};
    std::atomic_int callbacks{0};
    std::atomic_int closeCalls{0};
    std::atomic<HRESULT> closeResult{E_PENDING};
    std::atomic<HRESULT> showResult{E_PENDING};
    std::atomic<HRESULT> setupResult{E_PENDING};
    std::atomic_bool skipped{false};
    std::atomic_bool abortBeforeShow{false};
    std::atomic<DWORD> threadId{0};
    std::thread worker;

    explicit Request(HWND hwnd, std::wstring requestTitle)
        : owner(hwnd), title(std::move(requestTitle)) {}
    ~Request() {
        if (worker.joinable()) worker.join();
        if (checked) CloseHandle(checked);
        if (proceed) CloseHandle(proceed);
        if (finished) CloseHandle(finished);
    }
};

class DialogEvents final : public IFileDialogEvents {
  public:
    explicit DialogEvents(Request& request) : request_(request) {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** out) override {
        if (!out) return E_POINTER;
        *out = nullptr;
        if (iid == IID_IUnknown || iid == IID_IFileDialogEvents) {
            *out = static_cast<IFileDialogEvents*>(this);
            AddRef();
            return S_OK;
        }
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs_; }
    ULONG STDMETHODCALLTYPE Release() override {
        const auto count = --refs_;
        if (!count) delete this;
        return count;
    }
    HRESULT STDMETHODCALLTYPE OnFileOk(IFileDialog* dialog) override { return onEvent(dialog); }
    HRESULT STDMETHODCALLTYPE OnFolderChanging(IFileDialog* dialog, IShellItem*) override { return onEvent(dialog); }
    HRESULT STDMETHODCALLTYPE OnFolderChange(IFileDialog* dialog) override { return onEvent(dialog); }
    HRESULT STDMETHODCALLTYPE OnSelectionChange(IFileDialog* dialog) override { return onEvent(dialog); }
    HRESULT STDMETHODCALLTYPE OnShareViolation(IFileDialog* dialog, IShellItem*, FDE_SHAREVIOLATION_RESPONSE* response) override {
        if (response) *response = FDESVR_DEFAULT;
        return onEvent(dialog);
    }
    HRESULT STDMETHODCALLTYPE OnTypeChange(IFileDialog* dialog) override { return onEvent(dialog); }
    HRESULT STDMETHODCALLTYPE OnOverwrite(IFileDialog* dialog, IShellItem*, FDE_OVERWRITE_RESPONSE* response) override {
        if (response) *response = FDEOR_DEFAULT;
        return onEvent(dialog);
    }

  private:
    HRESULT onEvent(IFileDialog* dialog) {
        ++request_.callbacks;
        if (request_.cancel.load()) {
            ++request_.closeCalls;
            request_.closeResult = dialog->Close(HRESULT_FROM_WIN32(ERROR_CANCELLED));
        }
        return S_OK;
    }
    std::atomic_ulong refs_{1};
    Request& request_;
};

struct Lookup { HWND owner; const std::wstring* title; HWND found = nullptr; };
BOOL CALLBACK findDialog(HWND window, LPARAM raw) {
    auto& lookup = *reinterpret_cast<Lookup*>(raw);
    DWORD pid = 0;
    GetWindowThreadProcessId(window, &pid);
    if (pid != GetCurrentProcessId() || GetWindow(window, GW_OWNER) != lookup.owner || !IsWindowVisible(window))
        return TRUE;
    wchar_t title[160]{};
    GetWindowTextW(window, title, int(sizeof(title) / sizeof(title[0])));
    if (*lookup.title != title) return TRUE;
    lookup.found = window;
    return FALSE;
}
HWND visible(const Request& request) {
    Lookup lookup{request.owner, &request.title};
    EnumWindows(findDialog, reinterpret_cast<LPARAM>(&lookup));
    return lookup.found;
}

void pump() {
    MSG message{};
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
}

[[noreturn]] void fail(const char* message) {
    std::cerr << "FAIL " << message << std::endl;
    // All windows and threads belong only to this hosted fixture process.
    ExitProcess(1);
}
void require(bool condition, const char* message) { if (!condition) fail(message); }

void start(Request& request, bool skipIfCancelled) {
    require(request.checked && request.proceed && request.finished, "event creation");
    request.worker = std::thread([&request, skipIfCancelled] {
        request.threadId = GetCurrentThreadId();
        const HRESULT init = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
        if (FAILED(init)) { request.setupResult = init; SetEvent(request.finished); return; }
        IFileOpenDialog* dialog = nullptr;
        HRESULT result = CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                                          IID_PPV_ARGS(&dialog));
        if (FAILED(result)) { request.setupResult = result; CoUninitialize(); SetEvent(request.finished); return; }
        result = dialog->SetTitle(request.title.c_str());
        FILEOPENDIALOGOPTIONS options{};
        if (SUCCEEDED(result)) result = dialog->GetOptions(&options);
        if (SUCCEEDED(result)) result = dialog->SetOptions(options | FOS_FORCEFILESYSTEM | FOS_FILEMUSTEXIST);
        auto* events = new DialogEvents(request);
        DWORD cookie = 0;
        if (SUCCEEDED(result)) result = dialog->Advise(events, &cookie);
        request.setupResult = result;
        if (SUCCEEDED(result)) {
            if (skipIfCancelled && request.cancel.load()) {
                request.skipped = true;
                SetEvent(request.checked);
            } else {
                // The fixture deliberately pauses after the cancel check to
                // create the race between that check and IFileDialog::Show.
                SetEvent(request.checked);
                if (WaitForSingleObject(request.proceed, 10000) == WAIT_OBJECT_0) {
                    if (request.abortBeforeShow.load()) {
                        request.skipped = true;
                        request.showResult = HRESULT_FROM_WIN32(ERROR_CANCELLED);
                    } else {
                        request.showResult = dialog->Show(request.owner);
                    }
                } else
                    request.showResult = HRESULT_FROM_WIN32(ERROR_TIMEOUT);
            }
            dialog->Unadvise(cookie);
        }
        events->Release();
        dialog->Release();
        CoUninitialize();
        SetEvent(request.finished);
    });
}

bool waitEvent(HANDLE event, DWORD timeoutMs) {
    const auto started = GetTickCount64();
    while (GetTickCount64() - started < timeoutMs) {
        if (WaitForSingleObject(event, 0) == WAIT_OBJECT_0) return true;
        pump();
        Sleep(10);
    }
    return WaitForSingleObject(event, 0) == WAIT_OBJECT_0;
}

bool waitVisible(Request& request, DWORD timeoutMs) {
    const auto started = GetTickCount64();
    while (GetTickCount64() - started < timeoutMs) {
        if (visible(request)) return true;
        if (WaitForSingleObject(request.finished, 0) == WAIT_OBJECT_0) return false;
        pump();
        Sleep(10);
    }
    return bool(visible(request));
}

bool cancelAndWait(Request& request, DWORD timeoutMs, bool* lateObserved = nullptr) {
    request.cancel = true;
    SetEvent(request.proceed);
    const auto started = GetTickCount64();
    while (GetTickCount64() - started < timeoutMs) {
        if (WaitForSingleObject(request.finished, 0) == WAIT_OBJECT_0) return true;
        if (HWND window = visible(request)) {
            if (lateObserved) *lateObserved = true;
            PostMessageW(window, WM_CLOSE, 0, 0);
        }
        pump();
        Sleep(10);
    }
    return WaitForSingleObject(request.finished, 0) == WAIT_OBJECT_0;
}

HWND ownerWindow() {
    return CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, L"STATIC", L"XDStarter isolated picker owner",
                           WS_POPUP | WS_VISIBLE, 0, 0, 1, 1, nullptr, nullptr,
                           GetModuleHandleW(nullptr), nullptr);
}

void printResult(const char* name, const Request& request, bool observed) {
    std::cout << name << " callbackCount=" << request.callbacks.load()
              << " callbackCloseCalls=" << request.closeCalls.load()
              << " closeResult=0x" << std::hex << static_cast<unsigned long>(request.closeResult.load())
              << " showResult=0x" << static_cast<unsigned long>(request.showResult.load()) << std::dec
              << " cancelledWindowObserved=" << observed << std::endl;
}

bool hosted() {
    wchar_t actions[16]{}, runner[32]{};
    GetEnvironmentVariableW(L"GITHUB_ACTIONS", actions, 16);
    GetEnvironmentVariableW(L"RUNNER_ENVIRONMENT", runner, 32);
    return std::wstring(actions) == L"true" && std::wstring(runner) == L"github-hosted";
}

struct ThreadDialogLookup {
    DWORD threadId;
    const std::wstring* title;
    HWND found = nullptr;
};
BOOL CALLBACK findThreadDialog(HWND window, LPARAM raw) {
    auto& lookup = *reinterpret_cast<ThreadDialogLookup*>(raw);
    DWORD pid = 0;
    const DWORD thread = GetWindowThreadProcessId(window, &pid);
    if (pid != GetCurrentProcessId() || thread != lookup.threadId || !IsWindowVisible(window))
        return TRUE;
    wchar_t title[160]{};
    GetWindowTextW(window, title, int(sizeof(title) / sizeof(title[0])));
    if (*lookup.title != title) return TRUE;
    lookup.found = window;
    return FALSE;
}

std::atomic_int observedShows{0};
std::atomic<DWORD> watchedThread{0};
std::wstring watchedTitle;
void CALLBACK onShow(HWINEVENTHOOK, DWORD event, HWND window, LONG object, LONG child,
                     DWORD thread, DWORD timestamp) {
    if (event != EVENT_OBJECT_SHOW || object != OBJID_WINDOW || child != CHILDID_SELF ||
        thread != watchedThread.load())
        return;
    DWORD pid = 0;
    GetWindowThreadProcessId(window, &pid);
    if (pid != GetCurrentProcessId()) return;
    wchar_t title[160]{};
    GetWindowTextW(window, title, int(sizeof(title) / sizeof(title[0])));
    if (watchedTitle != title) return;
    ++observedShows;
    std::cout << "owner-destroy event=show thread=" << thread << " time=" << timestamp
              << " hwnd=" << window << " owner=" << GetWindow(window, GW_OWNER) << std::endl;
}

void probeDestroyedOwner() {
    HWND oldOwner = ownerWindow();
    require(oldOwner, "owner-destroy owner creation");
    Request old(oldOwner, L"XDStarter owner-destroy-before-show");
    start(old, false);
    require(waitEvent(old.checked, 10000) && SUCCEEDED(old.setupResult.load()) && old.threadId.load(),
            "owner-destroy setup and thread identity");
    watchedThread = old.threadId.load();
    watchedTitle = old.title;
    observedShows = 0;
    HWINEVENTHOOK hook = SetWinEventHook(EVENT_OBJECT_SHOW, EVENT_OBJECT_SHOW, nullptr, onShow,
                                         GetCurrentProcessId(), old.threadId.load(), WINEVENT_OUTOFCONTEXT);
    require(hook, "owner-destroy event observation");
    const auto destroyedAt = GetTickCount64();
    require(DestroyWindow(oldOwner) != FALSE && !IsWindow(oldOwner), "destroy original owner");
    HWND replacement = ownerWindow();
    require(replacement, "owner-destroy replacement creation");
    const bool reused = replacement == oldOwner;
    std::cout << "owner-destroy process=" << GetCurrentProcessId() << " thread=" << old.threadId.load()
              << " oldOwner=" << oldOwner << " replacement=" << replacement
              << " reused=" << reused << " destroyedAt=" << destroyedAt << std::endl;
    if (reused) {
        // A recycled HWND could make the old request target the new owner. Do
        // not release the test barrier into that ambiguous and unsafe case.
        old.abortBeforeShow = true;
        SetEvent(old.proceed);
        require(waitEvent(old.finished, 10000), "reused-owner fixture completion");
    } else {
        // Deliberately allow Show after the owner is destroyed, without setting
        // the event callback's cancel flag: this isolates owner destruction.
        SetEvent(old.proceed);
        const auto started = GetTickCount64();
        bool observed = false;
        while (GetTickCount64() - started < 10000 &&
               WaitForSingleObject(old.finished, 0) != WAIT_OBJECT_0) {
            pump();
            ThreadDialogLookup lookup{old.threadId.load(), &old.title};
            EnumWindows(findThreadDialog, reinterpret_cast<LPARAM>(&lookup));
            if (lookup.found) {
                observed = true;
                // The fixture verifies process, thread and unique title again;
                // it never closes another process or a later request's window.
                DWORD pid = 0;
                const DWORD tid = GetWindowThreadProcessId(lookup.found, &pid);
                if (pid == GetCurrentProcessId() && tid == old.threadId.load())
                    PostMessageW(lookup.found, WM_CLOSE, 0, 0);
            }
            Sleep(10);
        }
        pump();
        std::cout << "owner-destroy showResult=0x" << std::hex
                  << static_cast<unsigned long>(old.showResult.load()) << std::dec
                  << " finished=" << (WaitForSingleObject(old.finished, 0) == WAIT_OBJECT_0)
                  << " eventShows=" << observedShows.load() << " polledVisible=" << observed
                  << " elapsedMs=" << GetTickCount64() - started << std::endl;
        require(WaitForSingleObject(old.finished, 0) == WAIT_OBJECT_0,
                "destroyed-owner Show did not finish within fixture bound");
    }
    UnhookWinEvent(hook);
    watchedThread = 0;
    DestroyWindow(replacement);
}

} // namespace

int wmain(int argc, wchar_t** argv) {
    if (argc != 2 || std::wstring(argv[1]) != L"--hosted-test" || !hosted()) {
        std::cerr << "This fixture only runs on an explicitly requested GitHub-hosted Windows runner." << std::endl;
        return 2;
    }
    HWND ownerA = ownerWindow(), ownerB = ownerWindow();
    require(ownerA && ownerB && ownerA != ownerB, "distinct owned fixture windows");

    {
        Request before(ownerA, L"XDStarter picker PoC before");
        before.cancel = true;
        start(before, true);
        require(waitEvent(before.finished, 10000), "pre-cancel completion");
        require(SUCCEEDED(before.setupResult.load()) && before.skipped.load() && !visible(before), "pre-cancel skips Show");
        printResult("pre-cancel", before, false);
    }
    {
        Request raced(ownerA, L"XDStarter picker PoC shared title");
        start(raced, false);
        require(waitEvent(raced.checked, 10000), "raced check");
        require(SUCCEEDED(raced.setupResult.load()), "raced setup");
        raced.cancel = true;

        Request next(ownerB, L"XDStarter picker PoC shared title");
        start(next, false);
        require(waitEvent(next.checked, 10000), "next check");
        require(SUCCEEDED(next.setupResult.load()), "next setup");
        SetEvent(next.proceed);
        require(waitVisible(next, 10000), "subsequent window visible");
        Lookup wrongOwner{ownerA, &next.title};
        EnumWindows(findDialog, reinterpret_cast<LPARAM>(&wrongOwner));
        require(!wrongOwner.found, "owner identity prevents matching later request");
        require(WaitForSingleObject(raced.finished, 0) == WAIT_TIMEOUT,
                "raced request remains pending while next is visible");
        bool lateObserved = false;
        require(cancelAndWait(raced, 10000, &lateObserved), "raced cancellation finishes");
        require(raced.callbacks.load() > 0 || lateObserved, "raced Show reached a callback or visible window");
        require(raced.showResult.load() == HRESULT_FROM_WIN32(ERROR_CANCELLED), "raced Show reports cancellation");
        require(bool(visible(next)), "cancelling first request leaves later request visible");
        printResult("raced-cancel", raced, lateObserved);
        bool nextObserved = false;
        require(cancelAndWait(next, 10000, &nextObserved), "visible cancellation finishes");
        require(next.showResult.load() == HRESULT_FROM_WIN32(ERROR_CANCELLED), "visible Show reports cancellation");
        require(!visible(next), "cancelled next window disappears");
        printResult("visible-cancel", next, nextObserved);
    }
    probeDestroyedOwner();
    DestroyWindow(ownerB);
    DestroyWindow(ownerA);
    std::cout << "PASS hosted cancellation fixture; no product integration or physical input tested" << std::endl;
    return 0;
}
