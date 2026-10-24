// src/experimental/explorer_route/route_controller.cpp

// Parse COM interfaces before Qt undefines the legacy Windows interface macro.
#include <objbase.h>
#include <uiautomation.h>

#include "experimental/start_bridge/win_support.h"
#include "platform/windows/windows_desktop_platform.h"
#include "route_protocol.h"

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QSet>
#include <QUuid>

#include <bcrypt.h>
#include <softpub.h>
#include <wincrypt.h>
#include <wintrust.h>

#include <algorithm>
#include <iostream>
#include <memory>
#include <vector>

using namespace xdstarter::route_probe;
namespace support = xdstarter::bridge_poc;
namespace {
QString hexHandle(HWND window) { return QString::number(reinterpret_cast<quintptr>(window), 16); }
QString fileHash(const QString& path) {
    QFile file(path);
    QCryptographicHash hash(QCryptographicHash::Sha256);
    return file.open(QIODevice::ReadOnly) && hash.addData(&file)
               ? QString::fromLatin1(hash.result().toHex())
               : QString();
}
QString windowClass(HWND window) {
    wchar_t name[256]{};
    GetClassNameW(window, name, 256);
    return QString::fromWCharArray(name);
}
QString buildIdentity() {
    using Version = LONG(WINAPI*)(OSVERSIONINFOW*);
    const auto query =
        reinterpret_cast<Version>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlGetVersion"));
    OSVERSIONINFOW version{};
    version.dwOSVersionInfoSize = sizeof(version);
    DWORD revision = 0, size = sizeof(revision);
    if (!query || query(&version) ||
        RegGetValueW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion", L"UBR",
                     RRF_RT_REG_DWORD, nullptr, &revision, &size) != ERROR_SUCCESS)
        return {};
    return QStringLiteral("%1.%2").arg(version.dwBuildNumber).arg(revision);
}
QJsonObject signature(const std::wstring& path) {
    WINTRUST_FILE_INFO file{};
    file.cbStruct = sizeof(file);
    file.pcwszFilePath = path.c_str();
    WINTRUST_DATA trust{};
    trust.cbStruct = sizeof(trust);
    trust.dwUIChoice = WTD_UI_NONE;
    trust.dwUnionChoice = WTD_CHOICE_FILE;
    trust.pFile = &file;
    trust.dwStateAction = WTD_STATEACTION_VERIFY;
    trust.dwProvFlags = WTD_CACHE_ONLY_URL_RETRIEVAL;
    GUID action = WINTRUST_ACTION_GENERIC_VERIFY_V2;
    const auto status =
        WinVerifyTrust(reinterpret_cast<HWND>(INVALID_HANDLE_VALUE), &action, &trust);
    QString subject;
    if (status == ERROR_SUCCESS) {
        const auto* provider = WTHelperProvDataFromStateData(trust.hWVTStateData);
        const auto* signer = provider ? WTHelperGetProvSignerFromChain(
                                            const_cast<CRYPT_PROVIDER_DATA*>(provider), 0, FALSE, 0)
                                      : nullptr;
        if (signer && signer->csCertChain && signer->pasCertChain) {
            wchar_t name[512]{};
            CertGetNameStringW(signer->pasCertChain[0].pCert, CERT_NAME_SIMPLE_DISPLAY_TYPE, 0,
                               nullptr, name, 512);
            subject = QString::fromWCharArray(name);
        }
    }
    trust.dwStateAction = WTD_STATEACTION_CLOSE;
    WinVerifyTrust(reinterpret_cast<HWND>(INVALID_HANDLE_VALUE), &action, &trust);
    const bool microsoft = subject == QLatin1String("Microsoft Windows") ||
                           subject == QLatin1String("Microsoft Corporation") ||
                           subject == QLatin1String("Microsoft Windows Publisher");
    return {{"status", qint64(status)},
            {"subject", subject},
            {"trustedMicrosoft", status == ERROR_SUCCESS && microsoft},
            {"networkRetrieval", false}};
}
QJsonObject identityJson(const support::ProcessIdentity& identity) {
    const QByteArray hash(reinterpret_cast<const char*>(identity.digest.data()), 32);
    return {{"processId", int(identity.processId)},
            {"created", qint64(identity.creationTime)},
            {"machine", identity.machine},
            {"path", QString::fromWCharArray(identity.path)},
            {"sha256", QString::fromLatin1(hash.toHex())},
            {"fileVersionMs", qint64(identity.versionMs)},
            {"fileVersionLs", qint64(identity.versionLs)}};
}
struct Inventory {
    HWND bar = nullptr;
    DWORD process = 0;
    QSet<DWORD> threads;
    QJsonArray windows;
    bool truncated = false;
    void add(HWND window) {
        if (windows.size() >= 512) {
            truncated = true;
            return;
        }
        DWORD pid = 0;
        const auto thread = GetWindowThreadProcessId(window, &pid);
        wchar_t module[512]{};
        GetWindowModuleFileNameW(window, module, 512);
        windows.append(QJsonObject{{"window", hexHandle(window)},
                                   {"class", windowClass(window)},
                                   {"processId", int(pid)},
                                   {"threadId", int(thread)},
                                   {"modulePath", QString::fromWCharArray(module)}});
        if (pid == process && thread)
            threads.insert(thread);
    }
};
struct Qualification {
    xdstarter::StartButtonCaptureTarget target;
    QJsonObject observation;
};
Qualification qualification() {
    Qualification result;
    const auto apartment = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    IUIAutomation* automation = nullptr;
    IUIAutomation2* bounded = nullptr;
    auto status = SUCCEEDED(apartment)
                      ? CoCreateInstance(CLSID_CUIAutomation8, nullptr, CLSCTX_INPROC_SERVER,
                                         IID_PPV_ARGS(&automation))
                      : apartment;
    if (SUCCEEDED(status))
        status = automation->QueryInterface(IID_PPV_ARGS(&bounded));
    if (SUCCEEDED(status))
        status = bounded->put_ConnectionTimeout(100);
    if (SUCCEEDED(status))
        status = bounded->put_TransactionTimeout(100);
    if (SUCCEEDED(status)) {
        xdstarter::WindowsDesktopPlatform platform;
        xdstarter::StartButtonQueryObservation observation;
        result.target = platform.queryStartButtonCaptureTarget(
            automation, xdstarter::StartButtonCaptureMode::ExperimentalAreaCache, 1, &observation);
        result.observation = {{"identified", result.target.identified},
                              {"stage", observation.stage},
                              {"details", observation.details},
                              {"durationMs", qint64(observation.durationMs)}};
    } else {
        result.observation = {{"identified", false}, {"setupHresult", qint64(status)}};
    }
    if (bounded)
        bounded->Release();
    if (automation)
        automation->Release();
    if (SUCCEEDED(apartment))
        CoUninitialize();
    return result;
}
struct Attachment {
    support::Handle mapping;
    Shared* shared = nullptr;
    HHOOK hook = nullptr;
    HHOOK procedureHook = nullptr;
    DWORD thread = 0;
    LONG64 last = 0;
    ~Attachment() {
        if (shared)
            InterlockedExchange(&shared->armed, 0);
        if (hook)
            UnhookWindowsHookEx(hook);
        if (procedureHook)
            UnhookWindowsHookEx(procedureHook);
        if (shared)
            UnmapViewOfFile(shared);
    }
};
bool writeRecord(QFile& output, const QJsonObject& record, bool final = false) {
    const auto bytes = QJsonDocument(record).toJson(QJsonDocument::Compact) + '\n';
    return output.pos() + bytes.size() <= 8 * 1024 * 1024 - (final ? 0 : 4096) &&
           output.write(bytes) == bytes.size() && output.flush();
}
} // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    QCommandLineParser parser;
    parser.addHelpOption();
    parser.addOption({"inspect", "Only inspect identity, qualification and candidate threads."});
    parser.addOption({"observe", "Explicitly observe the current verified Explorer taskbar."});
    parser.addOption({"self-test", "Use only the hidden, owned route fixture."});
    parser.addOption({"expected-build", "Exact build.UBR from inspection.", "identity"});
    parser.addOption(
        {"expected-explorer-sha256", "Exact on-disk Explorer SHA256 from inspection.", "sha"});
    parser.addOption({"output", "New local NDJSON evidence file.", "path"});
    parser.addOption({"seconds", "Observation duration, 1..120.", "seconds", "30"});
    parser.process(app);
    const bool fixture = parser.isSet("self-test");
    if (int(parser.isSet("inspect")) + int(parser.isSet("observe")) + int(fixture) != 1)
        parser.showHelp(64);
    bool durationValid = false;
    const auto seconds = parser.value("seconds").toInt(&durationValid);
    if (!durationValid || seconds < 1 || seconds > 120) {
        std::cerr << "Duration must be 1..120 seconds\n";
        return 64;
    }
    QProcess owned;
    HWND bar = nullptr;
    Qualification qualified;
    if (fixture) {
        owned.setProgram(QDir(app.applicationDirPath()).filePath("xdstarter_route_fixture.exe"));
        owned.setArguments({"--controlled"});
        owned.setCreateProcessArgumentsModifier(
            [](QProcess::CreateProcessArguments* args) { args->flags |= CREATE_NO_WINDOW; });
        owned.start();
        if (!owned.waitForStarted(3000))
            return 1;
        for (int attempt = 0; attempt < 100 && !bar; ++attempt) {
            HWND candidate = nullptr;
            while ((candidate = FindWindowExW(nullptr, candidate, fixtureClass, nullptr))) {
                DWORD process = 0;
                GetWindowThreadProcessId(candidate, &process);
                if (process == DWORD(owned.processId()))
                    bar = candidate;
            }
            Sleep(10);
        }
    } else {
        qualified = qualification();
        bar = qualified.target.identified ? reinterpret_cast<HWND>(qualified.target.taskbar)
                                          : FindWindowW(L"Shell_TrayWnd", nullptr);
    }
    Inventory inventory;
    inventory.bar = bar;
    const auto rootThread = GetWindowThreadProcessId(bar, &inventory.process);
    support::ProcessIdentity process;
    if (!bar || !rootThread ||
        !support::readProcessIdentity(inventory.process, rootThread, process)) {
        std::cerr << "Cannot bind a live x64 taskbar/fixture process identity\n";
        return 1;
    }
    inventory.add(bar);
    EnumChildWindows(
        bar,
        [](HWND window, LPARAM parameter) -> BOOL {
            reinterpret_cast<Inventory*>(parameter)->add(window);
            return !reinterpret_cast<Inventory*>(parameter)->truncated;
        },
        reinterpret_cast<LPARAM>(&inventory));
    const auto build = buildIdentity();
    auto evidence = identityJson(process);
    evidence.insert("kind", "route-run");
    evidence.insert("controllerSha256", fileHash(app.applicationFilePath()));
    evidence.insert("runId", QUuid::createUuid().toString(QUuid::WithoutBraces));
    evidence.insert("windowsBuild", build);
    evidence.insert("fixture", fixture);
    evidence.insert("qualification", qualified.observation);
    evidence.insert("windows", inventory.windows);
    evidence.insert("inventoryTruncated", inventory.truncated);
    evidence.insert("policy", "observe only; MSG unchanged; no input suppression or detour");
    const auto signer = fixture ? QJsonObject{} : signature(process.path);
    evidence.insert("signature", signer);
    if (parser.isSet("inspect")) {
        std::cout << QJsonDocument(evidence).toJson().constData();
        return 0;
    }
    wchar_t windows[512]{};
    GetWindowsDirectoryW(windows, 512);
    const auto expectedImage = support::canonicalPath(std::wstring(windows) + L"\\explorer.exe");
    if (!fixture && (!qualified.target.identified || !signer["trustedMicrosoft"].toBool() ||
                     _wcsicmp(process.path, expectedImage.c_str()) || build.isEmpty() ||
                     parser.value("expected-build") != build ||
                     parser.value("expected-explorer-sha256") != evidence["sha256"].toString())) {
        std::cerr << "Explorer observation refused: exact identity/signature/unoccluded "
                     "qualification required\n";
        std::cout << QJsonDocument(evidence).toJson().constData();
        return 2;
    }
    if (inventory.truncated || inventory.threads.isEmpty() ||
        inventory.threads.size() > maxThreads || process.machine != IMAGE_FILE_MACHINE_AMD64)
        return 2;
    const auto dll = support::canonicalPath(
        QDir(app.applicationDirPath()).filePath("xdstarter_explorer_route.dll").toStdWString());
    const auto dllHash = fileHash(QString::fromStdWString(dll));
    if (dll.empty() || dllHash.isEmpty() ||
        support::modulePresence(process.processId, dll) != support::ModulePresence::Absent)
        return 2;
    const auto outputPath =
        fixture && !parser.isSet("output")
            ? QDir::current().filePath(QStringLiteral("route-self-test-%1.ndjson")
                                           .arg(QUuid::createUuid().toString(QUuid::WithoutBraces)))
            : parser.value("output");
    QFile output(outputPath);
    if (output.fileName().isEmpty() || !output.open(QIODevice::WriteOnly | QIODevice::NewOnly)) {
        std::cerr << "A new output file is required\n";
        return 64;
    }
    support::LocalSecurity security;
    const auto module = LoadLibraryW(dll.c_str());
    const auto callback =
        module ? reinterpret_cast<HOOKPROC>(GetProcAddress(module, "XDStarterObserveQueue"))
               : nullptr;
    const auto procedureCallback =
        module
            ? reinterpret_cast<HOOKPROC>(GetProcAddress(module, "XDStarterObserveWindowProcedure"))
            : nullptr;
    if (!security.get() || !callback || !procedureCallback) {
        if (module)
            FreeLibrary(module);
        return 1;
    }
    const auto began = GetTickCount64();
    const auto duration = fixture ? 1200 : seconds * 1000;
    evidence.insert("startedAt", qint64(began));
    evidence.insert("dllPath", QString::fromStdWString(dll));
    evidence.insert("observerSha256", dllHash);
    writeRecord(output, evidence);
    std::vector<std::unique_ptr<Attachment>> attachments;
    bool okay = true;
    for (const auto thread : inventory.threads) {
        auto attachment = std::make_unique<Attachment>();
        attachment->thread = thread;
        wchar_t name[160]{};
        mappingName(name, process.processId, process.creationTime, thread);
        attachment->mapping.reset(CreateFileMappingW(INVALID_HANDLE_VALUE, security.get(),
                                                     PAGE_READWRITE, 0, sizeof(Shared), name));
        if (!attachment->mapping || GetLastError() == ERROR_ALREADY_EXISTS) {
            okay = false;
            break;
        }
        attachment->shared = static_cast<Shared*>(MapViewOfFile(
            attachment->mapping.get(), FILE_MAP_READ | FILE_MAP_WRITE, 0, 0, sizeof(Shared)));
        if (!attachment->shared) {
            okay = false;
            break;
        }
        auto* shared = new (attachment->shared) Shared;
        shared->process = process.processId;
        shared->processCreated = process.creationTime;
        shared->thread = thread;
        shared->controller = GetCurrentProcessId();
        shared->fixture = fixture;
        shared->deadline = began + duration;
        shared->taskbar = reinterpret_cast<std::uint64_t>(bar);
        okay = BCryptGenRandom(nullptr, reinterpret_cast<PUCHAR>(shared->nonce.data()),
                               sizeof(shared->nonce), BCRYPT_USE_SYSTEM_PREFERRED_RNG) >= 0;
        InterlockedExchange64(&shared->heartbeat, GetTickCount64());
        InterlockedExchange(&shared->armed, okay);
        support::ProcessIdentity current;
        if (!okay || !support::readProcessIdentity(process.processId, rootThread, current) ||
            !support::identitiesEqual(process, current)) {
            okay = false;
            break;
        }
        attachment->hook = SetWindowsHookExW(WH_GETMESSAGE, callback, module, thread);
        attachment->procedureHook =
            SetWindowsHookExW(WH_CALLWNDPROC, procedureCallback, module, thread);
        if (!attachment->hook || !attachment->procedureHook) {
            okay = false;
            break;
        }
        PostThreadMessageW(thread, WM_NULL, 0, 0);
        attachments.push_back(std::move(attachment));
    }
    bool pairSent = false;
    int recordedDown = 0, recordedUp = 0, procedureDown = 0, procedureUp = 0;
    while (okay && GetTickCount64() - began < ULONGLONG(duration)) {
        for (auto& attachment : attachments) {
            auto* shared = attachment->shared;
            InterlockedExchange64(&shared->heartbeat, GetTickCount64());
            if (fixture && !pairSent && read(&shared->initialized)) {
                PostMessageW(bar, fixtureTest, 0, 0);
                const auto child = reinterpret_cast<HWND>(GetPropW(bar, L"RouteChild"));
                const auto other = reinterpret_cast<HWND>(GetPropW(bar, L"RouteOther"));
                DWORD_PTR ignored = 0;
                const auto send = [&](HWND window, UINT message, WPARAM flags) {
                    return SendMessageTimeoutW(window, message, flags, MAKELPARAM(5, 6),
                                               SMTO_ABORTIFHUNG, 250, &ignored) != 0;
                };
                okay = send(child, WM_LBUTTONDOWN, MK_LBUTTON) && send(child, WM_LBUTTONUP, 0) &&
                       send(other, WM_LBUTTONDOWN, MK_LBUTTON) && send(other, WM_LBUTTONUP, 0);
                pairSent = true;
            }
            const auto latest = read(&shared->sequence);
            if (latest > attachment->last + LONG64(capacity)) {
                okay =
                    writeRecord(output, {{"kind", "route-gap"},
                                         {"threadId", int(attachment->thread)},
                                         {"lost", qint64(latest - attachment->last - capacity)}});
                attachment->last = latest - capacity;
            }
            for (auto sequence = attachment->last + 1; sequence <= latest; ++sequence) {
                auto& entry = shared->entries[(sequence - 1) % capacity];
                if (read(&entry.sequence) != sequence)
                    break;
                const auto message = read(&entry.message);
                const auto source = read(&entry.source);
                const bool pointer = message >= WM_POINTERUPDATE && message <= WM_POINTERHWHEEL;
                QJsonObject event{
                    {"kind", "route-input"},
                    {"sequence", qint64(sequence)},
                    {"threadId", int(attachment->thread)},
                    {"at", qint64(read(&entry.at))},
                    {"window", QString::number(quintptr(read(&entry.window)), 16)},
                    {"message", QString::number(message, 16)},
                    {"pointerIdBits",
                     pointer ? QJsonValue(int(read(&entry.pointerId))) : QJsonValue()},
                    {"pointerFlagBits",
                     pointer ? QJsonValue(int(read(&entry.pointerFlags))) : QJsonValue()},
                    {"source", source == 1 ? "queue" : "window-procedure"},
                    {"queueTime32", source == 1 ? QJsonValue(qint64(DWORD(read(&entry.queueTime))))
                                                : QJsonValue()},
                    {"x", source == 1 ? QJsonValue(int(read(&entry.x))) : QJsonValue()},
                    {"y", source == 1 ? QJsonValue(int(read(&entry.y))) : QJsonValue()},
                    {"promoted",
                     source == 1 ? QJsonValue(bool(read(&entry.promoted))) : QJsonValue()}};
                if (read(&entry.sequence) != sequence)
                    break;
                recordedDown += source == 1 && message == WM_LBUTTONDOWN;
                recordedUp += source == 1 && message == WM_LBUTTONUP;
                procedureDown += source == 2 && message == WM_LBUTTONDOWN;
                procedureUp += source == 2 && message == WM_LBUTTONUP;
                okay = writeRecord(output, event) && okay;
                attachment->last = sequence;
            }
        }
        Sleep(20);
    }
    bool unhooked = true, initialized = !attachments.empty();
    for (auto& attachment : attachments) {
        InterlockedExchange(&attachment->shared->armed, 0);
        initialized = initialized && read(&attachment->shared->initialized);
        okay =
            writeRecord(output,
                        {{"kind", "route-thread"},
                         {"threadId", int(attachment->thread)},
                         {"initialized", bool(read(&attachment->shared->initialized))},
                         {"callbacks", int(read(&attachment->shared->callbacks))},
                         {"peekCount", int(read(&attachment->shared->peeks))},
                         {"reentrantDropped", int(read(&attachment->shared->reentrantDropped))},
                         {"initDurationMs", qint64(read(&attachment->shared->initDurationMs))}}) &&
            okay;
        if (UnhookWindowsHookEx(attachment->hook))
            attachment->hook = nullptr;
        else
            unhooked = false;
        if (UnhookWindowsHookEx(attachment->procedureHook))
            attachment->procedureHook = nullptr;
        else
            unhooked = false;
        PostThreadMessageW(attachment->thread, WM_NULL, 0, 0);
    }
    const auto cleanup = GetTickCount64();
    auto presence = support::modulePresence(process.processId, dll);
    while (unhooked && presence != support::ModulePresence::Absent &&
           GetTickCount64() - cleanup < 2000) {
        for (const auto& attachment : attachments)
            PostThreadMessageW(attachment->thread, WM_NULL, 0, 0);
        Sleep(25);
        presence = support::modulePresence(process.processId, dll);
    }
    bool nativePair = true;
    if (fixture) {
        const auto child = reinterpret_cast<HWND>(GetPropW(bar, L"RouteChild"));
        const auto other = reinterpret_cast<HWND>(GetPropW(bar, L"RouteOther"));
        nativePair = GetPropW(child, downProperty) == reinterpret_cast<HANDLE>(2) &&
                     GetPropW(child, upProperty) == reinterpret_cast<HANDLE>(2) &&
                     GetPropW(other, downProperty) == reinterpret_cast<HANDLE>(1) &&
                     GetPropW(other, upProperty) == reinterpret_cast<HANDLE>(1) &&
                     GetPropW(bar, L"RoutePeekPreserved") == reinterpret_cast<HANDLE>(1);
        nativePair = nativePair && recordedDown == 1 && recordedUp == 1 && procedureDown >= 1 &&
                     procedureDown <= 2 && procedureUp >= 1 && procedureUp <= 2;
    }
    const auto absent = presence == support::ModulePresence::Absent;
    const QJsonObject stopped{
        {"kind", "route-stop"},
        {"at", qint64(GetTickCount64())},
        {"hooksRemoved", unhooked},
        {"allThreadsInitialized", initialized},
        {"moduleAbsenceConfirmed", absent},
        {"nativeFixturePairUnmodified", fixture ? QJsonValue(nativePair) : QJsonValue()},
        {"physicalInputVerified", false},
        {"queueDown", recordedDown},
        {"queueUp", recordedUp},
        {"procedureDown", procedureDown},
        {"procedureUp", procedureUp},
        {"messagesModifiedByProbe", false}};
    okay = writeRecord(output, stopped, true) && okay;
    FreeLibrary(module);
    if (fixture) {
        PostMessageW(bar, WM_CLOSE, 0, 0);
        if (!owned.waitForFinished(2000)) {
            owned.kill();
            owned.waitForFinished(1000);
            okay = false;
        }
    }
    std::cout << QJsonDocument(stopped).toJson(QJsonDocument::Compact).constData() << '\n';
    return okay && unhooked && absent && initialized && nativePair ? 0 : 1;
}
