// src/platform/windows/windows_start_button_events.cpp

#include "windows_start_button_events.h"

#include <QJsonArray>

#include <algorithm>

namespace xdstarter {
StartButtonWindowEventDecision classifyStartButtonWindowEvent(const StartButtonWindowEvent& event,
                                                              HWND taskbar,
                                                              StartButtonCaptureMode mode) {
    if (!taskbar || !event.window ||
        (event.event != EVENT_OBJECT_DESTROY && event.event != EVENT_OBJECT_SHOW &&
         event.event != EVENT_OBJECT_HIDE && event.event != EVENT_OBJECT_REORDER &&
         event.event != EVENT_OBJECT_LOCATIONCHANGE))
        return {};
    const HWND root = GetAncestor(event.window, GA_ROOT);
    if (event.window != taskbar && root != taskbar)
        return {};
    // A shared taskbar HWND also reports accessible children appearing,
    // disappearing and moving. Those are not changes to that native window.
    // Area mode subscribes separately to the qualified Start UIA element and
    // its subtree; keep the native channel scoped to whole-window events.
    if (mode == StartButtonCaptureMode::ExperimentalAreaCache &&
        (event.objectId != OBJID_WINDOW || event.childId != CHILDID_SELF))
        return {StartButtonWindowEventAction::AccessibilityObject, root};
    return {StartButtonWindowEventAction::Invalidate, root};
}

void StartButtonWindowEventJournal::record(const StartButtonWindowEvent& event,
                                           StartButtonWindowEventDecision decision, HWND taskbar,
                                           quint64 session, quint64 before, quint64 after) {
    if (decision.action == StartButtonWindowEventAction::Unrelated)
        return;
    if (decision.action == StartButtonWindowEventAction::Invalidate)
        ++invalidations_;
    else
        ++accessibilityObjects_;
    const auto sequence = ++sequence_;
    auto& slot = samples_[(sequence - 1) % capacity];
    slot.sequence.store(0, std::memory_order_release);
    slot.at = GetTickCount64();
    slot.session = session;
    slot.before = before;
    slot.after = after;
    slot.window = reinterpret_cast<quintptr>(event.window);
    slot.root = reinterpret_cast<quintptr>(decision.root);
    slot.taskbar = reinterpret_cast<quintptr>(taskbar);
    slot.event = event.event;
    slot.object = event.objectId;
    slot.child = event.childId;
    slot.thread = event.threadId;
    slot.time = event.eventTime;
    slot.action = static_cast<int>(decision.action);
    slot.sequence.store(sequence, std::memory_order_release);
}

QJsonObject StartButtonWindowEventJournal::snapshot() const {
    const auto last = sequence_.load();
    QJsonArray events;
    const auto first = last > capacity ? last - capacity + 1 : 1;
    for (auto sequence = first; sequence <= last; ++sequence) {
        const auto& slot = samples_[(sequence - 1) % capacity];
        if (slot.sequence.load(std::memory_order_acquire) != sequence)
            continue;
        const QJsonObject row{
            {QStringLiteral("sequence"), qint64(sequence)},
            {QStringLiteral("at"), qint64(slot.at.load())},
            {QStringLiteral("sessionGeneration"), qint64(slot.session.load())},
            {QStringLiteral("epochBefore"), qint64(slot.before.load())},
            {QStringLiteral("epochAfter"), qint64(slot.after.load())},
            {QStringLiteral("window"), QString::number(slot.window.load(), 16)},
            {QStringLiteral("root"), QString::number(slot.root.load(), 16)},
            {QStringLiteral("taskbar"), QString::number(slot.taskbar.load(), 16)},
            {QStringLiteral("eventCode"), qint64(slot.event.load())},
            {QStringLiteral("objectId"), qint64(slot.object.load())},
            {QStringLiteral("childId"), qint64(slot.child.load())},
            {QStringLiteral("eventThread"), qint64(slot.thread.load())},
            // WinEvent's 32-bit producer tick is not a GetTickCount64 timestamp.
            {QStringLiteral("eventTime32"), qint64(slot.time.load())},
            {QStringLiteral("action"),
             slot.action.load() == static_cast<int>(StartButtonWindowEventAction::Invalidate)
                 ? QStringLiteral("invalidate")
                 : QStringLiteral("accessibility-object")}};
        if (slot.sequence.load(std::memory_order_acquire) == sequence)
            events.append(row);
    }
    return {
        {QStringLiteral("windowEventSequence"), qint64(last)},
        {QStringLiteral("windowEventInvalidations"), qint64(invalidations_.load())},
        {QStringLiteral("windowEventAccessibilityObjects"), qint64(accessibilityObjects_.load())},
        {QStringLiteral("windowEventHistoryCapacity"), int(capacity)},
        {QStringLiteral("windowEventEntriesEvicted"),
         qint64(last > capacity ? last - capacity : 0)},
        {QStringLiteral("recentWindowEvents"), events}};
}
} // namespace xdstarter
