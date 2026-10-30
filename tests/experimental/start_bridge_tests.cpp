// tests/experimental/start_bridge_tests.cpp

#include "experimental/start_bridge/session_server.h"

#include <functional>
#include <iostream>
#include <memory>
#include <string>

namespace {
using namespace xdstarter::bridge_poc;
int failures = 0;
void expect(bool condition, const char* description) {
    std::cout << (condition ? "PASS " : "FAIL ") << description << '\n';
    std::cout.flush();
    if (!condition)
        ++failures;
}
bool until(const std::function<bool()>& predicate, DWORD timeout = 2000) {
    const auto began = GetTickCount64();
    do {
        if (predicate())
            return true;
        Sleep(5);
    } while (GetTickCount64() - began < timeout);
    return predicate();
}
UINT_PTR count(HWND window, const wchar_t* property) {
    return reinterpret_cast<UINT_PTR>(GetPropW(window, property));
}

class Fixture {
  public:
    explicit Fixture(const std::wstring& path) {
        job_.reset(CreateJobObjectW(nullptr, nullptr));
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limit{};
        limit.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        if (!job_ || !SetInformationJobObject(job_.get(), JobObjectExtendedLimitInformation, &limit,
                                              sizeof(limit)))
            return;
        std::wstring command = L"\"" + path + L"\" --controlled";
        STARTUPINFOW startup{};
        startup.cb = sizeof(startup);
        PROCESS_INFORMATION process{};
        if (!CreateProcessW(path.c_str(), command.data(), nullptr, nullptr, FALSE,
                            CREATE_SUSPENDED | CREATE_NO_WINDOW, nullptr, nullptr, &startup,
                            &process))
            return;
        process_.reset(process.hProcess);
        Handle mainThread(process.hThread);
        identity_.processId = process.dwProcessId;
        identity_.threadId = process.dwThreadId;
        if (!AssignProcessToJobObject(job_.get(), process_.get())) {
            TerminateProcess(process_.get(), 2);
            return;
        }
        ResumeThread(mainThread.get());
        until([this] { return window() != nullptr; });
    }
    ~Fixture() {
        if (process_) {
            if (const auto target = window())
                PostMessageW(target, fixtureCommandMessage,
                             static_cast<WPARAM>(FixtureCommand::Exit), 0);
            if (WaitForSingleObject(process_.get(), 300) == WAIT_TIMEOUT)
                TerminateProcess(process_.get(), 3); // Only this constructor's child.
            WaitForSingleObject(process_.get(), 300);
        }
    }
    HWND window() const {
        std::uint64_t generation = 0;
        return findFixtureWindow(identity_, generation);
    }
    void command(FixtureCommand command) const {
        if (const auto target = window())
            PostMessageW(target, fixtureCommandMessage, static_cast<WPARAM>(command), 0);
    }
    DWORD process() const { return identity_.processId; }
    DWORD thread() const { return identity_.threadId; }

  private:
    Handle job_;
    Handle process_;
    ProcessIdentity identity_{};
};

bool start(SessionServer& server, Fixture& fixture, const std::wstring& dll,
           bool enableEmulatedFrames = false) {
    if (!fixture.window() || !server.start(dll, fixture.process(), fixture.thread(),
                                           ConfigurationFault::None, enableEmulatedFrames)) {
        std::wcerr << L"Session start: " << server.error() << L'\n';
        expect(false, "controlled cross-process bridge starts");
        return false;
    }
    const bool ready = until([&] { return server.observe().ready; }, 3000);
    if (!ready)
        std::wcerr << L"Session readiness: " << server.error() << L" worker="
                   << static_cast<int>(server.observe().worker) << L" reason="
                   << static_cast<int>(server.observe().reason) << L'\n';
    expect(ready, "DLL handshake and bound heartbeat enable the controlled target");
    return ready;
}
void testPointerTransportAndCapability(const std::wstring& dll, const std::wstring& fixturePath) {
    expect(inputKindAllowed(InputKind::Mouse, 0) &&
               !inputKindAllowed(InputKind::EmulatedPointer, 0) &&
               inputKindAllowed(InputKind::EmulatedPointer, emulatedFramesCapability) &&
               !inputKindAllowed(static_cast<InputKind>(999), emulatedFramesCapability),
           "IPC accepts only known input kinds and requires the explicit emulated capability");
    Fixture fixture(fixturePath);
    fixture.command(FixtureCommand::PointerTransportProbe);
    expect(until([&] { return count(fixture.window(), pointerPostsProperty) == 2; }),
           "real WM_POINTER PostMessage probe records both attempted posts");
    const auto succeeded = count(fixture.window(), pointerPostsSucceededProperty);
    const auto error = count(fixture.window(), pointerPostErrorProperty);
    std::cout << "PHYSICAL_POINTER_TRANSPORT posts=2 succeeded=" << succeeded
              << " lastError=" << error
              << "; this probe is not hardware input and never proves interception\n";
    if (succeeded == 0 && error == ERROR_INVALID_MESSAGE) {
        expect(
            count(fixture.window(), nativePointerDownProperty) == 0 &&
                count(fixture.window(), nativePointerUpProperty) == 0,
            "ERROR_INVALID_MESSAGE rejects fake pointer posts; zero delivery is not suppression");
    } else {
        expect(succeeded == 2 && until([&] {
                   return count(fixture.window(), nativePointerDownProperty) == 1 &&
                          count(fixture.window(), nativePointerUpProperty) == 1;
               }),
               "accepted raw pointer posts reach the unhooked fixture intact");
    }
    SessionServer server;
    if (!start(server, fixture, dll, false))
        return;
    fixture.command(FixtureCommand::EmulatedPointerPair);
    expect(until([&] {
               return count(fixture.window(), nativeEmulatedPointerDownProperty) == 1 &&
                      count(fixture.window(), nativeEmulatedPointerUpProperty) == 1;
           }),
           "default capability passes explicit emulated frames through unchanged");
    expect(server.observe().emulatedFramesObserved == 0 && server.observe().toggles == 0 &&
               !server.observe().ownsPress,
           "emulated normalization is disabled by default, independently of real mouse IPC");
    fixture.command(FixtureCommand::MousePair);
    expect(until([&] { return server.observe().toggles == 1; }),
           "default session still intercepts the real Win32 queued mouse pair");
    expect(server.stop(), "default capability session unloads safely");
}
void testGestureAndDrain(const std::wstring& dll, const std::wstring& fixturePath) {
    Fixture fixture(fixturePath);
    fixture.command(FixtureCommand::MousePair);
    expect(until([&] { return count(fixture.window(), nativeMouseUpProperty) == 1; }),
           "unhooked fixture handles native queued mouse pair");
    SessionServer server;
    if (!start(server, fixture, dll, true))
        return;
    expect(modulePresent(fixture.process(), canonicalPath(dll)),
           "actual DLL is mapped in the other fixture process");
    // The first message starts the worker before its shared state necessarily
    // exists. A ready heartbeat does not by itself guarantee that a later hook
    // callback has already been counted. Exercise the actual hook using a
    // harmless message addressed only to the controlled fixture window.
    const auto beforeProbe = server.observe();
    const HWND target = fixture.window();
    const bool posted = target && PostMessageW(target, WM_NULL, 0, 0);
    expect(posted, "controlled fixture receives a non-input hook probe");
    SessionObservation observed;
    const bool callbackObserved = posted && until([&] {
        observed = server.observe();
        return observed.callbacks > beforeProbe.callbacks && observed.connected && observed.ready;
    });
    if (!callbackObserved)
        std::cerr << "Bridge probe: before=" << beforeProbe.callbacks
                  << " after=" << observed.callbacks << " connected=" << observed.connected
                  << " ready=" << observed.ready << " posted=" << posted << '\n';
    expect(callbackObserved, "real GetMessage hook and named pipe are active");
    fixture.command(FixtureCommand::MousePair);
    expect(until([&] { return server.observe().toggles == 1; }),
           "one complete mouse pair dispatches one IPC toggle");
    expect(count(fixture.window(), nativeMouseDownProperty) == 1 &&
               count(fixture.window(), nativeMouseUpProperty) == 1,
           "both owned mouse messages are suppressed before target dispatch");
    fixture.command(FixtureCommand::PeekMousePair);
    expect(until([&] { return server.observe().toggles == 2; }),
           "peeked pair still dispatches exactly once on PM_REMOVE");
    expect(count(fixture.window(), peekUnmodifiedProperty) == 1,
           "PM_NOREMOVE never modifies the queued message");
    fixture.command(FixtureCommand::EmulatedPointerPair);
    expect(until([&] {
               return server.observe().toggles == 3 && server.observe().pointerToggles == 1;
           }),
           "one explicitly opted-in emulated-pointer frame pair dispatches one IPC toggle");
    expect(server.observe().emulatedFramesObserved == 2 &&
               count(fixture.window(), nativeEmulatedPointerDownProperty) == 0 &&
               count(fixture.window(), nativeEmulatedPointerUpProperty) == 0,
           "DLL observed both explicit WM_APP frames before consuming them; no hardware claim");
    fixture.command(FixtureCommand::OtherEmulatedCompatibilityPair);
    expect(until([&] {
               const auto other =
                   reinterpret_cast<HWND>(GetPropW(fixture.window(), otherWindowProperty));
               return other && count(other, nativeEmulatedCompatibilityDownProperty) == 1 &&
                      count(other, nativeEmulatedCompatibilityUpProperty) == 1;
           }),
           "same-thread foreign HWND emulated compatibility pair reaches its fixture unchanged");
    expect(!server.observe().ownsPress && server.observe().toggles == 3,
           "foreign compatibility pair cannot acquire ownership or toggle");
    fixture.command(FixtureCommand::ModifiedMousePair);
    expect(until([&] { return count(fixture.window(), nativeMouseUpProperty) == 2; }),
           "modified mouse pair is passed through intact");
    fixture.command(FixtureCommand::RightPair);
    expect(until([&] { return count(fixture.window(), nativeRightProperty) == 1; }),
           "right mouse button remains native");
    fixture.command(FixtureCommand::MouseDragPair);
    Sleep(100);
    expect(server.observe().toggles == 3 && !server.observe().ownsPress &&
               count(fixture.window(), nativeMouseUpProperty) == 2,
           "drag cancels action but drains the owned release");
    fixture.command(FixtureCommand::MouseDown);
    expect(until([&] { return server.observe().ownsPress; }),
           "mouse down acquires a bounded gesture");
    fixture.command(FixtureCommand::OtherMousePair);
    expect(
        until([&] {
            const auto other =
                reinterpret_cast<HWND>(GetPropW(fixture.window(), otherWindowProperty));
            return other && count(other, nativeMouseDownProperty) == 1 &&
                   count(other, nativeMouseUpProperty) == 1;
        }) &&
            server.observe().ownsPress,
        "foreign same-thread mouse pair remains native and cannot release the owned target down");
    fixture.command(FixtureCommand::KeyboardCancel);
    fixture.command(FixtureCommand::MouseUp);
    expect(until([&] { return !server.observe().ownsPress; }),
           "keyboard cancellation drains the owned up");
    expect(server.observe().toggles == 3, "canceled gesture sends no toggle");
    server.setEnabled(false);
    expect(until([&] { return !server.observe().ready; }), "explicit pause disarms new gestures");
    fixture.command(FixtureCommand::MousePair);
    expect(until([&] { return count(fixture.window(), nativeMouseUpProperty) == 3; }),
           "paused mouse pair remains native");
    fixture.command(FixtureCommand::EmulatedPointerPair);
    expect(until([&] {
               return count(fixture.window(), nativeEmulatedPointerDownProperty) == 1 &&
                      count(fixture.window(), nativeEmulatedPointerUpProperty) == 1;
           }),
           "paused emulated-pointer frame pair reaches the fixture intact");
    fixture.command(FixtureCommand::EmulatedCompatibilityUp);
    expect(
        until([&] { return count(fixture.window(), nativeEmulatedCompatibilityUpProperty) == 1; }),
        "unmatched emulated compatibility up reaches the fixture while disabled");
    server.setEnabled(true);
    expect(until([&] { return server.observe().ready; }), "fresh heartbeat rearms the bridge");
    fixture.command(FixtureCommand::MouseDown);
    expect(until([&] { return server.observe().ownsPress; }),
           "held gesture is observable before disarm");
    expect(!server.stop(100), "safe unload refuses while an owned release is outstanding");
    fixture.command(FixtureCommand::MouseUp);
    expect(until([&] { return !server.observe().ownsPress; }),
           "disabled hook still drains its matching release");
    expect(server.observe().toggles == 3 && count(fixture.window(), nativeMouseUpProperty) == 3,
           "drained release is neither replayed nor dispatched as a toggle");
    expect(server.stop(), "unhook plus GUI-loop barrier confirms remote DLL unload");
    expect(!modulePresent(fixture.process(), canonicalPath(dll)),
           "remote DLL is actually absent after safe unload");
    fixture.command(FixtureCommand::MousePair);
    expect(until([&] { return count(fixture.window(), nativeMouseUpProperty) == 4; }),
           "native target resumes after removal");
}

void testHeartbeatAndDisconnect(const std::wstring& dll, const std::wstring& fixturePath) {
    Fixture fixture(fixturePath);
    SessionServer server;
    if (!start(server, fixture, dll, true))
        return;
    server.pauseHeartbeats(true);
    expect(until([&] { return !server.observe().ready; }),
           "expired heartbeat fails open without waiting on the UI thread");
    fixture.command(FixtureCommand::MousePair);
    expect(until([&] { return count(fixture.window(), nativeMouseUpProperty) == 1; }),
           "expired receiver leaves a new pair native");
    expect(server.observe().toggles == 0, "expired receiver receives no late action");
    server.pauseHeartbeats(false);
    expect(until([&] { return server.observe().ready; }),
           "new authenticated heartbeat restores readiness");
    fixture.command(FixtureCommand::EmulatedPointerDown);
    expect(until([&] { return server.observe().ownsPress; }),
           "explicit emulated-pointer down is owned before disconnect");
    server.disconnectPeer();
    expect(until([&] { return !server.observe().ready; }),
           "broken named pipe disarms the receiver");
    fixture.command(FixtureCommand::EmulatedPointerUp);
    expect(until([&] { return !server.observe().ownsPress; }),
           "disconnect drains emulated-pointer up without dispatch");
    fixture.command(FixtureCommand::MousePair);
    expect(until([&] { return count(fixture.window(), nativeMouseUpProperty) == 2; }),
           "subsequent mouse input remains native after disconnect");
    expect(server.observe().toggles == 0, "disconnect does not cause replay or duplicate delivery");
    expect(server.stop(), "out-of-band disarm and barrier allow safe unload after IPC loss");
}

void testRecreation(const std::wstring& dll, const std::wstring& fixturePath) {
    Fixture fixture(fixturePath);
    SessionServer server;
    if (!start(server, fixture, dll, true))
        return;
    for (unsigned iteration = 0; iteration < 3; ++iteration) {
        fixture.command(FixtureCommand::EmulatedPointerPair);
        expect(until([&] { return server.observe().toggles == iteration * 2 + 1; }),
               "emulated-pointer sequence establishes a target-bound compatibility tail");
        fixture.command(FixtureCommand::EmulatedCompatibilityDown);
        expect(until([&] {
                   return server.observe().ownsPress &&
                          server.observe().promotedDownsOwned == iteration + 1;
               }),
               "explicit emulated compatibility down takes its distinct ownership path");
        const auto oldGeneration = count(fixture.window(), generationProperty);
        fixture.command(FixtureCommand::Recreate);
        expect(until([&] { return count(fixture.window(), generationProperty) > oldGeneration; }),
               "controlled InputSite window is destroyed and recreated");
        expect(until([&] {
                   const auto observation = server.observe();
                   const auto window = fixture.window();
                   return !observation.ownsPress && observation.ready &&
                          observation.bindingVerified &&
                          observation.boundWindow == reinterpret_cast<std::uint64_t>(window) &&
                          observation.boundGeneration == count(window, generationProperty);
               }),
               "destroyed compatibility owner clears even after main pointer gesture completed");
        fixture.command(FixtureCommand::MousePair);
        expect(until([&] { return server.observe().toggles == iteration * 2 + 2; }),
               "new generation is revalidated before accepting a new gesture");
    }
    expect(server.stop(), "recreated window permits safe drain and module removal");
}

void testIdentityAndProtocol(const std::wstring& dll, const std::wstring& fixturePath) {
    ProcessIdentity unsupported{};
    unsupported.versionMs = imageVersionMs;
    unsupported.versionLs = imageVersionLs;
    wcscpy_s(unsupported.path, L"C:\\Windows\\explorer.exe");
    expect(!allowedFixture(unsupported),
           "Explorer InputSite is explicitly unsupported, never an implicit target");
    Fixture fixture(fixturePath);
    SessionServer invalidThread;
    expect(!invalidThread.start(dll, fixture.process(), GetCurrentThreadId()),
           "wrong GUI thread identity is rejected before hook installation");
    SessionServer server;
    if (!start(server, fixture, dll))
        return;
    server.sendInvalidNonce(true);
    expect(until([&] {
               return !server.observe().ready && server.observe().reason == Reason::InvalidProtocol;
           }),
           "incorrect session nonce is rejected by the actual DLL pipe client");
    fixture.command(FixtureCommand::MousePair);
    expect(until([&] { return count(fixture.window(), nativeMouseUpProperty) == 1; }),
           "protocol rejection preserves native input");
    expect(server.observe().toggles == 0, "invalid peer cannot dispatch a toggle");
    expect(server.stop(), "invalid protocol still permits explicit safe cleanup");
}

void testInvalidBinding(const std::wstring& dll, const std::wstring& fixturePath) {
    Fixture fixture(fixturePath);
    SessionServer server;
    if (!start(server, fixture, dll))
        return;
    const HWND original = fixture.window();
    PostMessageW(original, fixtureCommandMessage,
                 static_cast<WPARAM>(FixtureCommand::DuplicateWindow), 0);
    expect(until([&] {
               return !server.observe().ready && server.observe().reason == Reason::WindowChanged;
           }),
           "authenticated heartbeat with ambiguous/missing window binding disarms the old target");
    PostMessageW(original, fixtureCommandMessage, static_cast<WPARAM>(FixtureCommand::MousePair),
                 0);
    expect(until([&] { return count(original, nativeMouseUpProperty) == 1; }),
           "invalid binding cannot retain old target input ownership");
    PostMessageW(original, fixtureCommandMessage,
                 static_cast<WPARAM>(FixtureCommand::CloseDuplicateWindow), 0);
    expect(until([&] { return server.observe().ready; }),
           "a unique valid generation can restore readiness");
    expect(server.stop(), "ambiguous binding session unloads safely after recovery");
}

void testInitializationFailures(const std::wstring& dll, const std::wstring& fixturePath) {
    Fixture fixture(fixturePath);
    for (const auto fault :
         {ConfigurationFault::MissingReleaseEvent, ConfigurationFault::InvalidImageVersion,
          ConfigurationFault::ArchitectureMismatch}) {
        SessionServer server;
        expect(server.start(dll, fixture.process(), fixture.thread(), fault),
               "controlled DLL validation fault is delivered through a real installed thread hook");
        expect(until([&] { return server.observe().reason == Reason::IdentityMismatch; }),
               "DLL rejects missing release handle or mismatched image metadata independently");
        expect(!server.observe().ready && !server.observe().ownsPress,
               "failed initialization publishes no usable callback view");
        const auto before = count(fixture.window(), nativeMouseUpProperty);
        fixture.command(FixtureCommand::MousePair);
        expect(until([&] { return count(fixture.window(), nativeMouseUpProperty) == before + 1; }),
               "failed initialization leaves subsequent native messages intact");
        expect(server.stop(), "failed initialization permits unhook and confirmed module removal");
    }
}

void testDestructorCustodian(const std::wstring& dll, const std::wstring& fixturePath) {
    Fixture fixture(fixturePath);
    auto server = std::make_unique<SessionServer>();
    if (!start(*server, fixture, dll))
        return;
    fixture.command(FixtureCommand::MouseDown);
    expect(until([&] { return server->observe().ownsPress; }),
           "destructor case starts with an actually swallowed down");
    server.reset();
    expect(retainedDrainCount() == 1 && modulePresent(fixture.process(), canonicalPath(dll)),
           "normal destructor transfers held input to a live drain custodian, without unhooking");
    fixture.command(FixtureCommand::MouseUp);
    expect(until([&] {
               return retainedDrainCount() == 0 &&
                      !modulePresent(fixture.process(), canonicalPath(dll));
           }),
           "custodian drains up before unhook/barrier/remote release");
    expect(count(fixture.window(), nativeMouseUpProperty) == 0,
           "destructor never exposes an isolated native up");
    fixture.command(FixtureCommand::MousePair);
    expect(until([&] {
               return count(fixture.window(), nativeMouseDownProperty) == 1 &&
                      count(fixture.window(), nativeMouseUpProperty) == 1;
           }),
           "normal native pairs resume after custodian unload without terminating the fixture");
}

void testCancellationRecoveryAndSingleUse(const std::wstring& dll,
                                          const std::wstring& fixturePath) {
    Fixture fixture(fixturePath);
    SessionServer server;
    if (!start(server, fixture, dll, true))
        return;
    fixture.command(FixtureCommand::MouseDown);
    expect(until([&] { return server.observe().ownsPress; }),
           "recovery case owns its initial down");
    fixture.command(FixtureCommand::KeyboardCancel);
    fixture.command(FixtureCommand::MouseUp);
    expect(until([&] { return !server.observe().ownsPress; }),
           "canceled sequence finishes before recovery");
    fixture.command(FixtureCommand::MousePair);
    expect(until([&] { return server.observe().toggles == 1; }),
           "new accepted mouse down resets prior cancellation state");
    fixture.command(FixtureCommand::EmulatedPointerPair);
    expect(until([&] { return server.observe().toggles == 2; }),
           "new emulated-pointer frame sequence starts with clean cancellation state");
    fixture.command(FixtureCommand::EmulatedCompatibilityDown);
    expect(until([&] {
               return server.observe().ownsPress && server.observe().promotedDownsOwned == 1;
           }),
           "first matching compatibility pair consumes the pointer tail permit");
    fixture.command(FixtureCommand::EmulatedCompatibilityUp);
    expect(until([&] { return !server.observe().ownsPress; }),
           "first compatibility pair drains without another toggle");
    fixture.command(FixtureCommand::EmulatedCompatibilityDown);
    fixture.command(FixtureCommand::EmulatedCompatibilityUp);
    expect(until([&] {
               return count(fixture.window(), nativeEmulatedCompatibilityDownProperty) == 1 &&
                      count(fixture.window(), nativeEmulatedCompatibilityUpProperty) == 1;
           }),
           "same pointer tail permit cannot consume a second compatibility pair");
    expect(server.observe().promotedDownsOwned == 1 && server.observe().toggles == 2,
           "compatibility permit is single-use and never duplicates the action");
    fixture.command(FixtureCommand::EmulatedPointerDown);
    expect(until([&] { return server.observe().ownsPress; }),
           "next pointer may own its own distinct permit");
    fixture.command(FixtureCommand::EmulatedCompatibilityDown);
    expect(until([&] { return server.observe().promotedDownsOwned == 2; }),
           "compatibility down before pointer up consumes that sequence's permit");
    fixture.command(FixtureCommand::EmulatedCompatibilityUp);
    fixture.command(FixtureCommand::EmulatedPointerUp);
    expect(until([&] { return server.observe().toggles == 3 && !server.observe().ownsPress; }),
           "pointer completion dispatches once after early compatibility drain");
    fixture.command(FixtureCommand::EmulatedCompatibilityDown);
    fixture.command(FixtureCommand::EmulatedCompatibilityUp);
    expect(until([&] {
               return count(fixture.window(), nativeEmulatedCompatibilityDownProperty) == 2 &&
                      count(fixture.window(), nativeEmulatedCompatibilityUpProperty) == 2;
           }),
           "pointer up does not regrant an already-used compatibility permit");
    expect(server.stop(), "single-use and cancellation recovery cases unload safely");
}
void testEmulatedPointerCaptureTermination(const std::wstring& dll,
                                           const std::wstring& fixturePath) {
    Fixture fixture(fixturePath);
    SessionServer server;
    if (!start(server, fixture, dll, true))
        return;
    fixture.command(FixtureCommand::EmulatedPointerDown);
    expect(until([&] { return server.observe().ownsPress; }),
           "capture termination test owns an explicit emulated-pointer frame");
    fixture.command(FixtureCommand::OtherEmulatedPointerUp);
    expect(until([&] {
               const auto other =
                   reinterpret_cast<HWND>(GetPropW(fixture.window(), otherWindowProperty));
               return other && count(other, nativeEmulatedPointerUpProperty) == 1;
           }) &&
               server.observe().ownsPress,
           "foreign same-ID emulated up cannot consume or release the original target owner");
    fixture.command(FixtureCommand::OtherEmulatedPointerCaptureLost);
    expect(until([&] {
               const auto other =
                   reinterpret_cast<HWND>(GetPropW(fixture.window(), otherWindowProperty));
               return other && count(other, nativeEmulatedCaptureLostProperty) == 1;
           }) &&
               server.observe().ownsPress,
           "capture-loss frame for another HWND cannot terminate the target pointer");
    fixture.command(FixtureCommand::WrongIdEmulatedPointerCaptureLost);
    expect(until([&] { return count(fixture.window(), nativeEmulatedCaptureLostProperty) == 1; }) &&
               server.observe().ownsPress,
           "capture-loss frame for another pointer ID cannot terminate owned pointer");
    fixture.command(FixtureCommand::EmulatedPointerCaptureLost);
    expect(until([&] {
               return !server.observe().ownsPress &&
                      count(fixture.window(), nativeEmulatedCaptureLostProperty) == 2;
           }),
           "matching capture-loss frame terminates ownership without waiting for an up");
    expect(server.observe().toggles == 0,
           "terminal pointer cancellation dispatches no action and grants no compatibility permit");
    fixture.command(FixtureCommand::EmulatedCompatibilityDown);
    fixture.command(FixtureCommand::EmulatedCompatibilityUp);
    expect(until([&] {
               return count(fixture.window(), nativeEmulatedCompatibilityDownProperty) == 1 &&
                      count(fixture.window(), nativeEmulatedCompatibilityUpProperty) == 1;
           }),
           "canceled pointer cannot authorize an emulated compatibility pair");
    fixture.command(FixtureCommand::EmulatedPointerPair);
    expect(until([&] { return server.observe().toggles == 1 && !server.observe().ownsPress; }),
           "new emulated-pointer sequence recovers after terminal capture cancellation");
    fixture.command(FixtureCommand::EmulatedPointerDown);
    expect(until([&] { return server.observe().ownsPress; }),
           "emulated canceled-flag case owns a new pointer down");
    fixture.command(FixtureCommand::EmulatedPointerCanceled);
    expect(until([&] { return !server.observe().ownsPress; }) && server.observe().toggles == 1,
           "CANCELED emulated frame terminates ownership without an artificial up or action");
    expect(server.stop(),
           "terminal capture cancellation allows safe unload without an artificial up");
}
} // namespace

int wmain(int argc, wchar_t** argv) {
    if (argc != 3)
        return 64;
    Handle completed(CreateEventW(nullptr, TRUE, FALSE, nullptr));
    Handle watchdog(CreateThread(
        nullptr, 0,
        [](void* event) -> DWORD {
            if (WaitForSingleObject(event, 32000) == WAIT_TIMEOUT) {
                std::cerr << "FAIL controlled bridge watchdog expired; terminating only this "
                             "test/job tree\n";
                TerminateProcess(GetCurrentProcess(), 124);
            }
            return 0;
        },
        completed.get(), 0, nullptr));
    testPointerTransportAndCapability(argv[1], argv[2]);
    testGestureAndDrain(argv[1], argv[2]);
    testHeartbeatAndDisconnect(argv[1], argv[2]);
    testRecreation(argv[1], argv[2]);
    testIdentityAndProtocol(argv[1], argv[2]);
    testInvalidBinding(argv[1], argv[2]);
    testInitializationFailures(argv[1], argv[2]);
    testDestructorCustodian(argv[1], argv[2]);
    testCancellationRecoveryAndSingleUse(argv[1], argv[2]);
    testEmulatedPointerCaptureTermination(argv[1], argv[2]);
    SetEvent(completed.get());
    WaitForSingleObject(watchdog.get(), 500);
    std::cout << "Controlled Win32 DLL/queue/named-pipe failures: " << failures
              << ". Explorer and physical pointer coverage remain unverified.\n";
    return failures ? 1 : 0;
}
