// src/platform/windows/windows_start_button_capture.cpp

#include "platform/start_button_capture.h"
#include "platform/start_button_capture_state.h"
#include "windows_desktop_platform.h"
#include "windows_start_button_events.h"

#include <QDebug>
#include <QJsonArray>

#include <objbase.h>
#include <uiautomation.h>
#include <windows.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <limits>
#include <mutex>
#include <thread>
#include <vector>

namespace xdstarter {
namespace {
constexpr UINT applyMessage = WM_APP + 91;
constexpr UINT toggleMessage = WM_APP + 92;

enum class EpochSource {
    UiaStructure,
    UiaBounds,
    UiaOffscreen,
    UiaAutomationId,
    UiaControlType,
    UiaProcess,
    NativeDestroy,
    NativeShow,
    NativeHide,
    NativeReorder,
    NativeLocation,
    Subscription,
    Pause,
    Stop,
    Desktop,
    MessageLoop,
    Dispatch,
    Count
};
constexpr auto epochSourceCount = static_cast<size_t>(EpochSource::Count);
constexpr auto reasonCount = static_cast<size_t>(StartButtonCaptureReason::Count);
constexpr std::array<const char*, epochSourceCount> epochNames{"uia-structure",
                                                               "uia-bounds",
                                                               "uia-offscreen",
                                                               "uia-automation-id",
                                                               "uia-control-type",
                                                               "uia-process",
                                                               "native-destroy",
                                                               "native-show",
                                                               "native-hide",
                                                               "native-reorder",
                                                               "native-location",
                                                               "subscription",
                                                               "pause",
                                                               "stop",
                                                               "desktop",
                                                               "message-loop",
                                                               "dispatch"};

struct EpochTracker {
    std::atomic<quint64> value{0};
    std::array<std::atomic<quint64>, epochSourceCount> sources{};
    quint64 invalidate(EpochSource source) {
        sources[static_cast<size_t>(source)].fetch_add(1, std::memory_order_relaxed);
        return value.fetch_add(1) + 1;
    }
};

// One writer (the hook thread), atomic fields, fixed capacity. Export retries
// no input operation and drops a sample if the slot changed while being read.
struct InputSample {
    std::atomic<quint64> sequence{0}, at{0}, epoch{0}, snapshotEpoch{0};
    std::atomic<quint64> windowEventSequence{0};
    std::atomic<quint64> session{0}, originSession{0}, gestureId{0}, pressedAt{0};
    std::atomic<qint64> age{-1}, dx{0}, dy{0};
    std::atomic<LONG> x{0}, y{0};
    std::atomic<int> event{0}, reason{0}, flags{0};
};

// A local, non-swapped-button Default input desktop matching this thread.
// This does not detect lock-screen overlay windows or establish hook liveness.
bool inputDesktopAvailable() {
    if (GetSystemMetrics(SM_REMOTESESSION) || GetSystemMetrics(SM_SWAPBUTTON))
        return false;
    const HDESK desktop = OpenInputDesktop(0, FALSE, DESKTOP_READOBJECTS);
    wchar_t name[128]{};
    wchar_t threadName[128]{};
    DWORD bytes = 0;
    const bool valid = desktop &&
                       GetUserObjectInformationW(desktop, UOI_NAME, name, sizeof(name), &bytes) &&
                       GetUserObjectInformationW(GetThreadDesktop(GetCurrentThreadId()), UOI_NAME,
                                                 threadName, sizeof(threadName), &bytes) &&
                       _wcsicmp(name, L"Default") == 0 && _wcsicmp(name, threadName) == 0;
    if (desktop)
        CloseDesktop(desktop);
    return valid;
}

// Provider event callbacks invalidate only. Neither these nor input callbacks
// call UIA, wait for the GUI or perform file/process-image queries.
class TargetEvents final : public IUIAutomationPropertyChangedEventHandler,
                           public IUIAutomationStructureChangedEventHandler {
  public:
    explicit TargetEvents(std::shared_ptr<EpochTracker> epoch) : epoch_(std::move(epoch)) {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** value) override {
        if (!value)
            return E_POINTER;
        *value = nullptr;
        if (iid == IID_IUnknown || iid == IID_IUIAutomationPropertyChangedEventHandler)
            *value = static_cast<IUIAutomationPropertyChangedEventHandler*>(this);
        else if (iid == IID_IUIAutomationStructureChangedEventHandler)
            *value = static_cast<IUIAutomationStructureChangedEventHandler*>(this);
        else
            return E_NOINTERFACE;
        AddRef();
        return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++references_; }
    ULONG STDMETHODCALLTYPE Release() override {
        const ULONG count = --references_;
        if (!count)
            delete this;
        return count;
    }
    HRESULT STDMETHODCALLTYPE HandlePropertyChangedEvent(IUIAutomationElement*, PROPERTYID property,
                                                         VARIANT) override {
        auto source = EpochSource::UiaStructure;
        switch (property) {
        case UIA_BoundingRectanglePropertyId:
            source = EpochSource::UiaBounds;
            break;
        case UIA_IsOffscreenPropertyId:
            source = EpochSource::UiaOffscreen;
            break;
        case UIA_AutomationIdPropertyId:
            source = EpochSource::UiaAutomationId;
            break;
        case UIA_ControlTypePropertyId:
            source = EpochSource::UiaControlType;
            break;
        case UIA_ProcessIdPropertyId:
            source = EpochSource::UiaProcess;
            break;
        }
        epoch_->invalidate(source);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE HandleStructureChangedEvent(IUIAutomationElement*,
                                                          StructureChangeType,
                                                          SAFEARRAY*) override {
        epoch_->invalidate(EpochSource::UiaStructure);
        return S_OK;
    }

  private:
    std::atomic<ULONG> references_{1};
    std::shared_ptr<EpochTracker> epoch_;
};

class WindowsStartButtonCapture final : public StartButtonCapture {
  public:
    explicit WindowsStartButtonCapture(StartButtonCaptureOptions options) : options_(options) {}
    ~WindowsStartButtonCapture() override { stop(); }
    bool isSupported() const override { return !GetSystemMetrics(SM_REMOTESESSION); }
    bool isActive() const override { return active_; }

    QJsonObject diagnosticSnapshot() const override {
        const quint64 now = GetTickCount64();
        StartButtonCaptureTarget snapshot;
        {
            std::lock_guard lock(snapshotMutex_);
            snapshot = snapshot_;
        }
        POINT cursor{};
        const bool cursorRead = GetPhysicalCursorPos(&cursor) != FALSE;
        const bool lease = active_ && requested_ && snapshot.sessionGeneration == generation_ &&
                           snapshot.epoch == epoch_ &&
                           StartButtonCaptureState::qualifies(snapshot, snapshot.qualifiedPoint,
                                                              now, options_.mode);
        const bool eligible =
            lease && !stopping_ && !inputModifiersDown() && cursorRead &&
            liveTarget(snapshot, cursor) == StartButtonCaptureReason::None &&
            StartButtonCaptureState::qualifies(snapshot, {cursor.x, cursor.y}, now, options_.mode);
        QJsonObject down, canceled, up, queryReasons, epochs;
        for (size_t index = 0; index < reasonCount; ++index) {
            const auto key = QString::fromLatin1(
                startButtonCaptureReasonName(static_cast<StartButtonCaptureReason>(index)));
            down.insert(key, static_cast<qint64>(downReasons_[index].load()));
            canceled.insert(key, static_cast<qint64>(upCanceledReasons_[index].load()));
            up.insert(key, static_cast<qint64>(upReasons_[index].load()));
            queryReasons.insert(key, static_cast<qint64>(queryReasons_[index].load()));
        }
        for (size_t index = 0; index < epochSourceCount; ++index)
            epochs.insert(QString::fromLatin1(epochNames[index]),
                          static_cast<qint64>(epochs_->sources[index].load()));
        QJsonArray samples;
        std::vector<std::pair<quint64, QJsonObject>> ordered;
        for (const auto& slot : samples_) {
            const auto sequence = slot.sequence.load(std::memory_order_acquire);
            if (!sequence)
                continue;
            const auto reason = static_cast<StartButtonCaptureReason>(slot.reason.load());
            const int flags = slot.flags.load();
            QJsonObject sample{
                {QStringLiteral("sequence"), static_cast<qint64>(sequence)},
                {QStringLiteral("at"), static_cast<qint64>(slot.at.load())},
                {QStringLiteral("sessionGeneration"), qint64(slot.session.load())},
                {QStringLiteral("originSessionGeneration"), qint64(slot.originSession.load())},
                {QStringLiteral("gestureId"), qint64(slot.gestureId.load())},
                {QStringLiteral("pressedAt"), qint64(slot.pressedAt.load())},
                {QStringLiteral("event"),
                 slot.event.load() == static_cast<int>(StartMouseEvent::LeftDown)
                     ? QStringLiteral("left-down")
                     : QStringLiteral("left-up")},
                {QStringLiteral("reason"),
                 QString::fromLatin1(startButtonCaptureReasonName(reason))},
                {QStringLiteral("snapshotAgeMs"), static_cast<qint64>(slot.age.load())},
                {QStringLiteral("pointDeltaX"), slot.dx.load()},
                {QStringLiteral("pointDeltaY"), slot.dy.load()},
                {QStringLiteral("pointX"), qint64(slot.x.load())},
                {QStringLiteral("pointY"), qint64(slot.y.load())},
                {QStringLiteral("epoch"), static_cast<qint64>(slot.epoch.load())},
                {QStringLiteral("snapshotEpoch"), static_cast<qint64>(slot.snapshotEpoch.load())},
                {QStringLiteral("nativeWindowEventSequence"), qint64(slot.windowEventSequence.load())},
                {QStringLiteral("consumed"), bool(flags & 1)},
                {QStringLiteral("toggle"), bool(flags & 2)},
                {QStringLiteral("nativeDispatchQueued"), bool(flags & 16)},
                {QStringLiteral("injected"), bool(flags & 4)}};
            sample.insert(QStringLiteral("pointDifferenceKnown"), bool(flags & 8));
            if (slot.sequence.load(std::memory_order_acquire) == sequence)
                ordered.emplace_back(sequence, std::move(sample));
        }
        std::sort(ordered.begin(), ordered.end(),
                  [](const auto& first, const auto& second) { return first.first < second.first; });
        for (const auto& sample : ordered)
            samples.append(sample.second);
        QJsonObject query, host;
        QJsonArray hostHistory;
        {
            std::lock_guard lock(queryDiagnosticsMutex_);
            query = lastQuery_;
        }
        {
            std::lock_guard lock(hostMutex_);
            host = lastHost_;
            for (const auto& event : hostHistory_)
                hostHistory.append(event);
        }
        QJsonArray queryLatency;
        for (size_t index = 0; index < queryLatency_.size(); ++index)
            queryLatency.append(QJsonObject{
                {QStringLiteral("upperBoundMs"),
                 index < queryLatencyLimits_.size() ? QJsonValue(qint64(queryLatencyLimits_[index]))
                                                    : QJsonValue(QJsonValue::Null)},
                {QStringLiteral("count"), qint64(queryLatency_[index].load())}});
        const auto last = lastInputAt_.load();
        QJsonObject result{
            {QStringLiteral("schemaVersion"), 2},
            {QStringLiteral("clock"), QStringLiteral("GetTickCount64 milliseconds")},
            {QStringLiteral("exportedAt"), qint64(now)},
            {QStringLiteral("platformImplemented"), true},
            {QStringLiteral("supported"), isSupported()},
            {QStringLiteral("inputDesktopAvailable"), inputDesktopAvailable()},
            {QStringLiteral("inputDesktopScope"),
             QStringLiteral("local non-swapped-button session; Default input desktop matches "
                            "exporter thread; does not prove absence of lock-screen overlays or "
                            "hook liveness")},
            {QStringLiteral("mode"), options_.mode == StartButtonCaptureMode::Point
                                         ? QStringLiteral("point")
                                         : QStringLiteral("experimental-area-cache")},
            {QStringLiteral("requested"), requested_.load()},
            {QStringLiteral("hookInstalled"), hookInstalled_.load()},
            {QStringLiteral("sessionActive"), active_.load()},
            {QStringLiteral("qualificationLeaseAvailable"), lease},
            {QStringLiteral("eligibleAtCurrentCursor"), eligible},
            {QStringLiteral("snapshotIdentified"), snapshot.identified},
            {QStringLiteral("snapshotEpoch"), static_cast<qint64>(snapshot.epoch)},
            {QStringLiteral("generation"), static_cast<qint64>(generation_.load())},
            {QStringLiteral("snapshotGeneration"), static_cast<qint64>(snapshot.sessionGeneration)},
            {QStringLiteral("snapshotAgeMs"),
             snapshot.identified && now >= snapshot.observedAt
                 ? QJsonValue(static_cast<qint64>(now - snapshot.observedAt))
                 : QJsonValue(QJsonValue::Null)},
            {QStringLiteral("snapshotPointDeltaX"),
             snapshot.identified && cursorRead
                 ? QJsonValue(qint64(cursor.x) - snapshot.qualifiedPoint.x())
                 : QJsonValue(QJsonValue::Null)},
            {QStringLiteral("snapshotPointDeltaY"),
             snapshot.identified && cursorRead
                 ? QJsonValue(qint64(cursor.y) - snapshot.qualifiedPoint.y())
                 : QJsonValue(QJsonValue::Null)},
            {QStringLiteral("hookLiveness"), QStringLiteral("unknown")},
            {QStringLiteral("inputObserved"), last != 0},
            {QStringLiteral("lastInputAgeMs"), last && now >= last
                                                   ? QJsonValue(static_cast<qint64>(now - last))
                                                   : QJsonValue(QJsonValue::Null)},
            {QStringLiteral("inputCallbacks"), static_cast<qint64>(inputCallbacks_.load())},
            {QStringLiteral("physicalInputCallbacks"),
             static_cast<qint64>(physicalInputCallbacks_.load())},
            {QStringLiteral("inputScope"),
             QStringLiteral("all mouse callbacks, not an inference of intended Start clicks")},
            {QStringLiteral("inputObservedThisSession"),
             last && lastInputGeneration_ == generation_},
            {QStringLiteral("keyboardCallbacks"), static_cast<qint64>(keyboardCallbacks_.load())},
            {QStringLiteral("consumedDown"), static_cast<qint64>(consumedDown_.load())},
            {QStringLiteral("consumedUp"), static_cast<qint64>(consumedUp_.load())},
            {QStringLiteral("toggleQueued"), static_cast<qint64>(toggleQueued_.load())},
            {QStringLiteral("downDecisions"), down},
            {QStringLiteral("upCanceled"), canceled},
            {QStringLiteral("upDecisions"), up},
            {QStringLiteral("queryDecisions"), queryReasons},
            {QStringLiteral("queryCount"), static_cast<qint64>(queryCount_.load())},
            {QStringLiteral("queryDurationHistogram"), queryLatency},
            {QStringLiteral("queryDurationTotalMs"), qint64(queryDurationTotal_.load())},
            {QStringLiteral("queryDurationMaxMs"), qint64(queryDurationMax_.load())},
            {QStringLiteral("queryCacheHits"), static_cast<qint64>(queryCacheHits_.load())},
            {QStringLiteral("dispatchFailures"), static_cast<qint64>(dispatchFailures_.load())},
            {QStringLiteral("epoch"), static_cast<qint64>(epoch_.load())},
            {QStringLiteral("epochSources"), epochs},
            {QStringLiteral("subscriptionRootHresult"), subscriptionRoot_.load()},
            {QStringLiteral("subscriptionGeneration"),
             static_cast<qint64>(subscriptionGeneration_.load())},
            {QStringLiteral("subscriptionStructureHresult"), subscriptionStructure_.load()},
            {QStringLiteral("subscriptionPropertiesHresult"), subscriptionProperties_.load()},
            {QStringLiteral("subscriptionDurationMs"),
             static_cast<qint64>(subscriptionDuration_.load())},
            {QStringLiteral("lastQuery"), query},
            {QStringLiteral("recentInput"), samples},
            {QStringLiteral("inputSequence"), qint64(sampleSequence_.load())},
            {QStringLiteral("inputHistoryCapacity"), int(samples_.size())},
            {QStringLiteral("inputEntriesEvicted"),
             qint64(sampleSequence_.load() > samples_.size()
                        ? sampleSequence_.load() - samples_.size()
                        : 0)},
            {QStringLiteral("nativeDispatchDropped"), qint64(nativeDispatchDropped_.load())},
            {QStringLiteral("hostObservationCount"), static_cast<qint64>(hostObservations_.load())},
            {QStringLiteral("hostForegroundConfirmedCount"),
             static_cast<qint64>(hostForeground_.load())},
            {QStringLiteral("lastHostObservation"), host},
            {QStringLiteral("recentHost"), hostHistory},
            {QStringLiteral("hostHistoryCapacity"), 512},
            {QStringLiteral("hostEntriesEvicted"),
             qint64(hostObservations_.load() > 512 ? hostObservations_.load() - 512 : 0)}};
        const auto windowEvents = windowEvents_.snapshot();
        for (auto it = windowEvents.begin(); it != windowEvents.end(); ++it)
            result.insert(it.key(), it.value());
        return result;
    }

    void recordHostObservation(const StartButtonHostObservation& observation) override {
        if (observation.nativeMeasured && observation.visible && !observation.minimized &&
            observation.foreground)
            ++hostForeground_;
        std::lock_guard lock(hostMutex_);
        lastHost_ = startButtonHostObservationJson(observation, GetTickCount64());
        lastHost_.insert(QStringLiteral("sequence"), qint64(++hostObservations_));
        if (hostHistory_.size() == 512)
            hostHistory_.removeFirst();
        hostHistory_.append(lastHost_);
    }

    void setEnabled(bool enabled) override {
        if (!enabled) {
            quint64 generation = 0;
            {
                std::lock_guard lock(sessionMutex_);
                requested_ = false;
                generation = endEventSession();
                generation_ = generation;
                active_ = false;
                epochs_->invalidate(EpochSource::Pause);
            }
            wake();
            queueStatusChanged(generation, false, QStringLiteral("开始按钮接管已暂停。"), {});
            return;
        }
        if (needsRestart_.exchange(false))
            stop(); // A failed timer/UIA/event registration needs fresh threads.
        {
            std::lock_guard lock(sessionMutex_);
            if (requested_.exchange(true))
                return;
            generation_ = beginEventSession();
        }
        if (hookThread_.joinable() && hookExited_)
            hookThread_.join();
        if (!hookThread_.joinable()) {
            stopping_ = false;
            hookExited_ = false;
            try {
                if (!queryThread_.joinable())
                    queryThread_ = std::thread([this] { queryLoop(); });
                hookThread_ = std::thread([this] { hookLoop(); });
            } catch (const std::system_error&) {
                stopping_ = true;
                requested_ = false;
                if (queryThread_.joinable())
                    queryThread_.join();
                queueStatusChanged(generation_, false, QStringLiteral("开始按钮接管线程启动失败。"),
                                   QStringLiteral("start-capture-thread-failed"));
            }
        } else {
            wake();
        }
    }

    void stop() override {
        {
            std::lock_guard lock(sessionMutex_);
            generation_ = endEventSession();
            requested_ = false;
            active_ = false;
            stopping_ = true;
            epochs_->invalidate(EpochSource::Stop);
        }
        wake();
        if (queryThread_.joinable())
            queryThread_.join();
        if (hookThread_.joinable())
            hookThread_.join();
    }

  private:
    // Control plane only; never held across COM or a join, and never acquired
    // by the native input callback.
    bool querySessionCurrent(quint64 generation) const {
        return generation == generation_ && requested_ && active_ && !stopping_ &&
               isCurrentEventSession(generation);
    }

    static bool inputModifiersDown() {
        return (GetAsyncKeyState(VK_LWIN) & 0x8000) || (GetAsyncKeyState(VK_RWIN) & 0x8000) ||
               (GetAsyncKeyState(VK_CONTROL) & 0x8000) || (GetAsyncKeyState(VK_MENU) & 0x8000) ||
               (GetAsyncKeyState(VK_SHIFT) & 0x8000) || (GetAsyncKeyState(VK_RBUTTON) & 0x8000) ||
               (GetAsyncKeyState(VK_MBUTTON) & 0x8000);
    }

    void wake() {
        if (const DWORD thread = threadId_.load())
            PostThreadMessageW(thread, applyMessage, 0, 0);
    }

    StartButtonCaptureReason liveTarget(const StartButtonCaptureTarget& target, POINT point) const {
        if (target.epoch != epoch_)
            return StartButtonCaptureReason::EpochChanged;
        const HWND bar = reinterpret_cast<HWND>(target.taskbar);
        const HWND hit = WindowFromPoint(point);
        RECT bounds{};
        DWORD pid = 0;
        GetWindowThreadProcessId(bar, &pid);
        const HMONITOR monitor = MonitorFromWindow(bar, MONITOR_DEFAULTTONULL);
        MONITORINFO info{};
        info.cbSize = sizeof(info);
        const bool valid =
            IsWindowVisible(bar) && pid == target.processId && monitor &&
            reinterpret_cast<quintptr>(monitor) == target.monitor &&
            GetDpiForWindow(bar) == target.dpi && GetMonitorInfoW(monitor, &info) &&
            QRect(info.rcMonitor.left, info.rcMonitor.top,
                  info.rcMonitor.right - info.rcMonitor.left,
                  info.rcMonitor.bottom - info.rcMonitor.top) == target.monitorRect &&
            GetWindowRect(bar, &bounds) &&
            QRect(bounds.left, bounds.top, bounds.right - bounds.left,
                  bounds.bottom - bounds.top) == target.taskbarRect &&
            GetAncestor(hit, GA_ROOT) == bar && reinterpret_cast<quintptr>(hit) == target.hitWindow;
        return valid ? StartButtonCaptureReason::None
                     : StartButtonCaptureReason::NativeTargetChanged;
    }

    void recordInput(StartMouseEvent event, const StartMouseDecision& decision,
                     const StartButtonCaptureTarget& target, POINT point, quint64 now,
                     bool injected, const StartButtonGestureIdentity& identity, quint64 session,
                     bool queued) {
        if (event != StartMouseEvent::LeftDown && event != StartMouseEvent::LeftUp)
            return;
        const auto index = static_cast<size_t>(decision.reason);
        if (event == StartMouseEvent::LeftDown) {
            downReasons_[index].fetch_add(1, std::memory_order_relaxed);
            if (decision.consume)
                ++consumedDown_;
        } else {
            upReasons_[index].fetch_add(1, std::memory_order_relaxed);
            if (decision.consume)
                ++consumedUp_;
            if (decision.consume && !decision.toggle)
                upCanceledReasons_[index].fetch_add(1, std::memory_order_relaxed);
        }
        const auto sequence = ++sampleSequence_;
        auto& slot = samples_[(sequence - 1) % samples_.size()];
        slot.sequence.store(0, std::memory_order_release);
        slot.at.store(now);
        slot.session.store(session);
        slot.originSession.store(identity.sessionGeneration);
        slot.gestureId.store(identity.id);
        slot.pressedAt.store(identity.pressedAt);
        slot.age.store(target.identified && now >= target.observedAt
                           ? static_cast<qint64>(now - target.observedAt)
                           : -1);
        slot.dx.store(target.identified ? qint64(point.x) - target.qualifiedPoint.x() : 0);
        slot.dy.store(target.identified ? qint64(point.y) - target.qualifiedPoint.y() : 0);
        slot.x.store(point.x);
        slot.y.store(point.y);
        slot.epoch.store(epoch_);
        slot.snapshotEpoch.store(target.epoch);
        slot.windowEventSequence.store(windowEvents_.sequence());
        slot.event.store(static_cast<int>(event));
        slot.reason.store(static_cast<int>(decision.reason));
        slot.flags.store((decision.consume ? 1 : 0) | (decision.toggle ? 2 : 0) |
                         (injected ? 4 : 0) | (target.identified ? 8 : 0) | (queued ? 16 : 0));
        slot.sequence.store(sequence, std::memory_order_release);
    }

    static LRESULT CALLBACK mouseCallback(int code, WPARAM message, LPARAM parameter) {
        auto* self = current_;
        if (code != HC_ACTION || !self)
            return CallNextHookEx(nullptr, code, message, parameter);
        const auto& native = *reinterpret_cast<const MSLLHOOKSTRUCT*>(parameter);
        const auto now = GetTickCount64();
        self->lastInputAt_ = now;
        self->lastInputGeneration_ = self->generation_.load();
        ++self->inputCallbacks_;
        const bool injected = (native.flags & LLMHF_INJECTED) != 0;
        if (!injected)
            ++self->physicalInputCallbacks_;
        const auto generation = self->generation_.load();
        StartMouseEvent event = StartMouseEvent::OtherButton;
        switch (message) {
        case WM_MOUSEMOVE:
            event = StartMouseEvent::Move;
            break;
        case WM_LBUTTONDOWN:
            event = StartMouseEvent::LeftDown;
            break;
        case WM_LBUTTONUP:
            event = StartMouseEvent::LeftUp;
            break;
        default:
            break;
        }
        StartButtonCaptureTarget target;
        bool snapshotRead = false;
        {
            std::unique_lock lock(self->snapshotMutex_, std::try_to_lock);
            if (lock.owns_lock()) {
                target = self->snapshot_;
                snapshotRead = true;
            }
        }
        const bool modifiers = inputModifiersDown();
        using Reason = StartButtonCaptureReason;
        Reason failure = Reason::None;
        if (self->stopping_)
            failure = Reason::Stopping;
        else if (!self->active_ || !self->requested_)
            failure = Reason::Disabled;
        else if (modifiers)
            failure = Reason::Modifiers;
        else if (!snapshotRead)
            failure = Reason::SnapshotBusy;
        else if (!target.identified)
            failure = Reason::Unidentified;
        else if (target.sessionGeneration != generation)
            failure = Reason::SessionSuperseded;
        else
            failure = self->liveTarget(target, native.pt);
        const auto identity = self->gestureTracker_.observe(event, generation, now, injected,
                                                            self->gesture_.ownsPress());
        const auto decision =
            self->gesture_.process(event, {native.pt.x, native.pt.y}, now, target,
                                   failure == Reason::None, injected, failure, self->options_.mode);
        bool queued = false;
        if (decision.toggle) {
            if (PostThreadMessageW(GetCurrentThreadId(), toggleMessage,
                                   static_cast<WPARAM>(generation),
                                   static_cast<LPARAM>(self->sampleSequence_.load() + 1))) {
                ++self->toggleQueued_;
                queued = true;
            } else {
                self->epochs_->invalidate(EpochSource::Dispatch);
                self->dispatchFailures_.fetch_add(1, std::memory_order_relaxed);
                self->requested_ = false;
                self->active_ = false;
                self->wake();
            }
        }
        self->recordInput(event, decision, target, native.pt, now, injected, identity, generation,
                          queued);
        return decision.consume ? 1 : CallNextHookEx(nullptr, code, message, parameter);
    }

    static LRESULT CALLBACK keyboardCallback(int code, WPARAM message, LPARAM parameter) {
        if (code == HC_ACTION && current_) {
            ++current_->keyboardCallbacks_;
            current_->gesture_.cancel(StartButtonCaptureReason::KeyboardInput);
        }
        return CallNextHookEx(nullptr, code, message, parameter);
    }

    static void CALLBACK windowEvent(HWINEVENTHOOK, DWORD event, HWND window, LONG object,
                                     LONG child, DWORD thread, DWORD time) {
        auto* self = current_;
        if (!self)
            return;
        const HWND bar = reinterpret_cast<HWND>(self->observedTaskbar_.load());
        const StartButtonWindowEvent observed{event, window, object, child, thread, time};
        const auto decision = classifyStartButtonWindowEvent(observed, bar, self->options_.mode);
        if (decision.action == StartButtonWindowEventAction::Unrelated)
            return;
        auto before = self->epoch_.load();
        auto after = before;
        if (decision.action == StartButtonWindowEventAction::Invalidate) {
            auto source = EpochSource::NativeLocation;
            if (event == EVENT_OBJECT_DESTROY)
                source = EpochSource::NativeDestroy;
            else if (event == EVENT_OBJECT_SHOW)
                source = EpochSource::NativeShow;
            else if (event == EVENT_OBJECT_HIDE)
                source = EpochSource::NativeHide;
            else if (event == EVENT_OBJECT_REORDER)
                source = EpochSource::NativeReorder;
            after = self->epochs_->invalidate(source);
            before = after - 1;
        }
        self->windowEvents_.record(observed, decision, bar, self->generation_.load(), before, after);
    }

    void queryLoop() {
        const HRESULT initialized = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        IUIAutomation* automation = nullptr;
        IUIAutomationElement* root = nullptr;
        auto* events = new TargetEvents(epochs_);
        HWND subscribed = nullptr;
        if (SUCCEEDED(initialized))
            CoCreateInstance(CLSID_CUIAutomation8, nullptr, CLSCTX_INPROC_SERVER,
                             IID_PPV_ARGS(&automation));
        if (automation) {
            IUIAutomation2* bounded = nullptr;
            const bool configured = SUCCEEDED(automation->QueryInterface(IID_PPV_ARGS(&bounded))) &&
                                    SUCCEEDED(bounded->put_ConnectionTimeout(100)) &&
                                    SUCCEEDED(bounded->put_TransactionTimeout(100));
            if (bounded)
                bounded->Release();
            if (!configured) {
                automation->Release();
                automation = nullptr;
            }
        }
        auto platform = std::make_unique<WindowsDesktopPlatform>();
        quint64 qualifiedGeneration = 0;
        int publishedReadiness = -1;
        quint64 publishedGeneration = 0;
        while (!stopping_) {
            const auto iterationBegan = GetTickCount64();
            bool queried = false;
            if (requested_ && active_ && !automation) {
                quint64 failedGeneration = 0;
                {
                    std::lock_guard lock(sessionMutex_);
                    const auto generation = generation_.load();
                    if (querySessionCurrent(generation)) {
                        failedGeneration = generation;
                        needsRestart_ = true;
                        requested_ = false;
                        active_ = false;
                    }
                }
                if (failedGeneration) {
                    queueStatusChanged(failedGeneration, false,
                                       QStringLiteral("UIA 初始化不可用，已保留系统入口。"),
                                       QStringLiteral("start-capture-uia-unavailable"));
                    wake();
                }
            }
            if (requested_ && active_ && automation) {
                queried = true;
                const auto queryGeneration = generation_.load();
                const auto epoch = epoch_.load();
                StartButtonQueryObservation observation;
                IUIAutomationElement* queriedButton = nullptr;
                auto target = platform->queryStartButtonCaptureTarget(
                    automation, options_.mode, epoch, &observation,
                    options_.mode == StartButtonCaptureMode::ExperimentalAreaCache ? &queriedButton
                                                                                   : nullptr);
                ++queryCount_;
                const auto duration = observation.durationMs;
                size_t bucket = 0;
                while (bucket < queryLatencyLimits_.size() &&
                       duration > queryLatencyLimits_[bucket])
                    ++bucket;
                ++queryLatency_[bucket];
                queryDurationTotal_.fetch_add(duration);
                if (duration > queryDurationMax_.load())
                    queryDurationMax_ = duration;
                {
                    std::lock_guard lock(sessionMutex_);
                    if (!querySessionCurrent(queryGeneration)) {
                        target.identified = false;
                        observation.reason = StartButtonCaptureReason::SessionSuperseded;
                        observation.stage = QStringLiteral("query-session-superseded");
                    }
                }
                if (observation.reusedElement)
                    ++queryCacheHits_;
                const HWND bar = reinterpret_cast<HWND>(target.taskbar);
                BOOL sameButton = FALSE;
                const bool area = options_.mode == StartButtonCaptureMode::ExperimentalAreaCache;
                const bool sameSubscription =
                    !area ||
                    (target.identified && root && queriedButton &&
                     SUCCEEDED(automation->CompareElements(root, queriedButton, &sameButton)) &&
                     sameButton);
                if (target.identified && (bar != subscribed || !sameSubscription)) {
                    const auto subscriptionBegan = GetTickCount64();
                    automation->RemoveAllEventHandlers();
                    if (root) {
                        root->Release();
                        root = nullptr;
                    }
                    subscribed = nullptr;
                    PROPERTYID properties[] = {UIA_BoundingRectanglePropertyId,
                                               UIA_IsOffscreenPropertyId,
                                               UIA_AutomationIdPropertyId,
                                               UIA_ControlTypePropertyId, UIA_ProcessIdPropertyId};
                    HRESULT rootResult = E_POINTER;
                    if (area && queriedButton) {
                        root = queriedButton;
                        root->AddRef();
                        rootResult = S_OK;
                    } else if (!area) {
                        rootResult = automation->ElementFromHandle(bar, &root);
                    }
                    const HRESULT structureResult =
                        SUCCEEDED(rootResult) ? automation->AddStructureChangedEventHandler(
                                                    root, TreeScope_Subtree, nullptr, events)
                                              : E_PENDING;
                    const HRESULT propertiesResult =
                        SUCCEEDED(rootResult) && SUCCEEDED(structureResult)
                            ? automation->AddPropertyChangedEventHandlerNativeArray(
                                  root, area ? TreeScope_Element : TreeScope_Subtree, nullptr,
                                  events, properties, static_cast<int>(std::size(properties)))
                            : E_PENDING;
                    subscriptionRoot_ = rootResult;
                    subscriptionGeneration_ = queryGeneration;
                    subscriptionStructure_ = structureResult;
                    subscriptionProperties_ = propertiesResult;
                    subscriptionDuration_ = GetTickCount64() - subscriptionBegan;
                    const auto subscription = startButtonSubscriptionOutcome(
                        rootResult, structureResult, propertiesResult);
                    bool subscriptionCurrent = false;
                    bool failedCurrentSubscription = false;
                    {
                        std::lock_guard lock(sessionMutex_);
                        subscriptionCurrent = querySessionCurrent(queryGeneration);
                        if (subscriptionCurrent) {
                            if (subscription.ready)
                                subscribed = bar;
                            observedTaskbar_ = reinterpret_cast<quintptr>(subscribed);
                            epochs_->invalidate(EpochSource::Subscription);
                            if (!subscription.ready) {
                                needsRestart_ = true;
                                requested_ = false;
                                active_ = false;
                                failedCurrentSubscription = true;
                            }
                        }
                    }
                    if (!subscriptionCurrent) {
                        // Only this worker owns event registrations. Remove an
                        // obsolete registration without disabling a newer GUI
                        // session, even if the old COM operation failed.
                        automation->RemoveAllEventHandlers();
                        if (root) {
                            root->Release();
                            root = nullptr;
                        }
                        subscribed = nullptr;
                        observedTaskbar_ = 0;
                        observation.reason = StartButtonCaptureReason::SessionSuperseded;
                        observation.stage = QStringLiteral("subscription-session-superseded");
                    } else if (failedCurrentSubscription) {
                        // Installed hooks are not usable without the required
                        // invalidation subscription. Do not keep a false active
                        // state while every candidate silently falls through.
                        observation.reason = StartButtonCaptureReason::SubscriptionFailed;
                        observation.stage =
                            subscription.stage == StartButtonSubscriptionStage::Root
                                ? QStringLiteral("subscription-root")
                            : subscription.stage == StartButtonSubscriptionStage::Structure
                                ? QStringLiteral("subscription-structure")
                                : QStringLiteral("subscription-properties");
                        observation.result = subscription.result;
                        queueStatusChanged(
                            queryGeneration, false,
                            QStringLiteral("UIA事件订阅失败，接管已停止；系统入口保留。"),
                            QStringLiteral("start-capture-subscription-failed"));
                        wake();
                    }
                }
                if (queriedButton)
                    queriedButton->Release();
                target.epoch = epoch;
                target.sessionGeneration = queryGeneration;
                bool currentSession = false;
                {
                    std::lock_guard lock(sessionMutex_);
                    const bool sameGeneration = queryGeneration == generation_ &&
                                                isCurrentEventSession(queryGeneration) &&
                                                !stopping_;
                    currentSession = querySessionCurrent(queryGeneration);
                    if (!sameGeneration) {
                        observation.reason = StartButtonCaptureReason::SessionSuperseded;
                        observation.stage = QStringLiteral("publication-session-superseded");
                    }
                    target.identified =
                        target.identified && currentSession && epoch == epoch_ && bar == subscribed;
                }
                if (!target.identified && observation.reason == StartButtonCaptureReason::None) {
                    observation.reason = StartButtonCaptureReason::EpochChanged;
                    observation.stage = QStringLiteral("epoch-changed-before-publication");
                }
                queryReasons_[static_cast<size_t>(observation.reason)].fetch_add(
                    1, std::memory_order_relaxed);
                {
                    std::lock_guard lock(queryDiagnosticsMutex_);
                    lastQuery_ = {
                        {QStringLiteral("startedAt"), qint64(iterationBegan)},
                        {QStringLiteral("completedAt"), qint64(GetTickCount64())},
                        {QStringLiteral("qualificationObservedAt"), qint64(target.observedAt)},
                        {QStringLiteral("durationMs"), static_cast<qint64>(observation.durationMs)},
                        {QStringLiteral("queryGeneration"), static_cast<qint64>(queryGeneration)},
                        {QStringLiteral("reason"),
                         QString::fromLatin1(startButtonCaptureReasonName(observation.reason))},
                        {QStringLiteral("stage"), observation.stage},
                        {QStringLiteral("hresult"), observation.result},
                        {QStringLiteral("details"), observation.details},
                        {QStringLiteral("reusedElement"), observation.reusedElement},
                        {QStringLiteral("identified"), target.identified}};
                }
                if (target.identified && qualifiedGeneration != queryGeneration) {
                    qualifiedGeneration = queryGeneration;
                    qInfo() << "Start button qualification observed for session" << queryGeneration
                            << "with lease age" << GetTickCount64() - target.observedAt << "ms";
                }
                {
                    std::lock_guard lock(snapshotMutex_);
                    snapshot_ = target;
                }
                const bool available = target.identified && StartButtonCaptureState::qualifies(
                                                                target, target.qualifiedPoint,
                                                                GetTickCount64(), options_.mode);
                const bool observed = lastInputAt_ && lastInputGeneration_ == queryGeneration;
                const int readiness = (available ? 1 : 0) | (observed ? 2 : 0);
                if (currentSession &&
                    (readiness != publishedReadiness || publishedGeneration != queryGeneration)) {
                    publishedReadiness = readiness;
                    publishedGeneration = queryGeneration;
                    const QString mode = options_.mode == StartButtonCaptureMode::Point
                                             ? QStringLiteral("点模式")
                                             : QStringLiteral("实验区域缓存模式");
                    queueStatusChanged(
                        queryGeneration, true,
                        QStringLiteral("%1Hook已安装；%2。%3持续活性仍未知。")
                            .arg(mode,
                                 available ? QStringLiteral("当前资格可用")
                                           : QStringLiteral("当前没有可用资格，点击交系统处理"),
                                 observed ? QStringLiteral("本次已观察到输入回调，")
                                          : QStringLiteral("本次尚未观察到输入回调，")),
                        {});
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(startButtonCaptureQueryDelay(
                queried ? options_.mode : StartButtonCaptureMode::Point,
                GetTickCount64() - iterationBegan)));
        }
        platform.reset(); // Cached UIA elements must die on this still-initialized MTA.
        if (automation) {
            automation->RemoveAllEventHandlers();
            automation->Release();
        }
        if (root)
            root->Release();
        events->Release();
        if (SUCCEEDED(initialized))
            CoUninitialize();
    }

    void hookLoop() {
        current_ = this;
        MSG message{};
        PeekMessageW(&message, nullptr, WM_USER, WM_USER, PM_NOREMOVE);
        threadId_ = GetCurrentThreadId();
        const auto timer = SetTimer(nullptr, 0, 100, nullptr);
        HHOOK hook = nullptr;
        HHOOK keyboard = nullptr;
        const auto events =
            SetWinEventHook(EVENT_OBJECT_DESTROY, EVENT_OBJECT_LOCATIONCHANGE, nullptr, windowEvent,
                            0, 0, WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);
        bool announced = false;
        bool announcedActive = false;
        quint64 announcedGeneration = 0;
        ULONGLONG drainingSince = 0;
        ULONGLONG desktopCheckedAt = 0;
        for (;;) {
            if (generation_ != announcedGeneration)
                announced = false;
            if (requested_ && !hook && timer && events && inputDesktopAvailable()) {
                keyboard = SetWindowsHookExW(WH_KEYBOARD_LL, keyboardCallback,
                                             GetModuleHandleW(nullptr), 0);
                if (keyboard)
                    hook =
                        SetWindowsHookExW(WH_MOUSE_LL, mouseCallback, GetModuleHandleW(nullptr), 0);
                active_ = hook && keyboard;
                hookInstalled_ = hook != nullptr;
                if (!active_) {
                    needsRestart_ = true;
                    requested_ = false;
                    if (keyboard) {
                        UnhookWindowsHookEx(keyboard);
                        keyboard = nullptr;
                    }
                }
                announced = false;
            }
            if (hook && requested_ && generation_ != announcedGeneration) {
                active_ = true;
                announced = false;
            }
            const auto now = GetTickCount64();
            if (active_ && now - desktopCheckedAt >= 100 && !inputDesktopAvailable()) {
                requested_ = false;
                active_ = false;
                epochs_->invalidate(EpochSource::Desktop);
            }
            if (now - desktopCheckedAt >= 100)
                desktopCheckedAt = now;
            if (!requested_ || stopping_)
                gesture_.cancel(stopping_ ? StartButtonCaptureReason::Stopping
                                          : StartButtonCaptureReason::Disabled);
            // Drain an owned release on pause/normal exit. The native Shell has
            // never seen its down, so forwarding just its up is not a fallback.
            if (hook && (!requested_ || stopping_) && !gesture_.ownsPress()) {
                UnhookWindowsHookEx(hook);
                hook = nullptr;
                hookInstalled_ = false;
                active_ = false;
                if (keyboard) {
                    UnhookWindowsHookEx(keyboard);
                    keyboard = nullptr;
                }
            }
            if (stopping_ && !gesture_.ownsPress())
                break;
            // A disconnected device must not hang process shutdown forever.
            // No action is dispatched after stop; a late unmatched up is left
            // to Windows after the bounded drain expires.
            if (stopping_ && !drainingSince)
                drainingSince = GetTickCount64();
            if (drainingSince && GetTickCount64() - drainingSince >= 2000)
                break;
            if (!announced || announcedActive != active_) {
                announced = true;
                announcedActive = active_;
                announcedGeneration = generation_;
                queueStatusChanged(
                    generation_, active_,
                    active_ ? QStringLiteral("开始按钮Hook已安装；资格和持续输入活性尚未确认。")
                            : QStringLiteral("开始按钮接管不可用，已保留系统入口。"),
                    active_ ? QString() : QStringLiteral("start-capture-unavailable"));
                if (!active_)
                    requested_ = false;
                if (!timer || !events)
                    needsRestart_ = true;
            }
            const int received = GetMessageW(&message, nullptr, 0, 0);
            if (received <= 0)
                break;
            if (message.message == toggleMessage) {
                const auto sequence = static_cast<quint64>(message.lParam);
                const auto& slot = samples_[(sequence ? sequence - 1 : 0) % samples_.size()];
                CaptureToggleContext context{slot.originSession.load(), slot.gestureId.load(),
                                             slot.pressedAt.load(), slot.at.load()};
                if (sequence && slot.sequence.load() == sequence && context.gestureId &&
                    context.sessionGeneration == static_cast<quint64>(message.wParam) && active_ &&
                    requested_ && !stopping_) {
                    queueToggleRequested(context);
                } else {
                    ++nativeDispatchDropped_;
                    StartButtonHostObservation observation;
                    if (sequence && slot.sequence.load() == sequence)
                        observation.context = context;
                    observation.event = QStringLiteral("native-toggle-discarded");
                    observation.errorCode = QStringLiteral("stale-session-or-evicted-input");
                    recordHostObservation(observation);
                }
            } else if (message.message != WM_TIMER && message.message != applyMessage) {
                TranslateMessage(&message);
                DispatchMessageW(&message);
            }
        }
        active_ = false;
        if (hook)
            UnhookWindowsHookEx(hook);
        hookInstalled_ = false;
        if (keyboard)
            UnhookWindowsHookEx(keyboard);
        if (events)
            UnhookWinEvent(events);
        if (timer)
            KillTimer(nullptr, timer);
        threadId_ = 0;
        if (!stopping_) {
            requested_ = false;
            epochs_->invalidate(EpochSource::MessageLoop);
            queueStatusChanged(generation_, false,
                               QStringLiteral("开始按钮消息线程已停止，使用系统入口。"),
                               QStringLiteral("start-capture-message-loop-ended"));
        }
        current_ = nullptr;
        hookExited_ = true;
    }

    std::atomic_bool requested_{false}, active_{false}, stopping_{false};
    std::atomic_bool hookExited_{false};
    std::atomic_bool needsRestart_{false};
    std::atomic<quint64> generation_{0};
    StartButtonCaptureOptions options_;
    std::shared_ptr<EpochTracker> epochs_ = std::make_shared<EpochTracker>();
    std::atomic<quint64>& epoch_ = epochs_->value;
    std::atomic<quintptr> observedTaskbar_{0};
    std::atomic<DWORD> threadId_{0};
    std::thread queryThread_, hookThread_;
    mutable std::mutex snapshotMutex_;
    mutable std::mutex sessionMutex_;
    StartButtonCaptureTarget snapshot_;
    std::atomic_bool hookInstalled_{false};
    std::atomic<quint64> lastInputAt_{0}, lastInputGeneration_{0}, inputCallbacks_{0},
        physicalInputCallbacks_{0}, keyboardCallbacks_{0};
    std::atomic<quint64> consumedDown_{0}, consumedUp_{0}, toggleQueued_{0}, dispatchFailures_{0};
    std::atomic<quint64> queryCount_{0}, queryCacheHits_{0}, subscriptionDuration_{0};
    std::atomic<quint64> subscriptionGeneration_{0};
    std::atomic<qint64> subscriptionRoot_{E_PENDING}, subscriptionStructure_{E_PENDING},
        subscriptionProperties_{E_PENDING};
    std::array<std::atomic<quint64>, reasonCount> downReasons_{}, upReasons_{},
        upCanceledReasons_{}, queryReasons_{};
    std::array<InputSample, 512> samples_{};
    StartButtonWindowEventJournal windowEvents_;
    std::atomic<quint64> sampleSequence_{0}, nativeDispatchDropped_{0};
    StartButtonGestureTracker gestureTracker_; // hook thread only
    static constexpr std::array<quint64, 8> queryLatencyLimits_{1, 5, 10, 25, 50, 100, 200, 500};
    std::array<std::atomic<quint64>, 9> queryLatency_{};
    std::atomic<quint64> queryDurationTotal_{0}, queryDurationMax_{0};
    mutable std::mutex queryDiagnosticsMutex_, hostMutex_;
    QJsonObject lastQuery_, lastHost_;
    QList<QJsonObject> hostHistory_;
    std::atomic<quint64> hostObservations_{0}, hostForeground_{0};
    StartButtonCaptureState gesture_; // hook thread only
    static thread_local WindowsStartButtonCapture* current_;
};
thread_local WindowsStartButtonCapture* WindowsStartButtonCapture::current_ = nullptr;
} // namespace

std::unique_ptr<WinKeyCapture> createStartButtonCapture(StartButtonCaptureOptions options) {
    return std::make_unique<WindowsStartButtonCapture>(options);
}
} // namespace xdstarter
