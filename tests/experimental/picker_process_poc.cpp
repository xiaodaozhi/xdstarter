// Disposable hosted-only process-boundary experiment for Issue #88.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shobjidl.h>
#include <objbase.h>
#include <UIAutomation.h>

#include <atomic>
#include <cstdint>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

namespace {

constexpr wchar_t title[] = L"XDStarter isolated process picker";

struct Shared {
    volatile LONG phase; // 1=ready, 2=about to call Show, 3=Show returned
    volatile LONG processId;
    volatile LONG threadId;
    volatile LONG showResult;
    volatile LONG selectedMatched;
};

bool hosted() {
    wchar_t actions[16]{}, runner[32]{};
    GetEnvironmentVariableW(L"GITHUB_ACTIONS", actions, 16);
    GetEnvironmentVariableW(L"RUNNER_ENVIRONMENT", runner, 32);
    return std::wstring(actions) == L"true" && std::wstring(runner) == L"github-hosted";
}

[[noreturn]] void fail(const char* message) {
    std::cerr << "FAIL " << message << " win32=" << GetLastError() << std::endl;
    // The fixture owns every process/job it creates. Process exit closes job
    // handles, so Windows kills only those isolated children.
    ExitProcess(1);
}
void require(bool value, const char* message) { if (!value) fail(message); }

void pump() {
    MSG message{};
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
}
bool wait(HANDLE handle, DWORD timeout) {
    const auto started = GetTickCount64();
    while (GetTickCount64() - started < timeout) {
        if (WaitForSingleObject(handle, 0) == WAIT_OBJECT_0) return true;
        pump();
        Sleep(10);
    }
    return WaitForSingleObject(handle, 0) == WAIT_OBJECT_0;
}

struct Lookup {
    DWORD pid;
    HWND owner;
    HWND found = nullptr;
    DWORD thread = 0;
};
BOOL CALLBACK findWindow(HWND window, LPARAM raw) {
    auto& lookup = *reinterpret_cast<Lookup*>(raw);
    DWORD pid = 0;
    const DWORD thread = GetWindowThreadProcessId(window, &pid);
    if (pid != lookup.pid || !IsWindowVisible(window) ||
        (lookup.owner && GetWindow(window, GW_OWNER) != lookup.owner))
        return TRUE;
    wchar_t text[128]{};
    GetWindowTextW(window, text, int(sizeof(text) / sizeof(text[0])));
    if (std::wstring(text) != title) return TRUE;
    lookup.found = window;
    lookup.thread = thread;
    return FALSE;
}
Lookup lookup(DWORD pid, HWND owner = nullptr) {
    Lookup result{pid, owner};
    EnumWindows(findWindow, reinterpret_cast<LPARAM>(&result));
    return result;
}
BOOL CALLBACK countWindows(HWND window, LPARAM raw) {
    auto& target = *reinterpret_cast<std::pair<DWORD, unsigned int>*>(raw);
    DWORD pid = 0;
    GetWindowThreadProcessId(window, &pid);
    if (pid == target.first) ++target.second;
    return TRUE;
}
unsigned int processWindowCount(DWORD pid) {
    std::pair<DWORD, unsigned int> result{pid, 0};
    EnumWindows(countWindows, reinterpret_cast<LPARAM>(&result));
    return result.second;
}
bool waitVisible(DWORD pid, HWND owner, DWORD timeout) {
    const auto started = GetTickCount64();
    while (GetTickCount64() - started < timeout) {
        if (lookup(pid, owner).found) return true;
        pump();
        Sleep(10);
    }
    return lookup(pid, owner).found != nullptr;
}
bool waitForeground(DWORD pid, HWND exact, DWORD timeout) {
    const auto started = GetTickCount64();
    while (GetTickCount64() - started < timeout) {
        const HWND foreground = GetForegroundWindow();
        DWORD ownerPid = 0;
        GetWindowThreadProcessId(foreground, &ownerPid);
        if (foreground && (!exact || foreground == exact) && (!pid || ownerPid == pid)) return true;
        pump();
        Sleep(10);
    }
    return false;
}
bool invokeOpen(HWND window, DWORD expectedPid) {
    IUIAutomation* automation = nullptr;
    IUIAutomationElement* root = nullptr;
    IUIAutomationCondition* condition = nullptr;
    IUIAutomationElement* button = nullptr;
    IUIAutomationInvokePattern* invoke = nullptr;
    VARIANT value;
    VariantInit(&value);
    value.vt = VT_BSTR;
    value.bstrVal = SysAllocString(L"1"); // Native common-dialog IDOK.
    HRESULT hr = value.bstrVal ? CoCreateInstance(CLSID_CUIAutomation, nullptr, CLSCTX_INPROC_SERVER,
                                                  IID_PPV_ARGS(&automation)) : E_OUTOFMEMORY;
    if (SUCCEEDED(hr)) hr = automation->ElementFromHandle(window, &root);
    if (SUCCEEDED(hr)) hr = automation->CreatePropertyCondition(UIA_AutomationIdPropertyId, value, &condition);
    if (SUCCEEDED(hr)) hr = root->FindFirst(TreeScope_Descendants, condition, &button);
    int pid = 0;
    CONTROLTYPEID type = 0;
    if (SUCCEEDED(hr) && button) hr = button->get_CurrentProcessId(&pid);
    if (SUCCEEDED(hr) && button) hr = button->get_CurrentControlType(&type);
    if (SUCCEEDED(hr) && button && pid == static_cast<int>(expectedPid) && type == UIA_ButtonControlTypeId)
        hr = button->GetCurrentPatternAs(UIA_InvokePatternId, IID_PPV_ARGS(&invoke));
    else
        hr = E_FAIL;
    if (SUCCEEDED(hr) && invoke) hr = invoke->Invoke();
    if (invoke) invoke->Release();
    if (button) button->Release();
    if (condition) condition->Release();
    if (root) root->Release();
    if (automation) automation->Release();
    VariantClear(&value);
    return SUCCEEDED(hr);
}

std::atomic<DWORD> watchedPid{0};
std::atomic_int showCount{0};
void CALLBACK onShow(HWINEVENTHOOK, DWORD event, HWND window, LONG object, LONG child,
                     DWORD thread, DWORD timestamp) {
    if (event != EVENT_OBJECT_SHOW || object != OBJID_WINDOW || child != CHILDID_SELF)
        return;
    DWORD pid = 0;
    GetWindowThreadProcessId(window, &pid);
    if (pid != watchedPid.load()) return;
    wchar_t text[128]{};
    GetWindowTextW(window, text, int(sizeof(text) / sizeof(text[0])));
    if (std::wstring(text) != title) return;
    ++showCount;
    std::cout << "event=show pid=" << pid << " thread=" << thread << " hwnd=" << window
              << " owner=" << GetWindow(window, GW_OWNER) << " eventTime=" << timestamp << std::endl;
}

HWND makeOwner(const wchar_t* label) {
    return CreateWindowExW(WS_EX_TOOLWINDOW, L"STATIC", label, WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                           10, 10, 240, 160, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
}
HWND makeProxyOwner(HWND owner) {
    RECT bounds{};
    if (!GetWindowRect(owner, &bounds)) return nullptr;
    return CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, L"STATIC", L"XDStarter request owner",
                           WS_POPUP, (bounds.left + bounds.right) / 2, (bounds.top + bounds.bottom) / 2,
                           1, 1, owner, nullptr, GetModuleHandleW(nullptr), nullptr);
}

HANDLE privateJob() {
    HANDLE job = CreateJobObjectW(nullptr, nullptr);
    require(job, "create private job");
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    require(SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits, sizeof(limits)) != FALSE,
            "configure kill-on-close job");
    return job;
}

PROCESS_INFORMATION startInJob(HANDLE job, const std::wstring& arguments) {
    wchar_t executable[32768]{};
    const DWORD size = GetModuleFileNameW(nullptr, executable, DWORD(sizeof(executable) / sizeof(executable[0])));
    require(size > 0 && size < sizeof(executable) / sizeof(executable[0]), "own executable path");
    const std::wstring command = L"\"" + std::wstring(executable) + L"\" " + arguments;
    std::vector<wchar_t> mutableCommand(command.begin(), command.end());
    mutableCommand.push_back(L'\0');
    SIZE_T attributeBytes = 0;
    InitializeProcThreadAttributeList(nullptr, 1, 0, &attributeBytes);
    require(attributeBytes > 0, "size process attribute list");
    std::vector<unsigned char> attributeStorage(attributeBytes);
    auto* attributes = reinterpret_cast<PPROC_THREAD_ATTRIBUTE_LIST>(attributeStorage.data());
    require(InitializeProcThreadAttributeList(attributes, 1, 0, &attributeBytes) != FALSE,
            "initialize process attribute list");
    HANDLE jobs[] = {job};
    require(UpdateProcThreadAttribute(attributes, 0, PROC_THREAD_ATTRIBUTE_JOB_LIST, jobs,
                                      sizeof(jobs), nullptr, nullptr) != FALSE,
            "bind private job during process creation");
    STARTUPINFOEXW startup{};
    startup.StartupInfo.cb = sizeof(startup);
    startup.lpAttributeList = attributes;
    PROCESS_INFORMATION created{};
    const BOOL createdOk = CreateProcessW(executable, mutableCommand.data(), nullptr, nullptr, FALSE,
                                         CREATE_SUSPENDED | CREATE_NO_WINDOW | EXTENDED_STARTUPINFO_PRESENT,
                                         nullptr, nullptr, &startup.StartupInfo, &created);
    const DWORD createError = GetLastError();
    DeleteProcThreadAttributeList(attributes);
    if (!createdOk) {
        SetLastError(createError);
        fail("start suspended child in private job");
    }
    if (ResumeThread(created.hThread) == DWORD(-1)) {
        const DWORD resumeError = GetLastError();
        TerminateJobObject(job, 0x89);
        SetLastError(resumeError);
        fail("resume child");
    }
    return created;
}

struct Child {
    HANDLE ready = nullptr;
    HANDLE go = nullptr;
    HANDLE job = nullptr;
    HANDLE process = nullptr;
    HANDLE thread = nullptr;
    HANDLE mapping = nullptr;
    Shared* shared = nullptr;
    DWORD pid = 0;
    DWORD mainThread = 0;
    HWND owner = nullptr;
    Child(const Child&) = delete;
    Child& operator=(const Child&) = delete;

    explicit Child(HWND ownerWindow, unsigned int serial, const std::wstring& selectedFile = {}) : owner(ownerWindow) {
        const auto prefix = L"Local\\XDStarter-picker-fixture-" + std::to_wstring(GetCurrentProcessId()) +
                            L"-" + std::to_wstring(GetTickCount64()) + L"-" + std::to_wstring(serial);
        const auto readyName = prefix + L"-ready";
        const auto goName = prefix + L"-go";
        const auto mappingName = prefix + L"-state";
        SetLastError(ERROR_SUCCESS);
        ready = CreateEventW(nullptr, TRUE, FALSE, readyName.c_str());
        require(ready && GetLastError() != ERROR_ALREADY_EXISTS, "create unique ready event");
        SetLastError(ERROR_SUCCESS);
        go = CreateEventW(nullptr, TRUE, FALSE, goName.c_str());
        require(go && GetLastError() != ERROR_ALREADY_EXISTS, "create unique go event");
        SetLastError(ERROR_SUCCESS);
        mapping = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, sizeof(Shared),
                                     mappingName.c_str());
        require(mapping && GetLastError() != ERROR_ALREADY_EXISTS, "create unique shared state");
        shared = static_cast<Shared*>(MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(Shared)));
        require(shared, "map shared fixture state");
        ZeroMemory(shared, sizeof(Shared));
        job = privateJob();
        std::wstring arguments = L"--child " +
                               std::to_wstring(reinterpret_cast<std::uintptr_t>(owner)) + L" " +
                               readyName + L" " + goName + L" " + mappingName;
        if (!selectedFile.empty()) {
            require(selectedFile.find(L'"') == std::wstring::npos, "safe fixture filename");
            arguments += L" --file \"" + selectedFile + L"\"";
        }
        const PROCESS_INFORMATION created = startInJob(job, arguments);
        process = created.hProcess;
        thread = created.hThread;
        pid = created.dwProcessId;
        mainThread = created.dwThreadId;
        std::cout << "child-created pid=" << pid << " thread=" << mainThread << " owner=" << owner << std::endl;
    }

    ~Child() {
        if (job) CloseHandle(job);
        if (thread) CloseHandle(thread);
        if (process) CloseHandle(process);
        if (shared) UnmapViewOfFile(shared);
        if (mapping) CloseHandle(mapping);
        if (ready) CloseHandle(ready);
        if (go) CloseHandle(go);
    }

    void terminate(const char* label) {
        const auto started = GetTickCount64();
        require(TerminateJobObject(job, 0x88) != FALSE, "terminate only this private job");
        require(wait(process, 5000), "child process terminates after job cancellation");
        DWORD exitCode = 0;
        require(GetExitCodeProcess(process, &exitCode) != FALSE && exitCode == 0x88, "cancelled child exit code");
        pump();
        require(processWindowCount(pid) == 0, "no child top-level window after process exit");
        std::cout << label << " pid=" << pid << " exit=" << exitCode
                  << " elapsedMs=" << GetTickCount64() - started
                  << " childPhase=" << shared->phase << " childThread=" << shared->threadId
                  << " ownerEnabled=" << bool(IsWindowEnabled(owner)) << std::endl;
    }
};

struct AutomationChild {
    HANDLE job = nullptr;
    PROCESS_INFORMATION process{};
    AutomationChild(HWND window, DWORD targetPid) {
        job = privateJob();
        process = startInJob(job, L"--invoke " + std::to_wstring(reinterpret_cast<std::uintptr_t>(window)) +
                                  L" " + std::to_wstring(targetPid));
        std::cout << "automation-created pid=" << process.dwProcessId << " targetPid=" << targetPid
                  << " hwnd=" << window << std::endl;
    }
    ~AutomationChild() {
        if (job) CloseHandle(job);
        if (process.hThread) CloseHandle(process.hThread);
        if (process.hProcess) CloseHandle(process.hProcess);
    }
    bool finishAfterSelection() {
        if (!wait(process.hProcess, 1000)) {
            // UIA providers can remain blocked while the target closes. The
            // fixture never lets that auxiliary call block the tested parent.
            require(TerminateJobObject(job, 0x90) != FALSE && wait(process.hProcess, 5000),
                    "terminate blocked fixture automation");
            std::cout << "automation-timeout-after-selection pid=" << process.dwProcessId << std::endl;
            return false;
        }
        DWORD code = 0;
        require(GetExitCodeProcess(process.hProcess, &code) != FALSE, "read fixture automation status");
        std::cout << "automation-returned pid=" << process.dwProcessId << " exit=" << code << std::endl;
        return code == 0;
    }
};

int invokeMain(int argc, wchar_t** argv) {
    if (argc != 4) return 2;
    wchar_t* end = nullptr;
    const auto rawWindow = _wcstoui64(argv[2], &end, 10);
    if (!end || *end || !rawWindow) return 2;
    const auto expectedPid = wcstoul(argv[3], &end, 10);
    if (!end || *end || !expectedPid) return 2;
    const HWND window = reinterpret_cast<HWND>(static_cast<std::uintptr_t>(rawWindow));
    DWORD actualPid = 0;
    if (!IsWindow(window) || !GetWindowThreadProcessId(window, &actualPid) || actualPid != expectedPid)
        return 3;
    const HRESULT initialized = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    if (FAILED(initialized)) return 4;
    const bool invoked = invokeOpen(window, actualPid);
    CoUninitialize();
    return invoked ? 0 : 5;
}

int childMain(int argc, wchar_t** argv) {
    if (argc != 6 && argc != 8) return 2;
    if (argc == 8 && std::wstring(argv[6]) != L"--file") return 2;
    wchar_t* end = nullptr;
    const auto value = _wcstoui64(argv[2], &end, 10);
    if (!end || *end || !value) return 2;
    const HWND owner = reinterpret_cast<HWND>(static_cast<std::uintptr_t>(value));
    HANDLE ready = OpenEventW(EVENT_MODIFY_STATE, FALSE, argv[3]);
    HANDLE go = OpenEventW(SYNCHRONIZE, FALSE, argv[4]);
    HANDLE mapping = OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, argv[5]);
    if (!ready || !go || !mapping) return 3;
    auto* shared = static_cast<Shared*>(MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(Shared)));
    if (!shared) return 3;
    InterlockedExchange(&shared->processId, static_cast<LONG>(GetCurrentProcessId()));
    InterlockedExchange(&shared->threadId, static_cast<LONG>(GetCurrentThreadId()));
    const HRESULT initialized = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    if (FAILED(initialized)) return 4;
    IFileOpenDialog* dialog = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog));
    if (SUCCEEDED(hr)) hr = dialog->SetTitle(title);
    if (SUCCEEDED(hr)) hr = dialog->SetOptions(FOS_PATHMUSTEXIST | FOS_FILEMUSTEXIST);
    if (SUCCEEDED(hr) && argc == 8) hr = dialog->SetFileName(argv[7]);
    if (FAILED(hr)) return 5;
    InterlockedExchange(&shared->phase, 1);
    SetEvent(ready);
    if (WaitForSingleObject(go, 15000) != WAIT_OBJECT_0) return 6;
    InterlockedExchange(&shared->phase, 2);
    hr = dialog->Show(owner);
    InterlockedExchange(&shared->showResult, static_cast<LONG>(hr));
    if (hr == S_OK && argc == 8) {
        IShellItem* item = nullptr;
        PWSTR selected = nullptr;
        if (SUCCEEDED(dialog->GetResult(&item)) && item &&
            SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &selected)) && selected &&
            _wcsicmp(selected, argv[7]) == 0)
            InterlockedExchange(&shared->selectedMatched, 1);
        if (selected) CoTaskMemFree(selected);
        if (item) item->Release();
    }
    InterlockedExchange(&shared->phase, 3);
    const bool completed = (argc == 6 && hr == HRESULT_FROM_WIN32(ERROR_CANCELLED)) ||
                           (argc == 8 && hr == S_OK && shared->selectedMatched == 1);
    dialog->Release();
    CoUninitialize();
    UnmapViewOfFile(shared);
    CloseHandle(mapping);
    CloseHandle(go);
    CloseHandle(ready);
    return completed ? 0 : 7;
}

int launcherMain(int argc, wchar_t** argv) {
    if (argc != 5) return 2;
    HANDLE ready = OpenEventW(EVENT_MODIFY_STATE, FALSE, argv[2]);
    HANDLE finish = OpenEventW(SYNCHRONIZE, FALSE, argv[3]);
    HANDLE mapping = OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, argv[4]);
    if (!ready || !finish || !mapping) return 3;
    auto* shared = static_cast<Shared*>(MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(Shared)));
    if (!shared) return 3;
    HWND owner = makeOwner(L"XDStarter launcher-exit fixture");
    if (!owner) return 4;
    Child helper(owner, 99);
    if (!wait(helper.ready, 10000)) return 5;
    InterlockedExchange(&shared->processId, static_cast<LONG>(helper.pid));
    InterlockedExchange(&shared->threadId, static_cast<LONG>(helper.mainThread));
    InterlockedExchange(&shared->phase, 1);
    SetEvent(ready);
    const DWORD signaled = WaitForSingleObject(finish, 15000);
    // Deliberately skip C++ destruction: Windows must close this launcher's job
    // handle on process exit, thereby terminating its still-blocked helper.
    ExitProcess(signaled == WAIT_OBJECT_0 ? 0 : 6);
}

void probeLauncherExit() {
    const auto prefix = L"Local\\XDStarter-launcher-fixture-" + std::to_wstring(GetCurrentProcessId()) +
                        L"-" + std::to_wstring(GetTickCount64());
    const auto readyName = prefix + L"-ready";
    const auto finishName = prefix + L"-finish";
    const auto mappingName = prefix + L"-state";
    HANDLE ready = CreateEventW(nullptr, TRUE, FALSE, readyName.c_str());
    HANDLE finish = CreateEventW(nullptr, TRUE, FALSE, finishName.c_str());
    HANDLE mapping = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, sizeof(Shared),
                                        mappingName.c_str());
    require(ready && finish && mapping, "launcher fixture handles");
    auto* shared = static_cast<Shared*>(MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(Shared)));
    require(shared, "launcher fixture state");
    ZeroMemory(shared, sizeof(Shared));
    wchar_t executable[32768]{};
    const DWORD size = GetModuleFileNameW(nullptr, executable, DWORD(sizeof(executable) / sizeof(executable[0])));
    require(size > 0 && size < sizeof(executable) / sizeof(executable[0]), "launcher executable path");
    std::wstring command = L"\"" + std::wstring(executable) + L"\" --launcher " + readyName + L" " +
                           finishName + L" " + mappingName;
    std::vector<wchar_t> mutableCommand(command.begin(), command.end());
    mutableCommand.push_back(L'\0');
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION launcher{};
    require(CreateProcessW(executable, mutableCommand.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
                           nullptr, nullptr, &startup, &launcher) != FALSE, "start isolated launcher");
    if (!wait(ready, 10000)) {
        TerminateProcess(launcher.hProcess, 8);
        fail("launcher reports blocked helper");
    }
    const DWORD childPid = static_cast<DWORD>(shared->processId);
    const DWORD childThread = static_cast<DWORD>(shared->threadId);
    require(childPid && childThread && shared->phase == 1, "launcher child identity");
    HANDLE helper = OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, childPid);
    require(helper && GetProcessId(helper) == childPid, "open exact launcher helper");
    const auto started = GetTickCount64();
    require(SetEvent(finish) != FALSE && wait(launcher.hProcess, 5000), "launcher exits without waiting for helper");
    DWORD launcherExit = 0;
    require(GetExitCodeProcess(launcher.hProcess, &launcherExit) != FALSE && launcherExit == 0,
            "launcher exit status");
    require(wait(helper, 5000) && processWindowCount(childPid) == 0, "launcher exit terminates its helper");
    DWORD helperExit = 0;
    require(GetExitCodeProcess(helper, &helperExit) != FALSE && helperExit != STILL_ACTIVE,
            "helper exit status after launcher death");
    std::cout << "launcher-exit launcherPid=" << launcher.dwProcessId << " childPid=" << childPid
              << " childThread=" << childThread << " childExit=" << helperExit
              << " elapsedMs=" << GetTickCount64() - started << std::endl;
    CloseHandle(helper);
    CloseHandle(launcher.hThread);
    CloseHandle(launcher.hProcess);
    UnmapViewOfFile(shared);
    CloseHandle(mapping);
    CloseHandle(finish);
    CloseHandle(ready);
}

} // namespace

int wmain(int argc, wchar_t** argv) {
    if (!hosted()) {
        std::cerr << "Only a disposable GitHub-hosted Windows runner may execute this fixture." << std::endl;
        return 2;
    }
    if (argc >= 2 && std::wstring(argv[1]) == L"--child") return childMain(argc, argv);
    if (argc >= 2 && std::wstring(argv[1]) == L"--launcher") return launcherMain(argc, argv);
    if (argc >= 2 && std::wstring(argv[1]) == L"--invoke") return invokeMain(argc, argv);
    if (argc != 2 || std::wstring(argv[1]) != L"--hosted-test") return 2;
    const HRESULT initialized = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    require(SUCCEEDED(initialized), "initialize fixture COM");
    HWND ownerA = makeOwner(L"XDStarter process fixture A");
    HWND ownerB = makeOwner(L"XDStarter process fixture B");
    require(ownerA && ownerB && ownerA != ownerB, "distinct fixture owners");
    const bool ownerAInitiallyEnabled = IsWindowEnabled(ownerA) != FALSE;
    const bool ownerBInitiallyEnabled = IsWindowEnabled(ownerB) != FALSE;
    require(ownerAInitiallyEnabled && ownerBInitiallyEnabled, "owners initially enabled");
    bool selectionPassed = false;
    bool automationPassed = false;
    HWINEVENTHOOK hook = nullptr;
    const auto watch = [&](DWORD pid) {
        if (hook) UnhookWinEvent(hook);
        watchedPid = pid;
        showCount = 0;
        hook = SetWinEventHook(EVENT_OBJECT_SHOW, EVENT_OBJECT_SHOW, nullptr, onShow,
                               pid, 0, WINEVENT_OUTOFCONTEXT);
        require(hook, "observe only the fixture child process");
    };
    {
        Child old(ownerA, 1);
        require(wait(old.ready, 10000), "old child ready before Show");
        require(old.shared->processId == static_cast<LONG>(old.pid) &&
                old.shared->threadId == static_cast<LONG>(old.mainThread) && old.shared->phase == 1,
                "old child identity and pre-Show barrier");
        Child next(ownerB, 2);
        require(wait(next.ready, 10000), "next child ready");
        const bool activationRequested = SetForegroundWindow(ownerB) != FALSE;
        const bool ownerForeground = waitForeground(GetCurrentProcessId(), ownerB, 1000);
        std::cout << "focus-before activationRequested=" << activationRequested
                  << " ownerForeground=" << ownerForeground << std::endl;
        watch(next.pid);
        require(SetEvent(next.go) != FALSE && waitVisible(next.pid, ownerB, 10000), "next dialog visible");
        const bool dialogForeground = waitForeground(next.pid, nullptr, 3000);
        if (ownerForeground)
            require(dialogForeground, "visible owned dialog becomes foreground after active owner");
        const Lookup visible = lookup(next.pid, ownerB);
        require(visible.found && GetWindow(visible.found, GW_OWNER) == ownerB,
                "next dialog has exact owner");
        std::cout << "shown pid=" << next.pid << " thread=" << visible.thread
                  << " hwnd=" << visible.found << " owner=" << ownerB
                  << " foreground=" << GetForegroundWindow()
                  << " ownerEnabled=" << bool(IsWindowEnabled(ownerB)) << std::endl;
        old.terminate("pre-show-cancel");
        require(lookup(next.pid, ownerB).found == visible.found,
                "old cancellation must not close same-title later dialog");
        require(PostMessageW(visible.found, WM_CLOSE, 0, 0) != FALSE, "close exact next dialog");
        require(wait(next.process, 10000), "normal dialog cancellation returns");
        DWORD code = 0;
        require(GetExitCodeProcess(next.process, &code) != FALSE && code == 0, "normal cancel result");
        require(next.shared->phase == 3 &&
                next.shared->showResult == static_cast<LONG>(HRESULT_FROM_WIN32(ERROR_CANCELLED)),
                "normal cancellation Show HRESULT");
        require(IsWindowEnabled(ownerB), "normal cancellation restores owner");
        const bool ownerRestoredForeground = waitForeground(GetCurrentProcessId(), ownerB, 3000);
        if (ownerForeground && dialogForeground)
            require(ownerRestoredForeground, "normal cancellation restores the active owner focus");
        std::cout << "normal-cancel pid=" << next.pid << " exit=" << code
                  << " showResult=0x" << std::hex << HRESULT_FROM_WIN32(ERROR_CANCELLED) << std::dec
                  << " ownerEnabled=" << bool(IsWindowEnabled(ownerB))
                  << " ownerForeground=" << ownerRestoredForeground << std::endl;
    }
    {
        wchar_t temporary[MAX_PATH]{};
        wchar_t selected[MAX_PATH]{};
        require(GetTempPathW(MAX_PATH, temporary) > 0 && GetTempFileNameW(temporary, L"xds", 0, selected) != 0,
                "create isolated selectable fixture file");
        Child accepted(ownerA, 6, selected);
        require(wait(accepted.ready, 10000) && SetEvent(accepted.go) != FALSE &&
                waitVisible(accepted.pid, ownerA, 10000), "normal selection dialog visible");
        const Lookup visible = lookup(accepted.pid, ownerA);
        require(visible.found, "find exact child selection dialog");
        AutomationChild automation(visible.found, accepted.pid);
        if (!wait(accepted.process, 10000)) {
            DWORD automationExit = STILL_ACTIVE;
            GetExitCodeProcess(automation.process.hProcess, &automationExit);
            std::cerr << "selection-timeout childPid=" << accepted.pid << " phase=" << accepted.shared->phase
                      << " automationPid=" << automation.process.dwProcessId
                      << " automationExit=" << automationExit << std::endl;
            accepted.terminate("selection-timeout-cleanup");
        } else {
            DWORD code = 0;
            require(GetExitCodeProcess(accepted.process, &code) != FALSE, "read normal selection result");
            selectionPassed = code == 0 && accepted.shared->phase == 3 &&
                              accepted.shared->showResult == S_OK && accepted.shared->selectedMatched == 1;
            std::cout << "normal-select pid=" << accepted.pid << " exit=" << code
                      << " showResult=0x" << std::hex << accepted.shared->showResult << std::dec
                      << " selectedMatched=" << accepted.shared->selectedMatched << std::endl;
        }
        automationPassed = automation.finishAfterSelection();
        if (!IsWindowEnabled(ownerA) && ownerAInitiallyEnabled) {
            selectionPassed = false;
            EnableWindow(ownerA, TRUE);
            std::cout << "fixture restored owner after incomplete normal selection" << std::endl;
        }
        require(IsWindowEnabled(ownerA), "selection owner recoverable");
        require(DeleteFileW(selected) != FALSE, "remove only the selectable fixture file");
    }
    {
        Child raced(ownerA, 3);
        require(wait(raced.ready, 10000), "raced child ready");
        watch(raced.pid);
        require(SetEvent(raced.go) != FALSE, "release raced Show");
        raced.terminate("raced-cancel");
        if (!IsWindowEnabled(ownerA) && ownerAInitiallyEnabled) {
            EnableWindow(ownerA, TRUE);
            std::cout << "fixture restored originally enabled owner after raced termination" << std::endl;
        }
        require(IsWindowEnabled(ownerA), "raced-cancel owner recoverable");
        // Event observation records what was seen; it cannot prove the absence
        // of every transient frame before process termination.
        std::cout << "raced-show-events=" << showCount.load() << std::endl;
    }
    {
        Child shown(ownerB, 4);
        require(wait(shown.ready, 10000) && SetEvent(shown.go) != FALSE &&
                waitVisible(shown.pid, ownerB, 10000), "shown child ready");
        shown.terminate("visible-cancel");
        if (!IsWindowEnabled(ownerB) && ownerBInitiallyEnabled) {
            EnableWindow(ownerB, TRUE);
            std::cout << "fixture restored originally enabled owner after child termination" << std::endl;
        }
        require(IsWindowEnabled(ownerB), "visible-cancel owner recoverable");
    }
    {
        HWND proxy = makeProxyOwner(ownerA);
        require(proxy && GetWindow(proxy, GW_OWNER) == ownerA && !IsWindowVisible(proxy),
                "hidden request owner has exact menu owner");
        Child proxied(proxy, 7);
        require(wait(proxied.ready, 10000), "proxy child ready");
        const bool rootWasForeground = SetForegroundWindow(ownerA) != FALSE &&
                                       waitForeground(GetCurrentProcessId(), ownerA, 1000);
        EnableWindow(ownerA, FALSE); // Only this fixture's own root; model a modal lease.
        require(!IsWindowEnabled(ownerA), "fixture root modal lease");
        require(SetEvent(proxied.go) != FALSE && waitVisible(proxied.pid, proxy, 10000),
                "dialog visible with hidden per-request owner");
        const bool childForeground = waitForeground(proxied.pid, nullptr, 3000);
        const Lookup visible = lookup(proxied.pid, proxy);
        require(visible.found && GetWindow(visible.found, GW_OWNER) == proxy &&
                GetWindow(proxy, GW_OWNER) == ownerA, "dialog owner chain reaches fixture root");
        std::cout << "proxy-shown pid=" << proxied.pid << " thread=" << visible.thread
                  << " hwnd=" << visible.found << " proxy=" << proxy << " root=" << ownerA
                  << " rootWasForeground=" << rootWasForeground << " childForeground=" << childForeground
                  << " rootEnabled=" << bool(IsWindowEnabled(ownerA)) << std::endl;
        proxied.terminate("proxy-cancel");
        EnableWindow(ownerA, TRUE);
        require(IsWindowEnabled(ownerA), "modal lease restores originally enabled root");
        require(DestroyWindow(proxy) != FALSE, "destroy only completed request owner");
    }
    {
        Child blocked(ownerA, 5);
        require(wait(blocked.ready, 10000), "blocked child ready");
        const auto started = GetTickCount64();
        require(CloseHandle(blocked.job) != FALSE, "close kill-on-close job");
        blocked.job = nullptr;
        require(wait(blocked.process, 5000), "job-close terminates blocked child");
        std::cout << "job-close pid=" << blocked.pid << " elapsedMs=" << GetTickCount64() - started << std::endl;
    }
    probeLauncherExit();
    require(selectionPassed && automationPassed, "normal selection and fixture automation both completed");
    watchedPid = 0;
    UnhookWinEvent(hook);
    DestroyWindow(ownerB);
    DestroyWindow(ownerA);
    CoUninitialize();
    std::cout << "PASS hosted process-boundary fixture; not yet product integration or Windows 11 acceptance" << std::endl;
    return 0;
}
