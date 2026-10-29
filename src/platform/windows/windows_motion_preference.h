#pragma once

#include "platform/motion_preference.h"

#include <QAbstractNativeEventFilter>

#include <functional>
#include <optional>

namespace xdstarter {

class WindowsMotionPreference final : public MotionPreference, public QAbstractNativeEventFilter {
  public:
    using Reader = std::function<std::optional<bool>()>;
    explicit WindowsMotionPreference(QObject* parent = nullptr, Reader reader = {});
    ~WindowsMotionPreference() override;
    void refresh() override;
    bool nativeEventFilter(const QByteArray& eventType, void* message, qintptr* result) override;

  private:
    Reader reader_;
    bool refreshPending_ = false;
};

} // namespace xdstarter
