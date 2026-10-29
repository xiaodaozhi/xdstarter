#pragma once

#include "platform/contrast_preference.h"

#include <QAbstractNativeEventFilter>

#include <functional>
#include <optional>

namespace xdstarter {

class WindowsContrastPreference final : public ContrastPreference, public QAbstractNativeEventFilter {
  public:
    using Reader = std::function<std::optional<ContrastState>()>;
    explicit WindowsContrastPreference(QObject* parent = nullptr, Reader reader = {});
    ~WindowsContrastPreference() override;
    void refresh() override;
    bool nativeEventFilter(const QByteArray& eventType, void* message, qintptr* result) override;

  private:
    Reader reader_;
    bool refreshPending_ = false;
};

} // namespace xdstarter
