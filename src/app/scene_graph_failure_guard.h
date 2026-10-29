#pragma once

#include <QObject>

class QCoreApplication;
class QQuickWindow;

namespace xdstarter {

// The window must still be hidden when this guard is installed. Qt delivers
// scene graph initialization failures on the GUI thread.
class SceneGraphFailureGuard final : public QObject {
  public:
    SceneGraphFailureGuard(QQuickWindow& window, QCoreApplication& application);
    [[nodiscard]] bool failed() const { return failed_; }

  private:
    bool failed_ = false;
};

} // namespace xdstarter
