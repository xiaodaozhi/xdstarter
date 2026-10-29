// tests/platform/start_button_window_event_tests.cpp

#include "platform/start_button_capture_state.h"
#include "platform/windows/windows_start_button_events.h"

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>

#include <array>
#include <iostream>

using namespace xdstarter;
namespace {
int checks = 0, failures = 0;
void expect(bool value, const char* message) {
    ++checks;
    if (!value) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

struct Observer {
    HWND bar = nullptr;
    StartButtonCaptureMode mode = StartButtonCaptureMode::ExperimentalAreaCache;
    quint64 epoch = 10, session = 3, received = 0;
    StartButtonWindowEventJournal journal;
    void observe(StartButtonWindowEvent event) {
        ++received;
        const auto decision = classifyStartButtonWindowEvent(event, bar, mode);
        const auto before = epoch;
        if (decision.action == StartButtonWindowEventAction::Invalidate)
            ++epoch;
        journal.record(event, decision, bar, session, before, epoch);
    }
};
Observer* current = nullptr;
void CALLBACK eventCallback(HWINEVENTHOOK, DWORD event, HWND window, LONG object, LONG child,
                            DWORD thread, DWORD time) {
    if (current)
        current->observe({event, window, object, child, thread, time});
}

bool notify(Observer& observer, DWORD event, HWND window, LONG object, LONG child) {
    const auto before = observer.received;
    NotifyWinEvent(event, window, object, child);
    const auto deadline = GetTickCount64() + 1000;
    while (observer.received == before && GetTickCount64() < deadline) {
        MSG message{};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        Sleep(1);
    }
    return observer.received > before;
}

StartButtonCaptureTarget target(Observer& observer, HWND hit) {
    StartButtonCaptureTarget result;
    result.taskbar = reinterpret_cast<quintptr>(observer.bar);
    result.hitWindow = reinterpret_cast<quintptr>(hit);
    result.processId = GetCurrentProcessId();
    result.taskbarRect = QRect(0, 1040, 1920, 40);
    result.buttonRect = QRect(0, 1040, 48, 40);
    result.qualifiedPoint = QPoint(24, 1060);
    result.observedAt = 1000;
    result.epoch = observer.epoch;
    result.identified = true;
    result.monitor = 7;
    result.monitorRect = QRect(0, 0, 1920, 1080);
    result.dpi = 96;
    result.mode = observer.mode;
    result.areaValidated = true;
    result.sessionGeneration = observer.session;
    return result;
}

StartMouseDecision input(StartButtonCaptureState& state, Observer& observer,
                         const StartButtonCaptureTarget& cached, StartMouseEvent event, quint64 now,
                         bool enabled = true) {
    const bool eligible =
        enabled && cached.epoch == observer.epoch && cached.sessionGeneration == observer.session;
    return state.process(event, cached.qualifiedPoint, now, cached, eligible, false,
                         !enabled ? StartButtonCaptureReason::Disabled
                         : cached.sessionGeneration != observer.session
                             ? StartButtonCaptureReason::SessionSuperseded
                             : StartButtonCaptureReason::EpochChanged,
                         observer.mode);
}
} // namespace

int main(int argc, char* argv[]) {
    QCoreApplication application(argc, argv);
    // Hidden, owned windows and a process-scoped WinEvent subscription. There
    // is no Explorer attachment, low-level input Hook or generated mouse input.
    const HWND bar = CreateWindowExW(0, L"STATIC", L"XDStarter owned event fixture", WS_POPUP, 0, 0,
                                     0, 0, nullptr, nullptr, nullptr, nullptr);
    const HWND child =
        CreateWindowExW(0, L"STATIC", L"", WS_CHILD, 0, 0, 0, 0, bar, nullptr, nullptr, nullptr);
    const HWND outside = CreateWindowExW(0, L"STATIC", L"", WS_POPUP, 0, 0, 0, 0, nullptr, nullptr,
                                         nullptr, nullptr);
    Observer observer;
    observer.bar = bar;
    current = &observer;
    const auto hook =
        SetWinEventHook(EVENT_OBJECT_DESTROY, EVENT_OBJECT_LOCATIONCHANGE, nullptr, eventCallback,
                        GetCurrentProcessId(), GetCurrentThreadId(), WINEVENT_OUTOFCONTEXT);
    expect(bar && child && outside && hook, "owned windows and process-scoped event hook ready");
    if (!bar || !child || !outside || !hook)
        return 1;
    constexpr std::array<DWORD, 5> events{EVENT_OBJECT_DESTROY, EVENT_OBJECT_SHOW,
                                          EVENT_OBJECT_HIDE, EVENT_OBJECT_REORDER,
                                          EVENT_OBJECT_LOCATIONCHANGE};
    constexpr std::array<std::pair<LONG, LONG>, 5> objects{{{OBJID_CLIENT, CHILDID_SELF},
                                                            {OBJID_CLIENT, 7},
                                                            {OBJID_CARET, CHILDID_SELF},
                                                            {42, 9},
                                                            {OBJID_WINDOW, 7}}};
    for (const auto event : events) {
        for (const auto [object, id] : objects) {
            const auto cached = target(observer, child);
            StartButtonCaptureState state;
            expect(input(state, observer, cached, StartMouseEvent::LeftDown, 1050).consume,
                   "fresh resumed area owns down before accessibility notification");
            expect(notify(observer, event, child, object, id),
                   "object notification delivered by OS");
            const auto up = input(state, observer, cached, StartMouseEvent::LeftUp, 1060);
            expect(up.consume && up.toggle,
                   "accessibility-only event does not cancel resumed area's complete gesture");
        }
        // Whole-window changes still invalidate. An owned release is consumed
        // without activation; new presses stay with Windows until requalified.
        for (const HWND window : {bar, child}) {
            const auto cached = target(observer, child);
            StartButtonCaptureState state;
            expect(input(state, observer, cached, StartMouseEvent::LeftDown, 1050).consume,
                   "area down owned before native window invalidation");
            expect(notify(observer, event, window, OBJID_WINDOW, CHILDID_SELF),
                   "whole-window notification delivered by OS");
            const auto up = input(state, observer, cached, StartMouseEvent::LeftUp, 1060);
            expect(up.consume && !up.toggle, "native invalidation drains up without activation");
            expect(!input(state, observer, cached, StartMouseEvent::LeftDown, 1070).consume,
                   "invalidated area cannot accept new down");
            const auto refreshed = target(observer, child);
            expect(input(state, observer, refreshed, StartMouseEvent::LeftDown, 1080).consume &&
                       input(state, observer, refreshed, StartMouseEvent::LeftUp, 1090).toggle,
                   "fresh qualification restores complete gesture");
        }
    }

    auto cached = target(observer, child);
    StartButtonCaptureState state;
    expect(!input(state, observer, cached, StartMouseEvent::LeftDown, 1050, false).consume,
           "pause permits no new press");
    observer.session += 2;
    ++observer.epoch;
    expect(!input(state, observer, cached, StartMouseEvent::LeftDown, 1050).consume,
           "resume cannot reuse preceding session qualification");
    cached = target(observer, child);
    expect(!input(state, observer, cached, StartMouseEvent::LeftDown, 1201).consume,
           "resumed area retains 200ms lease limit");
    expect(input(state, observer, cached, StartMouseEvent::LeftDown, 1050).consume,
           "fresh resumed down accepted");
    ++observer.epoch; // Model the independent Start UIA property/structure invalidation channel.
    const auto uiChanged = input(state, observer, cached, StartMouseEvent::LeftUp, 1060);
    expect(uiChanged.consume && !uiChanged.toggle, "Start UIA invalidation still cancels action");

    observer.mode = StartButtonCaptureMode::Point;
    const auto pointEpoch = observer.epoch;
    for (const auto event : events)
        expect(notify(observer, event, child, OBJID_CLIENT, 7), "point control event delivered");
    expect(observer.epoch == pointEpoch + events.size(), "point invalidation policy unchanged");
    const auto beforeOutside = observer.journal.sequence();
    expect(notify(observer, EVENT_OBJECT_SHOW, outside, OBJID_WINDOW, CHILDID_SELF),
           "unrelated window notification delivered");
    observer.observe({EVENT_OBJECT_SHOW, nullptr, OBJID_WINDOW, CHILDID_SELF});
    observer.observe({EVENT_OBJECT_FOCUS, child, OBJID_WINDOW, CHILDID_SELF});
    expect(observer.journal.sequence() == beforeOutside, "unrelated events never enter journal");
    expect(classifyStartButtonWindowEvent({EVENT_OBJECT_LOCATIONCHANGE, nullptr, OBJID_CURSOR, 0},
                                          nullptr, observer.mode)
                   .action == StartButtonWindowEventAction::Unrelated,
           "two null HWNDs cannot invent a watched taskbar or invalidate a pending query");

    observer.mode = StartButtonCaptureMode::ExperimentalAreaCache;
    for (size_t index = 0; index < StartButtonWindowEventJournal::capacity + 5; ++index)
        expect(notify(observer, EVENT_OBJECT_HIDE, bar, OBJID_CARET, CHILDID_SELF),
               "bounded-journal stress notification delivered");
    const auto evidence = observer.journal.snapshot();
    const auto rows = evidence["recentWindowEvents"].toArray();
    expect(rows.size() == int(StartButtonWindowEventJournal::capacity), "journal stays bounded");
    expect(evidence["windowEventEntriesEvicted"].toInteger() > 0, "overwritten history explicit");
    const auto last = rows.last().toObject();
    expect(last["objectId"].toInteger() == OBJID_CARET &&
               last["childId"].toInteger() == CHILDID_SELF &&
               last["sessionGeneration"].toInteger() == qint64(observer.session) &&
               last["epochBefore"] == last["epochAfter"] &&
               last["action"] == QLatin1String("accessibility-object"),
           "event identity and unchanged epoch preserved");
    expect(UnhookWinEvent(hook), "process-scoped subscription removed");
    current = nullptr;
    DestroyWindow(child);
    DestroyWindow(bar);
    DestroyWindow(outside);
    std::cout
        << checks << " checks, " << failures << " failures\n"
        << "Scope: owned hidden WinEvent fixture; virtual gesture clock; no physical Start trials\n"
        << QJsonDocument(evidence).toJson(QJsonDocument::Compact).constData() << '\n';
    return failures ? 1 : 0;
}
